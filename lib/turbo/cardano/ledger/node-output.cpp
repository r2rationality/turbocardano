#include <turbo/cardano/ledger/node-output.hpp>
#include <limits>
#include <set>

namespace turbo::cardano::ledger::node_snapshot {
    namespace {
        enum output_tag : uint8_t { plain = 0, datum_hash_output = 1, ada_only = 2,
            ada_only_with_hash = 3, inline_datum = 4, reference_script = 5 };
        constexpr size_t quantity_size = 8, offset_size = 2;
        constexpr size_t asset_entry_size = quantity_size + 2 * offset_size;
        // MemPack uses big-endian base-128 lengths, native little-endian words,
        // and raw hash bytes. See cardano-ledger f649f975 (core 1.21), TxOut,
        // Mary.Value and Coin; Consensus 82ecba32 uses big-endian TxIx.
        struct packed_reader {
            buffer data;
            size_t pos = 0;

            buffer take(const size_t n)
            {
                if (n > data.size() - pos)
                    throw error{"truncated MemPack record at byte {} (need {})", pos, n};
                const auto b = data.subbuf(pos, n);
                pos += n;
                return b;
            }
            uint8_t byte() { return take(1)[0]; }
            uint64_t var()
            {
                uint64_t v = 0;
                for (size_t i = 0; i < 10; ++i) {
                    const auto b = byte();
                    if (v > (std::numeric_limits<uint64_t>::max() >> 7))
                        throw error("MemPack integer overflow");
                    v = (v << 7) | (b & 0x7f);
                    if (!(b & 0x80)) return v;
                }
                throw error("MemPack integer is too long");
            }
            buffer bytes() { return take(var()); }
            void end() const
            {
                if (pos != data.size())
                    throw error{"trailing MemPack data at byte {}", pos};
            }
        };

        void put_var(uint8_vector &out, uint64_t v)
        {
            unsigned shift = 0;
            for (auto n = v; n >= 128; n >>= 7) shift += 7;
            for (; shift; shift -= 7) out << static_cast<uint8_t>((v >> shift) | 0x80);
            out << static_cast<uint8_t>(v & 0x7f);
        }
        void put_bytes(uint8_vector &out, const buffer b) { put_var(out, b.size()); out << b; }
        uint64_t little(const buffer b)
        {
            uint64_t v = 0;
            for (size_t i = 0; i < b.size(); ++i) v |= uint64_t { b[i] } << (8 * i);
            return v;
        }
        void put_little(uint8_vector &out, const size_t at, uint64_t v, const size_t n)
        {
            for (size_t i = 0; i < n; ++i) out.at(at + i) = static_cast<uint8_t>(v >> (8 * i));
        }
        void put_big(uint8_vector &out, uint64_t v, size_t n)
        {
            while (n) out << static_cast<uint8_t>(v >> (8 * --n));
        }

