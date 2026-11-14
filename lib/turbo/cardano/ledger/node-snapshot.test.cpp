#include <turbo/cardano/ledger/node-snapshot.hpp>
#include <turbo/cbor/compare.hpp>
#include <turbo/common/test.hpp>
#include <turbo/crypto/crc32.hpp>
#include <turbo/json.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    namespace snapshot = cardano::ledger::node_snapshot;

    uint8_vector test_state(uint64_t slot=42, uint64_t epoch=0, uint64_t era=2,
        const block_hash &hash={}, uint64_t height=1)
    {
        // Minimal envelope fixture, not a ledger-validation fixture.
        cbor::encoder enc;
        enc.array(2).uint(1).array(2).array(era);
        for (size_t i = 1; i < era; ++i) enc.array(2).array(0).array(0);
        enc.array(2).array(0).array(2).uint(2).array(4);
        enc.array(1).array(3).uint(slot).uint(height).bytes(hash);
        enc.array(1).uint(epoch).uint(0).array(0); // epoch, transition, Peras
        enc.array(0); // header state
        return std::move(enc.cbor());
    }

    void write_snapshot(const std::filesystem::path &dir, const tx_out_data &out, const bool optimized=false)
    {
        std::filesystem::create_directories(dir);
        file::write(dir / "state", test_state(42, 0, 6));
        cbor::encoder tables;
        tables.array(1).map(1).bytes(snapshot::pack_input({ {}, 256 }));
        if (optimized) {
            // Alonzo/Babbage constructor 2: credential tag + hash, Addr28Extra
            // (four LE words), then CompactCoin. Address is mainnet key/key.
            uint8_vector packed;
            packed << uint8_t { 2 } << uint8_t { 1 };
            packed.insert(packed.end(), 28, 0);
            packed.insert(packed.end(), 24, 0);
            packed << uint8_vector::from_hex("03000000000000000001");
            tables.bytes(packed);
        } else tables.bytes(snapshot::pack_output(out));
        file::write(dir / "tables", tables.cbor());
        snapshot::write_meta(dir);
    }
}

