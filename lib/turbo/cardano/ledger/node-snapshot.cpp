#include <turbo/cardano/ledger/node-snapshot.hpp>
#include <turbo/cbor/compare.hpp>
#include <turbo/crypto/crc32.hpp>
#include <turbo/json.hpp>
#include <charconv>

namespace turbo::cardano::ledger::node_snapshot {
    namespace {
        // Share record serialization between direct comparison and file export.
        // Keep only one record in flight, independently of the UTxO count.
        void encode_outputs(const txo_map &outputs, const auto &sink)
        {
            for (const auto &[key, value]: outputs)
                sink(pack_input(key), pack_output(value));
        }

        uint64_t number(const std::string_view s)
        {
            uint64_t n;
            const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
            if (ec != std::errc {} || end != s.data() + s.size()) throw error("invalid snapshot epoch or slot");
            return n;
        }
        void array_size(cbor::zero2::value &v, const size_t size)
        {
            if (v.type() != cbor::major_type::array || v.indefinite() || v.special_uint() != size)
                throw error{"expected a {}-element snapshot array", size};
        }
        uint32_t file_crc(const std::filesystem::path &path)
        {
            CRC32 crc;
            file::read_stream in { path.string() };
            uint8_vector buf(1024 * 1024);
            for (;;) {
                const auto n = in.try_read(buf);
                if (!n) {
                    if (!in.eof()) throw error{"cannot read {}", path.string()};
                    break;
                }
                crc.add(buf.data(), n);
            }
            uint32_t value;
            crc.getHash(&value);
            return value;
        }
        uint32_t combined_crc(const uint32_t state, const uint32_t tables)
        {
            const auto decimal = fmt::format("{}{}", state, tables);
            return crypto::crc32::digest(buffer { decimal });
        }
        uint32_t metadata_checksum(const std::filesystem::path &dir)
        {
            const auto value = json::load((dir / "meta").string());
            const auto &meta = value.as_object();
            if (meta.size() != 3 || meta.at("backend").as_string() != "utxohd-mem"
                || meta.at("tablesCodecVersion").to_number<uint64_t>() != 1)
                throw error{"{}: expected V2InMemory tables codec 1 metadata", dir.string()};
            return meta.at("checksum").to_number<uint32_t>();
        }
        // Read only the small tables envelope and one packed entry at a time.
        // Hash refill buffers, so successful comparisons verify every byte once.
        class table_reader {
        public:
            explicit table_reader(const std::filesystem::path &path)
                : _in { path.string() }, _size { std::filesystem::file_size(path) }
            {
                if (head(4) != 1) throw error("expected a one-element tables array");
                const auto map = byte();
                if (map == 0xbf) _indefinite = true;
                else _remaining = head_value(map, 5);
            }

            bool next()
            {
                if (_finished) return false;
                if ((_indefinite && peek() == 0xff) || (!_indefinite && !_remaining)) {
                    if (_indefinite) byte();
                    if (_consumed != _size) throw error("trailing tables data");
                    _finished = true;
                    return false;
                }
                const auto key_size = head(2);
                if (key_size != _key.size()) throw error("expected a 34-byte packed transaction input");
                read(_key.data(), _key.size());
                const auto key = unpack_input(buffer { _key.data(), _key.size() });
                if (_previous && !(key > *_previous)) throw error("tables: keys are not strictly ordered");
                _previous = key;
                const auto size = head(2);
                if (size > _size - _consumed) throw error("truncated packed output");
                _value.resize(size);
                read(_value.data(), _value.size());
                if (!_indefinite) --_remaining;
                return true;
            }
            const tx_out_ref &key() const { return *_previous; }
            buffer value() const { return _value; }
            uint32_t checksum()
            {
                if (!_finished) throw error("tables checksum requested before end of input");
                uint32_t value;
                _crc.getHash(&value);
                return value;
            }
        private:
            file::read_stream _in;
            const uint64_t _size;
            uint64_t _consumed = 0, _remaining = 0;
            bool _indefinite = false, _finished = false;
            uint8_vector _buffer = uint8_vector(1024 * 1024);
            size_t _pos = 0, _end = 0;
            CRC32 _crc;
            std::array<uint8_t, 34> _key;
            uint8_vector _value;
            std::optional<tx_out_ref> _previous;