        void pack_value(uint8_vector &out, const tx_out_data &txo)
        {
            out << static_cast<uint8_t>(!txo.assets.empty());
            put_var(out, txo.coin);
            if (txo.assets.empty()) return;
            struct asset_entry { const script_hash *policy; const asset_name_t *name; uint64_t quantity; };
            std::vector<asset_entry> entries;
            std::map<asset_name_t, size_t> names;
            for (const auto &[policy, assets]: txo.assets) {
                for (const auto &[name, amount]: assets) {
                    entries.push_back({ &policy, &name, amount });
                    names.try_emplace(name, 0);
                }
            }
            const auto count = entries.size();
            if (count > 65535 / asset_entry_size) throw error("compact multiasset table is too large");
            uint8_vector rep(count * asset_entry_size);
            std::map<script_hash, size_t> policies;
            for (const auto &[policy, assets]: txo.assets) {
                policies.emplace(policy, rep.size());
                rep << policy;
            }
            for (auto it = names.rbegin(); it != names.rend(); ++it) {
                it->second = rep.size();
                rep << it->first.span();
            }
            if (rep.size() > 65535) throw error("compact multiasset offsets exceed Word16");
            // Sort only existing assets: descending name, then ascending policy.
            std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
                if (*a.name != *b.name) return *b.name < *a.name;
                return *a.policy < *b.policy;
            });
            for (size_t i = 0; i < count; ++i) {
                const auto &entry = entries[i];
                put_little(rep, quantity_size * i, entry.quantity, quantity_size);
                put_little(rep, quantity_size * count + offset_size * i, policies.at(*entry.policy), offset_size);
                put_little(rep, (quantity_size + offset_size) * count + offset_size * i, names.at(*entry.name), offset_size);
            }
            put_var(out, count);
            put_bytes(out, rep);
        }

        void unpack_value(packed_reader &r, auto &txo, const uint64_t era)
        {
            const auto tag = r.byte();
            if (tag > 1 || (tag && era < 4)) throw error("invalid compact value tag for era");
            txo.coin = r.var();
            if (!tag) return;
            const auto count = r.var();
            const auto rep = r.bytes();
            if (!count || count > rep.size() / asset_entry_size || rep.size() > 65535)
                throw error("invalid compact multiasset size");
            std::set<size_t> offsets { rep.size() };
            for (size_t i = 0; i < count; ++i) {
                const auto off = little(rep.subbuf((quantity_size + offset_size) * count + offset_size * i, 2));
                if (off < asset_entry_size * count || off > rep.size()) throw error("invalid asset name offset");
                offsets.insert(off);
            }
            const auto names_begin = *offsets.begin();
            for (size_t i = 0; i < count; ++i) {
                const auto po = little(rep.subbuf(quantity_size * count + offset_size * i, 2));
                const auto no = little(rep.subbuf((quantity_size + offset_size) * count + offset_size * i, 2));
                const auto next = offsets.upper_bound(no);
                const auto len = next == offsets.end() ? 0 : *next - no;
                if (po < asset_entry_size * count || po > names_begin || names_begin - po < 28 || len > 32)
                    throw error("invalid compact policy or asset name");
                const script_hash policy { rep.subbuf(po, 28) };
                if (!txo.assets[policy].emplace(asset_name_t { rep.subbuf(no, len) }, little(rep.subbuf(8 * i, 8))).second)
                    throw error("duplicate compact asset");
            }
        }

        void pack_datum(uint8_vector &out, const std::optional<datum_option_t> &datum)
        {
            if (!datum) out << uint8_t { 0 };
            else if (const auto *hash = std::get_if<datum_hash>(&datum->val)) { out << uint8_t { 1 }; out << *hash; }
            else { out << uint8_t { 2 }; put_bytes(out, std::get<uint8_vector>(datum->val)); }
        }
        void check_cbor(const buffer bytes)
        {
            auto parsed = cbor::zero2::parse(bytes);
            if (parsed.get().data_raw().size() != bytes.size()) throw error("trailing datum or native script CBOR");
        }
        // Borrow variable-length fields; only optimized addresses/hashes need
        // reconstruction. Assets allocate only on the nonidentical path.
        struct output_view {
            buffer address, datum, script;
            uint64_t coin = 0;
            multi_asset_map assets;
            uint8_t datum_kind = 0;
            std::optional<script_type> script_kind;
            std::array<uint8_t, 57> address_storage {};
            std::array<uint8_t, 32> hash_storage {};

            explicit output_view(buffer, uint64_t era);
            output_view(const output_view &) =delete;

            void read_datum(packed_reader &r)
            {
                datum_kind = r.byte();
                switch (datum_kind) {
                    case 0: break;
                    case 1: datum = r.take(32); break;
                    case 2: datum = r.bytes(); break;
                    default: throw error("invalid MemPack datum tag");
                }
            }
            bool operator==(const output_view &o) const
            {
                return address == o.address && coin == o.coin && assets == o.assets
                    && datum_kind == o.datum_kind && datum == o.datum
                    && script_kind == o.script_kind && script == o.script;
            }
        };

        // Build diagnostic CBOR only on disagreement, retaining nested data
        // and native scripts instead of comparing opaque byte strings.
        uint8_vector output_tree(const output_view &out)
        {
            cbor::encoder enc;
            enc.array(5).bytes(out.address).uint(out.coin).map(out.assets.size());
            for (const auto &[policy, assets]: out.assets) {
                enc.bytes(policy).map(assets.size());
                for (const auto &[name, quantity]: assets) enc.bytes(name.span()).uint(quantity);
            }
            if (!out.datum_kind) enc.array(0);
            else {
                enc.array(2).uint(out.datum_kind - 1);
                if (out.datum_kind == 1) enc.bytes(out.datum);
                else enc.cbor() << out.datum;
            }
            if (!out.script_kind) enc.array(0);
            else {
                enc.array(2).uint(static_cast<uint8_t>(*out.script_kind));
                if (*out.script_kind == script_type::native) enc.cbor() << out.script;
                else enc.bytes(out.script);
            }
            return std::move(enc.cbor());
        }
    }

    uint8_vector pack_input(const tx_out_ref &ref)
    {
        uint8_vector out;
        out.reserve(34);
        out << ref.hash;
        put_big(out, ref.idx, 2);
        return out;
    }
    tx_out_ref unpack_input(const buffer b)
    {
        if (b.size() != 34) throw error("expected a 34-byte packed transaction input");
        return { tx_hash { b.subbuf(0, 32) }, static_cast<tx_out_idx>((uint16_t { b[32] } << 8) | b[33]) };
    }

    stake_pointer normalize_pointer(const stake_pointer &ptr)
    {
        // cardano-ledger's mkPtrNormalized resets the entire pointer on overflow.
        if (ptr.slot > std::numeric_limits<uint32_t>::max()
                || ptr.tx_idx > std::numeric_limits<uint16_t>::max()
                || ptr.cert_idx > std::numeric_limits<uint16_t>::max())
            return {};
        return ptr;
    }

    uint8_vector pack_output(const tx_out_data &txo)
    {
        // Use the general constructors; the node may choose its optimized
        // Ada-only constructors (2/3). Both decode to the same ledger output.
        auto tag = plain;
        if (txo.script_ref) tag = reference_script;
        else if (txo.datum) tag = txo.datum->val.index() == 0 ? datum_hash_output : inline_datum;
        uint8_vector out;
        out << static_cast<uint8_t>(tag);
        // Internal Byron outputs may store only the address payload. Match
        // address::to_cbor: Node's compact address includes tag 24 and the CRC.
        if (!txo.address_raw.empty() && txo.address_raw[0] == 0x83)
            put_bytes(out, byron_crc_protected(txo.address_raw));
        else if (!txo.address_raw.empty() && (txo.address_raw[0] >> 5) == 2) {
            // Pointer addresses: payment-key (type 4) or payment-script (type 5).
            const address addr { txo.address_raw };
            const auto ptr = addr.pointer();
            if (normalize_pointer(ptr) == ptr) {
                put_bytes(out, addr.bytes());
            } else {
                put_var(out, 32); // Header, payment credential, then pointer (0,0,0).
                out << addr.bytes().subbuf(0, 29) << uint8_t { 0 } << uint8_t { 0 } << uint8_t { 0 };
            }
        } else if (txo.address_raw.size() > 57
                || (txo.address_raw.size() > 29 && (txo.address_raw[0] >> 5) == 3)) {
            // Historical base/enterprise addresses may contain ignored suffixes.
            // Reuse address normalization only for oversized encodings; ordinary
            // outputs keep the direct-copy path. Wrapped Byron addresses are unchanged.
            put_bytes(out, address { txo.address_raw }.bytes());
        } else
            put_bytes(out, txo.address_raw);
        pack_value(out, txo);
        if (tag == datum_hash_output) out << std::get<datum_hash>(txo.datum->val);
        else if (tag == inline_datum) put_bytes(out, std::get<uint8_vector>(txo.datum->val));
        else if (tag == reference_script) pack_datum(out, txo.datum);
        if (txo.script_ref) {
            const auto type = static_cast<uint8_t>(txo.script_ref->type());
            out << static_cast<uint8_t>(type != 0);
            if (type) out << static_cast<uint8_t>(type - 1);
            put_bytes(out, txo.script_ref->script());
        }
        return out;
    }

    namespace {
        output_view::output_view(const buffer b, const uint64_t era)
        {
            if (era < 2 || era > 7) throw error("snapshot tables support Shelley through Conway only");
            packed_reader r { b };
            const auto tag = r.byte();
            if (tag > reference_script || (era < 5 && tag) || (era < 6 && tag >= inline_datum))
                throw error("invalid output tag for era");
            const bool optimized = tag == ada_only || tag == ada_only_with_hash;
            if (optimized) {
                const auto cred = r.byte();
                if (cred > 1) throw error("invalid stake credential tag");
                const auto stake = r.take(28);
                const auto words = r.take(32);
                const auto flags = little(words.subbuf(24, 8));
                if (flags & 0xfffffffcULL) throw error("invalid optimized address flags");
                address_storage[0] = ((flags & 1) ? 0 : 0x10) | (cred ? 0 : 0x20) | ((flags & 2) ? 1 : 0);
                for (size_t i = 0; i < 28; ++i)
                    address_storage[1 + i] = words[(i / 8) * 8 + 7 - i % 8];
                std::copy(stake.begin(), stake.end(), address_storage.begin() + 29);
                address = buffer { address_storage.data(), address_storage.size() };
            } else address = r.bytes();
            unpack_value(r, *this, era);
            if (optimized && !assets.empty()) throw error("multiasset value in an Ada-only output");
            if (tag == datum_hash_output) {
                datum_kind = 1;
                datum = r.take(32);
            } else if (tag == ada_only_with_hash) {
                const auto words = r.take(32);
                for (size_t i = 0; i < 32; ++i)
                    hash_storage[i] = words[(i / 8) * 8 + 7 - i % 8];
                datum_kind = 1;
                datum = buffer { hash_storage.data(), hash_storage.size() };
            } else if (tag == inline_datum) {
                // Constructor 4 stores BinaryData directly; only constructor 5
                // stores the tagged Datum sum (none, hash, or inline data).
                datum_kind = 2;
                datum = r.bytes();
            } else if (tag == reference_script) read_datum(r);
            if (tag == reference_script) {
                const auto kind = r.byte();
                uint8_t language = 0;
                if (kind == 1) {
                    const auto encoded = r.byte();
                    if (encoded >= (era == 6 ? 2 : 3)) throw error("invalid Plutus script language");
                    language = encoded + 1;
                } else if (kind != 0) throw error("invalid script tag");
                script_kind = static_cast<script_type>(language);
                script = r.bytes();
            }
            r.end();
            if (datum_kind == 2) check_cbor(datum);
            if (script_kind == script_type::native) check_cbor(script);
        }
    }

    tx_out_data unpack_output(const buffer b, const uint64_t era)
    {
        output_view view { b, era };
        tx_out_data out { .address_raw=uint8_vector { view.address }, .coin=view.coin, .assets=std::move(view.assets) };
        if (view.datum_kind == 1) out.datum = datum_option_t { datum_hash { view.datum } };
        else if (view.datum_kind == 2) out.datum = datum_option_t { uint8_vector { view.datum } };
        if (view.script_kind) out.script_ref.emplace(*view.script_kind, view.script);
        return out;
    }

    cbor::diff_list compare_outputs(const buffer expected, const buffer actual, const uint64_t era)
    {
        if (expected == actual) return {};
        const output_view a { expected, era }, b { actual, era };
        if (a == b) return {};
        return cbor::compare(output_tree(a), output_tree(b), true);
    }
}