suite node_snapshot_suite = [] {
    "cardano::ledger::node_snapshot"_test = [] {
        "packed input uses a big-endian index"_test = [] {
            tx_out_ref ref { {}, 256 };
            const auto packed = snapshot::pack_input(ref);
            expect_equal(packed.size(), 34);
            expect_equal(packed[32], 1);
            expect_equal(packed[33], 0);
            expect(snapshot::unpack_input(packed) == ref);
            expect(throws([&] { snapshot::unpack_input(buffer { packed }.subbuf(0, 33)); }));
        };
        "independent MemPack layouts"_test = [] {
            // Constructor 0, one-byte address, coin tag 0, big-endian varint 128.
            const auto plain = uint8_vector::from_hex("000160008100");
            const auto output = snapshot::unpack_output(plain, 2);
            expect_equal(output.address_raw, uint8_vector::from_hex("60"));
            expect_equal(output.coin, 128);
            expect_equal(snapshot::pack_output(output), plain);

            uint8_vector optimized = uint8_vector::from_hex("0201");
            optimized.insert(optimized.end(), 28, 0x22);
            optimized << uint8_vector::from_hex("0807060504030201100F0E0D0C0B0A091817161514131211030000001C1B1A19008100");
            const auto decoded = snapshot::unpack_output(optimized, 5);
            uint8_vector address = uint8_vector::from_hex("010102030405060708090A0B0C0D0E0F101112131415161718191A1B1C");
            address.insert(address.end(), 28, 0x22);
            expect_equal(decoded.address_raw, address);
            expect_equal(decoded.coin, 128);
            expect_equal(snapshot::unpack_output(snapshot::pack_output(decoded), 5).address_raw, address);
            optimized[0] = 3; // Optimized datum hash uses four little-endian words.
            optimized << uint8_vector::from_hex("0807060504030201100F0E0D0C0B0A091817161514131211201F1E1D1C1B1A19");
            const auto with_hash = snapshot::unpack_output(optimized, 5);
            expect(with_hash.datum == std::optional<datum_option_t> { datum_option_t { datum_hash::from_hex(
                "0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20") } });
            expect(snapshot::compare_outputs(optimized, snapshot::pack_output(with_hash), 5).empty());
        };
        "packed Byron addresses retain the wrapper and checksum"_test = [] {
            // Address from the epoch-208 reference, including its independent CRC.
            const auto payload = uint8_vector::from_hex(
                "83581C2C9A3D50AC9FB7C559167A6F318490399850B76CDF71386518E59BEF"
                "A101581E581C69DFC49CFC4FC6447B2DB97D88852A6EA598224A52A32188EB72179700");
            const auto wrapped = uint8_vector::from_hex(
                "82D818584283581C2C9A3D50AC9FB7C559167A6F318490399850B76CDF71386518E59BEF"
                "A101581E581C69DFC49CFC4FC6447B2DB97D88852A6EA598224A52A32188EB721797001AFCF170A5");
            // Constructor 0, 76-byte address, coin constructor 0, coin 1.
            auto expected = uint8_vector::from_hex("004C");
            expected << wrapped << uint8_vector::from_hex("0001");
            tx_out_data out { .address_raw=payload, .coin=1 };
            expect_equal(snapshot::pack_output(out), expected);
            out.address_raw = wrapped;
            expect_equal(snapshot::pack_output(out), expected); // Do not double-wrap.
            for (uint64_t era = 2; era <= 7; ++era) {
                expect_equal(snapshot::unpack_output(expected, era).address_raw, wrapped);
                out.address_raw = payload;
                expect(snapshot::compare_outputs(expected, snapshot::pack_output(out), era).empty());
                out.address_raw = wrapped;
                out.address_raw.back() ^= 1;
                const auto diff = snapshot::compare_outputs(expected, snapshot::pack_output(out), era);
                expect(!diff.empty()); // Comparison still checks the full address, including CRC.
            }
        };
        "packed addresses discard historical trailing bytes"_test = [] {
            // Base address and ignored suffix from the epoch-288 disagreement.
            const auto base = uint8_vector::from_hex(
                "015BAD085057AC10ECC7060F7AC41EDD6F63068D8963EF7D86CA58669E5E"
                "CF2D283418A60BE5A848A2380EB721000DA1E0BBF39733134BECA4");
            const auto suffix = uint8_vector::from_hex("CB57AFB0B35FC89C63061C9914E055001A518C7516");
            for (uint8_t type = 0; type < 8; ++type) {
                auto canonical = base;
                canonical[0] = (type << 4) | 1;
                if (type >= 4) canonical.resize(29);
                if (type == 4 || type == 5) canonical << uint8_vector::from_hex("0C0C0C");
                auto raw = canonical;
                raw << suffix;
                const tx_out_data output { .address_raw=raw, .coin=1 };
                // Independent MemPack: constructor, length, address, coin tag/value.
                auto expected = uint8_vector::from_hex("00");
                expected << static_cast<uint8_t>(canonical.size()) << canonical
                    << uint8_vector::from_hex("0001");
                const auto packed = snapshot::pack_output(output);
                expect_equal(packed, expected);
                expect_equal(snapshot::unpack_output(packed, 4).address_raw, canonical);
                expect_equal(output.address_raw, raw);
                auto changed = output;
                changed.address_raw[1] ^= 1;
                expect(!snapshot::compare_outputs(expected, snapshot::pack_output(changed), 4).empty());
            }
        };
        "packed pointers use Node 11 bounds"_test = [] {
            uint8_vector prefix(29, 0);
            prefix[0] = 0x41;
            auto zero = prefix;
            zero << uint8_vector::from_hex("000000");
            for (const auto suffix: {
                    "BCD9819561BDDC6614", // Epoch 258: (16292793057,1011302,20).
                    "90808080000000",     // Slot exceeds Word32.
                    "0C84800000",         // Transaction index exceeds Word16.
                    "0C00848000" }) {     // Certificate index exceeds Word16.
                auto raw = prefix;
                raw << uint8_vector::from_hex(suffix);
                tx_out_data output { .address_raw=raw, .coin=50'000'000 };
                auto expected = output;
                expected.address_raw = zero;
                expect_equal(snapshot::pack_output(output), snapshot::pack_output(expected));
                expect_equal(output.address_raw, raw); // Keep the cached output unchanged.
            }
            for (const auto suffix: { "000000", "0C0C0C", "8FFFFFFF7F83FF7F83FF7F" }) {
                auto raw = prefix;
                raw << uint8_vector::from_hex(suffix);
                const auto packed = snapshot::pack_output({ .address_raw=raw, .coin=1 });
                expect_equal(snapshot::unpack_output(packed, 4).address_raw, raw);
            }
            // Normalization applies only when exporting our output, never to the
            // reference during comparison: a disagreeing pointer stays visible.
            auto different = zero;
            different.back() = 1;
            expect(!snapshot::compare_outputs(
                snapshot::pack_output({ .address_raw=zero, .coin=1 }),
                snapshot::pack_output({ .address_raw=different, .coin=1 }), 4).empty());
        };
        "multiassets, datum and reference scripts"_test = [] {
            tx_out_data out { .address_raw=uint8_vector::from_hex("60"), .coin=1234567 };
            script_hash policy {};
            out.assets[policy][asset_name_t {}] = 3;
            out.assets[policy][asset_name_t { uint8_vector::from_hex("6162") }] = 7;
            policy[0] = 1;
            out.assets[policy][asset_name_t { uint8_vector::from_hex("6162") }] = 9;
            // Independent layout: quantities, policy offsets, name offsets,
            // policies, then descending distinct names (including empty).
            auto compact = out;
            compact.coin = 0;
            auto expected = uint8_vector::from_hex(
                "0001600100035E0700000000000000090000000000000003000000000000002400400024005C005C005E00");
            expected.insert(expected.end(), 28, 0);
            expected << policy << uint8_vector::from_hex("6162");
            expect_equal(snapshot::pack_output(compact), expected);
            for (const auto &datum: { datum_option_t { datum_hash {} }, datum_option_t { uint8_vector::from_hex("820102") } }) {
                out.datum = datum;
                for (const auto type: { script_type::native, script_type::plutus_v1, script_type::plutus_v2, script_type::plutus_v3 }) {
                    out.script_ref.emplace(type, uint8_vector::from_hex("8100"));
                    const auto decoded = snapshot::unpack_output(snapshot::pack_output(out), 7);
                    expect_equal(decoded.coin, out.coin);
                    expect(decoded.assets == out.assets);
                    expect(decoded.datum->val == out.datum->val);
                    expect(decoded.script_ref == out.script_ref);
                }
            }
        };
        "inline BinaryData and reference-script Datum have distinct layouts"_test = [] {
            // Constructor 4: address, value, then length-prefixed CBOR, with no datum tag.
            const auto direct = uint8_vector::from_hex("0401600001058282010203");
            tx_out_data out { .address_raw=uint8_vector::from_hex("60"), .coin=1,
                .datum=datum_option_t { uint8_vector::from_hex("8282010203") } };
            expect_equal(snapshot::pack_output(out), direct);
            for (const uint64_t era: { 6, 7 }) {
                expect(snapshot::unpack_output(direct, era).datum->val == out.datum->val);
                // Constructor 5 keeps the Datum tag 2, followed by a native script.
                const auto with_script = uint8_vector::from_hex("05016000010205828201020300028100");
                auto scripted = out;
                scripted.script_ref.emplace(script_type::native, uint8_vector::from_hex("8100"));
                expect_equal(snapshot::pack_output(scripted), with_script);
                const auto decoded = snapshot::unpack_output(with_script, era);
                expect(decoded.datum->val == out.datum->val);
                expect(decoded.script_ref == scripted.script_ref);
                // The correction must preserve the first nested disagreement.
                const auto changed = uint8_vector::from_hex("0401600001058282010405");
                const auto diff = snapshot::compare_outputs(direct, changed, era);
                expect_equal(diff.size(), 1);
                if (!diff.empty())
                    expect(diff.front().to_string().find("0.3.1.0.1:") != std::string::npos);
                for (const auto malformed: { "0401600001", "04016000010582820102",
                        "04016000010182", "0401600001020001" })
                    expect(throws([&] { snapshot::unpack_output(uint8_vector::from_hex(malformed), era); }));
            }
        };
        "malformed packed values fail closed"_test = [] {
            for (const auto hex: { "", "06", "0001600080", "000160000000", "00056000",
                    "000160008280808080808080808000", "00016001010100" }) {
                expect(throws([&] { snapshot::unpack_output(uint8_vector::from_hex(hex), 7); })) << hex;
            }
            expect(throws([] { snapshot::unpack_output(uint8_vector::from_hex("0101600000"), 2); }));
        };
        "discovery sorts slots and allows empty epochs"_test = [] {
            file::tmp_directory tmp { "node-snapshot-discovery" };
            const std::filesystem::path root { tmp.path() };
            tx_out_data out;
            write_snapshot(root / "30_epoch-3", out);
            write_snapshot(root / "10_epoch-1", out);
            file::write(root / "epochs.json", buffer { std::string_view { "{}" } });
            const auto refs = snapshot::discover(root, 0, 10);
            expect_equal(refs.size(), 2);
            expect_equal(refs[0].slot, 10);
            expect_equal(refs[1].epoch, 3);
            expect_equal(snapshot::discover(root, 3, 3).size(), 1);
            expect(throws([&] { snapshot::discover(root, 2, 2); }));
            expect(throws([&] { snapshot::discover(root, 3, 1); }));
            std::filesystem::remove(root / "30_epoch-3" / "tables");
            expect(throws([&] { snapshot::discover(root, 0, 10); }));
        };
        "state envelope and checksum"_test = [] {
            expect_equal(crypto::crc32::digest(buffer { std::string_view { "123456789" } }), 0xcbf43926U);
            block_hash hash {};
            hash[0] = 0xab;
            hash[31] = 0xcd;
            for (uint64_t era = 2; era <= 7; ++era) {
                const auto info = snapshot::inspect(test_state(12345, 47, era, hash, 987));
                expect_equal(info.tip.slot, 12345);
                expect_equal(info.tip.height, 987);
                expect_equal(info.tip.hash, hash);
                expect_equal(info.epoch, 47);
                expect_equal(info.era, era);
            }
            auto invalid = test_state();
            invalid << uint8_t { 0 };
            expect(throws([&] { snapshot::inspect(invalid); }));
            expect(throws([] { snapshot::inspect(test_state(42, 0, 1)); }));
            expect(throws([] { snapshot::inspect(test_state(42, 0, 8)); }));
            file::tmp_directory tmp { "node-snapshot-checksum" };
            const std::filesystem::path root { tmp.path() };
            write_snapshot(root, {});
            expect(nothrow([&] { snapshot::verify(root); }));
            file::write(root / "tables", uint8_vector::from_hex("81A0"));
            expect(throws([&] { snapshot::verify(root); }));
            snapshot::write_meta(root);
            auto meta = json::load((root / "meta").string());
            meta.as_object()["tablesCodecVersion"] = 2;
            file::write(root / "meta", buffer { json::serialize(meta) });
            expect(throws([&] { snapshot::verify(root); }));
        };
        "optimized and general outputs compare by fields"_test = [] {
            file::tmp_directory tmp { "node-snapshot-optimized" };
            const auto a = std::filesystem::path { tmp.path() } / "expected";
            const auto b = std::filesystem::path { tmp.path() } / "actual";
            tx_out_data out { .address_raw=uint8_vector(57), .coin=1 };
            out.address_raw[0] = 1;
            write_snapshot(a, out, true);
            write_snapshot(b, out);
            expect(nothrow([&] { snapshot::compare(a, b); }));
        };
        "diagnostics reach the first nested datum difference"_test = [] {
            file::tmp_directory tmp { "node-snapshot-diagnostics" };
            const auto a = std::filesystem::path { tmp.path() } / "expected";
            const auto b = std::filesystem::path { tmp.path() } / "actual";
            tx_out_data out { .address_raw=uint8_vector::from_hex("60"), .coin=1,
                .datum=datum_option_t { uint8_vector::from_hex("8282010203") } };
            write_snapshot(a, out);
            write_snapshot(b, out);
            expect(nothrow([&] { snapshot::compare(a, b); }));
            out.datum = datum_option_t { uint8_vector::from_hex("8282010405") };
            write_snapshot(b, out);
            try {
                snapshot::compare(a, b);
                expect(false) << "expected a nested datum mismatch";
            } catch (const std::exception &ex) {
                const std::string message = ex.what();
                expect(message.find("tables[0]") != std::string::npos);
                expect(message.find("#256") != std::string::npos);
                expect(message.find("0.3.1.0.1:") != std::string::npos) << message;
            }
        };
        "streaming comparison and checksums cross buffer boundaries"_test = [] {
            file::tmp_directory tmp { "node-snapshot-streaming" };
            const auto a = std::filesystem::path { tmp.path() } / "expected";
            const auto b = std::filesystem::path { tmp.path() } / "actual";
            tx_out_data out { .address_raw=uint8_vector::from_hex("60"), .coin=1 };
            // A record larger than the read buffer also crosses length headers.
            out.script_ref.emplace(script_type::plutus_v2, uint8_vector(1024 * 1024 + 17));
            write_snapshot(a, out);
            txo_map outputs;
            outputs.try_emplace(tx_out_ref { {}, 256 }, out);
            const auto state = test_state(42, 0, 6);
            const snapshot::reader reference { a };
            expect(nothrow([&] { reference.compare(state, outputs); }));
            std::filesystem::create_directory(b);
            file::write(b / "state", state);
            const auto crc = snapshot::write_tables(b / "tables", outputs);
            snapshot::write_meta(b, crypto::crc32::digest(state), crc);
            expect(nothrow([&] { snapshot::verify(b); }));
            // Compare a definite reference map with the indefinite writer map.
            expect(nothrow([&] { snapshot::compare(a, b); }));
            auto meta = json::load((a / "meta").string());
            meta.as_object()["checksum"] = meta.as_object().at("checksum").to_number<uint32_t>() ^ 1U;
            file::write(a / "meta", buffer { json::serialize(meta) });
            expect(throws([&] { snapshot::reader { a }.compare(state, outputs); }));
            // Preparing a reference owns its state and metadata; no rereads.
            std::filesystem::remove(a / "state");
            expect(nothrow([&] { reference.compare(state, outputs); }));
        };
        "state disagreement precedes table access"_test = [] {
            file::tmp_directory tmp { "node-snapshot-state-first" };
            const std::filesystem::path dir { tmp.path() };
            write_snapshot(dir, {});
            const snapshot::reader reference { dir };
            std::filesystem::remove(dir / "tables");
            try {
                reference.compare(test_state(43, 0, 6), txo_map {});
                expect(false) << "expected a state mismatch";
            } catch (const std::exception &ex) {
                expect(std::string_view { ex.what() }.starts_with("state ")) << ex.what();
            }
        };
        "streaming tables reject missing, extra, unordered and malformed entries"_test = [] {
            file::tmp_directory tmp { "node-snapshot-table-errors" };
            const std::filesystem::path dir { tmp.path() };
            const tx_out_ref key { {}, 256 };
            tx_out_data out { .address_raw=uint8_vector::from_hex("60"), .coin=1 };
            txo_map outputs;
            outputs.try_emplace(key, out);
            write_snapshot(dir, out);
            const auto state = test_state(42, 0, 6);
            expect(throws([&] { snapshot::reader { dir }.compare(state, txo_map {}); }));
            outputs.try_emplace(tx_out_ref { {}, 257 }, out);
            expect(throws([&] { snapshot::reader { dir }.compare(state, outputs); }));
            outputs.erase(tx_out_ref { {}, 257 });
            const auto check = [&](const buffer bytes) {
                file::write(dir / "tables", bytes);
                snapshot::write_meta(dir); // Fail on structure, not a stale CRC.
                expect(throws([&] { snapshot::reader { dir }.compare(state, outputs); }));
            };
            for (const auto hex: { "81A0", "81A0FF", "81BF", "81A15822", "81A15BFFFFFFFFFFFFFFFF" })
                check(uint8_vector::from_hex(hex));
            cbor::encoder tables;
            tables.array(1).map(2);
            for (size_t i = 0; i < 2; ++i) tables.bytes(snapshot::pack_input(key)).bytes(snapshot::pack_output(out));
            check(tables.cbor());
            tables.cbor().clear();
            tables.array(1).map(1).bytes(snapshot::pack_input(key)).bytes(snapshot::pack_output(out));
            tables.cbor().pop_back();
            check(tables.cbor());
        };
    };
};