            uint8_t peek()
            {
                if (_pos == _end) {
                    _end = _in.try_read(_buffer);
                    _pos = 0;
                    if (!_end) throw error("truncated or unreadable tables file");
                    _crc.add(_buffer.data(), _end);
                }
                return _buffer[_pos];
            }
            uint8_t byte()
            {
                const auto b = peek();
                ++_pos;
                ++_consumed;
                return b;
            }
            void read(uint8_t *out, size_t size)
            {
                while (size) {
                    peek();
                    const auto n = std::min(size, _end - _pos);
                    std::memcpy(out, _buffer.data() + _pos, n);
                    _pos += n;
                    _consumed += n;
                    out += n;
                    size -= n;
                }
            }
            uint64_t head(const uint8_t major) { return head_value(byte(), major); }
            uint64_t head_value(const uint8_t b, const uint8_t major)
            {
                if ((b >> 5) != major) throw error("unexpected CBOR type in tables");
                const auto extra = b & 31;
                if (extra < 24) return extra;
                if (extra > 27) throw error("unsupported CBOR length in tables");
                uint64_t value = 0;
                for (size_t n = size_t { 1 } << (extra - 24); n; --n) value = (value << 8) | byte();
                return value;
            }
        };

        void compare_state(const buffer expected, const buffer actual)
        {
            if (const auto diff = cbor::compare(expected, actual, true); !diff.empty())
                throw error{"state {}", diff};
        }
        void compare_entry(const size_t i, const table_reader &expected,
            const tx_out_ref &key, const buffer value, const uint64_t era)
        {
            if (expected.key() != key)
                throw error{"tables[{}].key: expected {} actual {}", i, expected.key(), key};
            try {
                if (const auto diff = compare_outputs(expected.value(), value, era); !diff.empty())
                    throw error{"value {} (fields: 0 address, 1 coin, 2 assets, 3 datum, 4 script)", diff};
            } catch (const std::exception &ex) {
                throw error{"tables[{}] {}: {}", i, key, ex.what()};
            }
        }
    }

    std::vector<reference> discover(const std::filesystem::path &root, const uint64_t from, const uint64_t to)
    {
        if (to < from) throw error("--to must be at least --from");
        std::vector<reference> refs;
        for (const auto &entry: std::filesystem::directory_iterator { root }) {
            const auto name = entry.path().filename().string();
            if (name == "epochs.json" || name == "epochs.json.tmp") continue;
            const auto split = name.find("_epoch-");
            if (split == std::string::npos || !entry.is_directory())
                throw error{"unexpected entry in snapshot directory: {}", name};
            const auto slot = number(std::string_view { name }.substr(0, split));
            const auto epoch = number(std::string_view { name }.substr(split + 7));
            if (epoch < from || epoch > to) continue;
            for (const auto *part: { "state", "tables", "meta" })
                if (!std::filesystem::is_regular_file(entry.path() / part))
                    throw error{"incomplete snapshot: {} is missing {}", name, part};
            refs.push_back({ entry.path(), epoch, slot });
        }
        std::sort(refs.begin(), refs.end(), [](const auto &a, const auto &b) { return a.slot < b.slot; });
        for (size_t i = 1; i < refs.size(); ++i)
            if (refs[i].slot == refs[i - 1].slot || refs[i].epoch <= refs[i - 1].epoch)
                throw error("snapshot epochs and slots must increase together");
        if (refs.empty()) throw error("no reference snapshots in the requested epoch range");
        return refs;
    }

    state_info inspect(const buffer bytes)
    {
        auto parsed = cbor::zero2::parse(bytes);
        auto &root = parsed.get();
        array_size(root, 2);
        // zero2 is forward-only: at(n) skips n items from the current position.
        auto &envelope = root.array();
        if (envelope.read().uint() != 1) throw error("unsupported snapshot state version");
        auto &ext = envelope.read();
        array_size(ext, 2);
        auto &eras = ext.at(0);
        const auto era = eras.special_uint();
        if (eras.type() != cbor::major_type::array || eras.indefinite() || era < 2 || era > 7)
            throw error("C++ snapshot export supports Shelley through Conway, not Byron or later eras");
        auto &versioned = eras.at(era - 1).at(1);
        array_size(versioned, 2);
        auto &versioned_fields = versioned.array();
        if (versioned_fields.read().uint() != 2) throw error("unsupported Shelley ledger version");
        auto &ledger = versioned_fields.read();
        array_size(ledger, 4);
        auto &ledger_fields = ledger.array();
        auto &tip = ledger_fields.read();
        array_size(tip, 1);
        auto &point_data = tip.at(0);
        array_size(point_data, 3);
        auto &point_fields = point_data.array();
        state_info info { .era=era, .epoch=0 };
        info.tip.slot = point_fields.read().uint();
        info.tip.height = point_fields.read().uint();
        info.tip.hash = point_fields.read().bytes();
        info.epoch = ledger_fields.read().at(0).uint();
        // Peras is disabled in the supported eras. Never silently omit a round.
        array_size(ledger_fields.skip(1).read(), 0); // Skip the transition state.
        if (root.data_raw().size() != bytes.size()) throw error("trailing snapshot state data");
        return info;
    }

    uint32_t checksum(const std::filesystem::path &dir)
    {
        return combined_crc(file_crc(dir / "state"), file_crc(dir / "tables"));
    }
    void write_meta(const std::filesystem::path &dir, const uint32_t state_crc, const uint32_t tables_crc)
    {
        const auto meta = json::serialize(json::object {
            { "backend", "utxohd-mem" }, { "tablesCodecVersion", 1 }, { "checksum", combined_crc(state_crc, tables_crc) }
        });
        file::write((dir / "meta").string(), buffer { meta });
    }
    void write_meta(const std::filesystem::path &dir)
    {
        write_meta(dir, file_crc(dir / "state"), file_crc(dir / "tables"));
    }
    void verify(const std::filesystem::path &dir)
    {
        if (metadata_checksum(dir) != checksum(dir))
            throw error{"{}: snapshot checksum mismatch", dir.string()};
    }

    reader::reader(const std::filesystem::path &path)
        : _path { path }, _state { file::read((path / "state").string()) },
          _info { inspect(_state) }, _checksum { metadata_checksum(path) }
    {
    }

    void reader::verify_tables(const uint32_t crc) const
    {
        if (combined_crc(crypto::crc32::digest(_state), crc) != _checksum)
            throw error{"{}: snapshot checksum mismatch", _path.string()};
    }

    void reader::compare(const buffer state, const txo_map &outputs) const
    {
        compare_state(_state, state);
        table_reader tables { _path / "tables" };
        size_t i = 0;
        encode_outputs(outputs, [&](const buffer key, const buffer value) {
            if (!tables.next()) throw error{"tables[{}]: extra output", i};
            compare_entry(i++, tables, unpack_input(key), value, _info.era);
        });
        if (tables.next()) throw error{"tables[{}]: missing output", i};
        verify_tables(tables.checksum());
    }

    void reader::compare(const reader &actual) const
    {
        compare_state(_state, actual._state);
        table_reader a { _path / "tables" }, b { actual._path / "tables" };
        size_t i = 0;
        while (a.next()) {
            if (!b.next()) throw error{"tables[{}]: missing output", i};
            compare_entry(i++, a, b.key(), b.value(), _info.era);
        }
        if (b.next()) throw error{"tables[{}]: extra output", i};
        verify_tables(a.checksum());
        actual.verify_tables(b.checksum());
    }

    uint32_t write_tables(const std::filesystem::path &path, const txo_map &outputs)
    {
        file::write_stream out { path.string(), 1024 * 1024 };
        CRC32 crc;
        const auto write = [&](const buffer b) { out.write(b); crc.add(b.data(), b.size()); };
        cbor::encoder enc;
        enc.array(1).map();
        write(enc.cbor());
        encode_outputs(outputs, [&](const buffer key, const buffer value) {
            enc.cbor().clear();
            enc.bytes(key).bytes(value);
            write(enc.cbor());
        });
        enc.cbor().clear();
        enc.s_break();
        write(enc.cbor());
        out.close();
        uint32_t value;
        crc.getHash(&value);
        return value;
    }

    void compare(const std::filesystem::path &expected, const std::filesystem::path &actual)
    {
        reader { expected }.compare(reader { actual });
    }
}
