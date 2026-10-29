/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/state.hpp>
#include <turbo/cardano/ledger/node-snapshot.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace cardano::ledger;

    void update_params(state &st, const uint64_t slot, const cardano::param_update &update,
        const cardano::config &cfg=cardano::config::get())
    {
        const cardano::slot slot_obj { slot, cfg };
        for (const auto &[deleg_id, meta]: cfg.shelley_delegates) {
            st.propose_update(slot, { .key_id=deleg_id, .update=update, .epoch=slot_obj.epoch() });
        }
    }

    struct node_layout_fixture {
        state st;
        cardano::pool_hash pool_id {};
        cardano::pool_params pool {};
        cardano::stake_ident delegated {}, undelegated {};

        node_layout_fixture(const cardano::config &cfg, const uint64_t major)
            : st { cfg, scheduler::get(), state::init_mode::empty }
        {
            for (const auto &[key, delegate]: cfg.shelley_delegates)
                st.propose_update(1, { .key_id=key,
                    .update={ .protocol_ver=cardano::protocol_version { major, 0 } }, .epoch=0 });
            st.start_epoch(0);
            pool.reward_id.at(0) = 0xE0 | cfg.shelley_network_id;
            pool.owners.insert(cardano::stake_keyhash_t {});
            if (major == 2 || major == 9) {
                pool.metadata.emplace();
                pool.metadata->url = "https://example.com/pool.json";
                pool.metadata->hash.resize(32, 0x42);
            }
            st.register_pool({ pool_id, pool });
            undelegated.hash.at(0) = 1;
            st.process_cert(cardano::cert_t { cardano::stake_reg_cert { delegated } }, { 1, 2, 3 });
            st.process_cert(cardano::cert_t { cardano::stake_reg_cert { undelegated } }, { 1, 2, 4 });
            st.process_cert(cardano::cert_t { cardano::stake_deleg_cert { delegated, pool_id } }, { 1, 2, 5 });
            if (major >= 9)
                st.process_cert(cardano::cert_t { cardano::vote_deleg_cert { delegated, {} } }, { 1, 2, 6 });
        }
    };

    cbor::zero2::value &node_new_epoch_state(cbor::zero2::value &root, const size_t era_count)
    {
        // ExtLedgerState -> LedgerState -> current era -> versioned NewEpochState.
        return root.at(1).at(0).at(era_count - 1).at(1).at(1).at(1);
    }

    cbor::zero2::value &node_header_state(cbor::zero2::value &root, const size_t era_count)
    {
        // HeaderState contains [tip, era states]; descend into the states first.
        auto &eras = root.at(1).at(1).at(1);
        return eras.at(era_count - 1).at(1).at(1);
    }

    void check_node_instant_stake(const buffer encoded, const state &st,
        const cardano::stake_ident &id, const uint64_t stake)
    {
        const bool conway = st.params().protocol_ver.major >= 9;
        auto parsed = cbor::zero2::parse(encoded);
        auto &utxo = node_new_epoch_state(parsed.get(), st.eras().size()).at(3).at(1).at(1).array();
        cbor::encoder expected;
        if (!conway)
            expected.array(2);
        expected.map(stake ? 1 : 0);
        if (stake)
            expected.array(2).uint(0).bytes(id.hash).uint(stake);
        if (!conway)
            expected.map(0);
        expect_equal(utxo.skip(4).read().data_raw(), buffer { expected.cbor() });
        expect_equal(utxo.read().uint(), 0); // Donations follow InstantStake directly.
        expect(utxo.done());
    }
}

suite cardano_ledger_state_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "cardano::ledger::state"_test = [] {
        "empty"_test = [] {
            state st {};
            expect(st.epoch() == 0_ull);
            expect(st.end_offset() == 0_ull);
            expect(st.pool_stake_dist().empty());
            expect(!st.utxos().empty());

            state snapshot_target {
                cardano::config::get(), scheduler::get(), state::init_mode::empty
            };
            expect(snapshot_target.utxos().empty());
            snapshot_target.clear(state::init_mode::empty);
            expect(snapshot_target.utxos().empty());
            snapshot_target.clear();
            expect(snapshot_target == st);
        };
        "finish_epoch"_test = [] {
            cardano::config cfg {};
            state st { cfg };
            expect_equal(st.params().protocol_ver.major, 0);
            update_params(st, 1, { .protocol_ver=cardano::protocol_version { 2, 0 } }, cfg);
            expect_equal(st.treasury(), 0);
            expect_equal(st.reserves(), 0);
            st.start_epoch();
            st.process_block(20000, 2, 0, 0);
            cfg.shelley_start_epoch(0);
            expect_equal(st.treasury(), 0);
            expect_equal(st.reserves(), 13887515255000000ULL);
            expect_equal(st.params().protocol_ver.major, 2);
            expect_equal(st.treasury(), 0);
            st.process_block(40000, 2, 400000, 0);
            st.start_epoch();
            expect_equal(st.treasury(), 8332509153000ULL);
            st.start_epoch();
            expect_equal(st.epoch(), 2);
        };
        "clear"_test = [] {
            file::tmp tmp_state { "validator-state-test" };
            state st {};
            update_params(st, cardano::slot { 1, cardano::config::get() }, { .protocol_ver=cardano::protocol_version { 8, 0 } });
            st.start_epoch();
            st.reserves(10'000'000'000'000'000ULL);
            st.start_epoch();
            st.clear();
            state st2 {};
            expect(st == st2);
        };
        "snapshot reload preserves pending protocol upgrade"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            state original { cfg, scheduler::get(), state::init_mode::empty };
            for (const auto &[key, delegate]: cfg.shelley_delegates)
                original.propose_update(1, { .key_id=key,
                    .update={ .protocol_ver=cardano::protocol_version { 8, 0 } }, .epoch=0 });
            const file::tmp snapshot_file { "ledger-pending-upgrade" };
            original.save_zpp(snapshot_file.path());
            state reloaded { cfg, scheduler::get(), state::init_mode::empty };
            reloaded.load_zpp(snapshot_file.path());
            expect(original == reloaded);
            original.start_epoch(0);
            reloaded.start_epoch(0);
            expect_equal(original.params().protocol_ver.major, 8);
            expect_equal(reloaded.params().protocol_ver.major, 8);
            expect_equal(original.reserves(), cfg.shelley_max_lovelace_supply);
            expect_equal(reloaded.reserves(), original.reserves());
            expect(original == reloaded);
        };
        "save and load"_test = [] {
            file::tmp tmp_state { "validator-state-test" };
            state st {};
            update_params(st, 1, { .protocol_ver=cardano::protocol_version { 8, 0 } });
            st.start_epoch();
            st.reserves(10'000'000'000'000'000ULL);
            st.start_epoch();
            st.save_zpp(tmp_state.path());
            state st2 {
                cardano::config::get(), scheduler::get(), state::init_mode::empty
            };
            expect(st2.utxos().empty());
            st2.load_zpp(tmp_state.path());
            expect(st == st2);
        };
        "snapshot completion preserves the chain point and epoch result"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            for (const uint64_t major: { 2, 8, 9, 10, 11 }) {
                for (const auto epoch_slot: { cfg.shelley_randomness_stabilization_window - 1,
                        cfg.shelley_randomness_stabilization_window,
                        cfg.shelley_randomness_stabilization_window + 1,
                        cfg.shelley_rewards_ready_slot }) {
                    state saved { cfg, scheduler::get(), state::init_mode::empty };
                    state uninterrupted { cfg, scheduler::get(), state::init_mode::empty };
                    for (auto *st: { &saved, &uninterrupted }) {
                        for (const auto &[key, delegate]: cfg.shelley_delegates)
                            st->propose_update(1, { .key_id=key,
                                .update={ .protocol_ver=cardano::protocol_version { major, 0 } }, .epoch=0 });
                        st->start_epoch(0);
                        st->process_block(0, st->params().protocol_ver.era(), epoch_slot, 0);
                    }
                    const auto treasury = saved.treasury();
                    const auto reserves = saved.reserves();
                    const auto tip = saved.last_slot();
                    const file::tmp snapshot_file { "ledger-completed-pulsers" };
                    saved.save_zpp(snapshot_file.path());
                    expect(saved.exportable());
                    expect_equal(saved.last_slot(), tip);
                    expect_equal(saved.epoch(), 0);
                    expect_equal(saved.end_offset(), 0);
                    expect_equal(saved.treasury(), treasury);
                    expect_equal(saved.reserves(), reserves);
                    const auto first_save = file::read(snapshot_file.path());
                    saved.save_zpp(snapshot_file.path());
                    expect_equal(file::read(snapshot_file.path()), first_save);

                    state restored { cfg, scheduler::get(), state::init_mode::empty };
                    restored.load_zpp(snapshot_file.path());
                    expect(saved == restored);
                    const auto node_tip = cardano::point { {}, tip, 0, 0 };
                    const auto saved_cbor = saved.to_cbor(node_tip).flat();
                    expect_equal(restored.to_cbor(node_tip).flat(), saved_cbor);
                    const file::tmp node_file { "ledger-completed-pulsers.cbor" };
                    restored.save_node(node_file.path(), node_tip);
                    expect_equal(file::read(node_file.path()), saved_cbor);

                    for (auto *st: { &saved, &restored, &uninterrupted })
                        st->start_epoch(1);
                    expect(saved == uninterrupted);
                    expect(restored == uninterrupted);
                    expect_equal(saved.to_cbor(cardano::point {}).flat(), uninterrupted.to_cbor(cardano::point {}).flat());
                }
            }
        };
        "save_node and load_node"_test = [] {
            file::tmp tmp_state { "validator-state-node-test" };
            state st {};
            update_params(st, 1, { .protocol_ver=cardano::protocol_version { 8, 0 } });
            st.start_epoch();
            st.reserves(10'000'000'000'000'000ULL);
            st.start_epoch();
            st.save_node(tmp_state.path(), cardano::point {});
            state st2 {};
            st2.load_node(tmp_state.path());
            expect(st == st2);
        };
        "Node 11.1.3 separates the UTxO without changing legacy export"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            for (const uint64_t major: { 2, 3, 4, 5, 7, 9 }) {
                state st { cfg, scheduler::get(), state::init_mode::empty };
                for (const auto &[key, delegate]: cfg.shelley_delegates)
                    st.propose_update(1, { .key_id=key,
                        .update={ .protocol_ver=cardano::protocol_version { major, 0 } }, .epoch=0 });
                st.start_epoch(0);
                st.process_block(0, st.params().protocol_ver.era(), 1, 0);
                utxo_update_list updates(1);
                updates[0].try_emplace(cardano::tx_out_ref { {}, 0 }, cardano::tx_out_data {
                    .address_raw=uint8_vector::from_hex("6000000000000000000000000000000000000000000000000000000000"),
                    .coin=123
                });
                st.process_utxo_updates(std::move(updates));
                const cardano::point tip { {}, 1, 1, 0 };
                const auto legacy = st.to_cbor(tip).flat();
                const file::tmp_directory tmp { "ledger-node-11-split" };
                const auto dir = std::filesystem::path { tmp.path() } / "snapshot";
                st.save_node(dir.string(), tip, 1000, node_format::v11_1_3);
                const auto bytes = file::read((dir / "state").string());
                const auto check = [&](const char *operation, const auto &fn) {
                    try {
                        fn();
                    } catch (const std::exception &ex) {
                        // Keep the small fixture outside the temporary directory
                        // so its destructor does not erase the evidence.
                        const auto diagnostic = fmt::format("{}-pv{}-state.cbor", tmp.path(), major);
                        file::write(diagnostic, bytes);
                        throw error{"Node 11.1.3 protocol {} {}: {}; state saved at {}",
                            major, operation, ex.what(), diagnostic};
                    }
                };
                check("checksum verification", [&] { node_snapshot::verify(dir); });
                check("state inspection", [&] { expect_equal(node_snapshot::inspect(bytes).tip.slot, 1); });
                check("file comparison", [&] { node_snapshot::compare(dir, dir); });
                check("streaming comparison", [&] {
                    node_snapshot::reader { dir }.compare(st.to_cbor(tip, 1000, node_format::v11_1_3).flat(), st.utxos());
                });
                auto parsed = cbor::zero2::parse(bytes);
                auto &nes = node_new_epoch_state(parsed.get(), st.eras().size());
                expect(nes.at(3).at(1).at(1).at(0).map().done());
                const auto tables = file::read((dir / "tables").string());
                auto table = cbor::zero2::parse(tables);
                auto &map = table.get().at(0).map();
                expect(!map.done());
                auto &key = map.read_key();
                expect_equal(node_snapshot::unpack_input(key.bytes()).idx, 0);
                const auto output = node_snapshot::unpack_output(map.read_val(std::move(key)).bytes(), st.eras().size());
                expect_equal(output.coin, 123);
                expect(map.done());
                expect_equal(st.to_cbor(tip).flat(), legacy);
                expect_equal(st.utxos().size(), 1);
            }
        };
        "Node 11 pointer balances normalize and merge without changing cached state"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            for (const uint64_t major: { 2, 4, 7, 9 }) {
                state st { cfg, scheduler::get(), state::init_mode::empty };
                for (const auto &[key, delegate]: cfg.shelley_delegates)
                    st.propose_update(1, { .key_id=key,
                        .update={ .protocol_ver=cardano::protocol_version { major, 0 } }, .epoch=0 });
                st.start_epoch(0);
                utxo_update_list updates(1);
                size_t idx = 0;
                // Existing zero pointer, observed epoch-258 overflow, individual
                // bounds, valid dangling pointer, and the largest valid pointer.
                for (const auto suffix: { "000000", "BCD9819561BDDC6614", "90808080000000",
                        "0C84800000", "0C00848000", "0C0C0C", "8FFFFFFF7F83FF7F83FF7F" }) {
                    uint8_vector raw(29, 0);
                    raw[0] = 0x41;
                    raw << uint8_vector::from_hex(suffix);
                    updates[0].try_emplace(cardano::tx_out_ref { {}, idx++ }, cardano::tx_out_data {
                        .address_raw=std::move(raw), .coin=1
                    });
                }
                st.process_utxo_updates(std::move(updates));
                const auto pointers_before = st.stake_pointers();
                const auto legacy = st.to_cbor(cardano::point {}).flat();
                const auto modern = st.to_cbor(cardano::point {}, 1000, node_format::v11_1_3).flat();
                auto parsed = cbor::zero2::parse(modern);
                auto &nes = node_new_epoch_state(parsed.get(), st.eras().size());
                auto &instant_stake = nes.at(3).at(1).at(1).at(4);
                auto &pointers = major >= 9 ? instant_stake : instant_stake.at(1);
                cbor::encoder expected;
                if (major >= 9) {
                    expected.map(0); // Conway has only a credential map; these outputs all use pointers.
                } else {
                    expected.map(3)
                        .array(3).uint(0).uint(0).uint(0).uint(5)
                        .array(3).uint(12).uint(12).uint(12).uint(1)
                        .array(3).uint(0xFFFFFFFFULL).uint(0xFFFF).uint(0xFFFF).uint(1);
                }
                expect(cbor::compare(expected.cbor(), pointers.data_raw()).empty());
                expect(st.stake_pointers() == pointers_before);
                expect_equal(st.to_cbor(cardano::point {}).flat(), legacy);
            }
        };
        "Node 11 state layout follows the node version"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            for (const uint64_t major: { 2, 3, 4, 5, 7, 9, 10 }) {
                node_layout_fixture fixture { cfg, major };
                auto &[st, pool_id, pool, delegated, undelegated] = fixture;
                const bool conway = major >= 9;

                const auto legacy = st.to_cbor({}).flat();
                const auto bytes = st.to_cbor({}, 1000, node_format::v11_1_3).flat();
                check_node_instant_stake(bytes, st, delegated, 0);
                auto parsed = cbor::zero2::parse(bytes);
                auto &cert = node_new_epoch_state(parsed.get(), st.eras().size()).at(3).at(1).at(0);
                expect_equal(cert.special_uint(), conway ? 3 : 2);
                auto &fields = cert.array();
                if (conway)
                    fields.skip(1); // VState exists only in Conway.
                auto &pools = fields.read().array();
                expect(pools.read().map().done()); // VRF counts stay empty before protocol 11.
                auto &active = pools.read().map();
                auto &pool_key = active.read_key();
                expect_equal(pool_key.bytes(), buffer { pool_id });
                auto &pool_state = active.read_val(std::move(pool_key));
                expect_equal(pool_state.special_uint(), 10);
                auto &pool_fields = pool_state.array();
                auto &metadata = pool_fields.skip(7).read().array();
                if (pool.metadata) {
                    auto &details = metadata.read().array();
                    expect_equal(details.read().text(), pool.metadata->url);
                    expect_equal(details.read().bytes(), buffer { pool.metadata->hash });
                    expect(details.done());
                }
                expect(metadata.done());
                expect_equal(pool_fields.read().uint(), st.params().pool_deposit);
                const auto delegators = cardano::set_t<cardano::stake_ident>::from_cbor(pool_fields.read());
                expect_equal(delegators.size(), 1);
                expect(delegators.contains(delegated));
                expect(active.done());
                expect(pools.read().map().done()); // Future parameters.
                expect(pools.read().map().done()); // Retirements.
                expect(pools.done());

                auto &accounts = fields.read().at(0);
                auto &registrations = (conway ? accounts : accounts.at(0)).map();
                for (const bool has_delegation: { true, false }) {
                    auto &key = registrations.read_key();
                    expect(cardano::stake_ident::from_cbor(key) == (has_delegation ? delegated : undelegated));
                    auto &account = registrations.read_val(std::move(key));
                    // Independent wire expectation: no legacy optional-array wrappers.
                    cbor::encoder expected;
                    expected.array(4);
                    if (!conway)
                        expected.array(3).uint(1).uint(2).uint(has_delegation ? 3 : 4);
                    expected.uint(0).uint(st.params().key_deposit);
                    if (has_delegation)
                        expected.bytes(pool_id);
                    else
                        expected.s_null();
                    if (conway) {
                        if (has_delegation)
                            expected.array(1).uint(2); // Always abstain.
                        else
                            expected.s_null();
                    }
                    expect_equal(account.data_raw(), buffer { expected.cbor() });
                }
                expect(registrations.done());

                auto whole = cbor::zero2::parse(bytes);
                auto &nes = node_new_epoch_state(whole.get(), st.eras().size()).array();
                auto &epoch = nes.skip(3).read().array();
                auto &params = epoch.skip(1).read().at(1).at(3).at(conway ? 3 : 2);
                expect_equal(params.special_uint(), conway ? 31 : major >= 7 ? 22 : major >= 5 ? 24 : 17);
                auto &version = params.at(major >= 7 ? 12 : 14);
                expect_equal(version.special_uint(), 2);
                auto &version_fields = version.array();
                expect_equal(version_fields.read().uint(), major);
                expect_equal(version_fields.read().uint(), 0);
                auto &snapshots = epoch.read().array();
                const auto empty_snapshot = uint8_vector::from_hex("82A0A0");
                for (size_t i = 0; i < 3; ++i)
                    expect_equal(snapshots.read().data_raw(), buffer { empty_snapshot });
                expect_equal(snapshots.read().uint(), 0);
                expect_equal(nes.skip(1).read().at(1).uint(), 1); // Empty active stake is NonZero 1.
                auto &avvm = nes.read();
                if (major == 2)
                    expect(avvm.map().done());
                else
                    expect_equal(avvm.data_raw(), buffer { uint8_vector::from_hex("F6") });
                auto header_parsed = cbor::zero2::parse(bytes);
                auto &header = node_header_state(header_parsed.get(), st.eras().size());
                expect_equal(header.special_uint(), major >= 7 ? 8 : 2);
                auto &last_slot = header.at(0);
                // TPraos uses [slot]; Praos (Babbage onward) uses [1, slot].
                expect_equal(last_slot.special_uint(), major >= 7 ? 2 : 1);
                auto &slot_fields = last_slot.array();
                if (major >= 7) expect_equal(slot_fields.read().uint(), 1);
                expect_equal(slot_fields.read().uint(), 0);
                expect(slot_fields.done());
                expect_equal(st.to_cbor({}).flat(), legacy);
            }
        };
        "Node 11 funded snapshots retain zero-stake delegators and Shelley AVVM outputs"_test = [] {
            cardano::config cfg { cardano::config::get() };
            cfg.shelley_start_epoch(0);
            for (const uint64_t major: { 2, 9 }) {
                node_layout_fixture fixture { cfg, major };
                auto &[st, pool_id, pool, delegated, undelegated] = fixture;
                const bool conway = major >= 9;
                // Zero-stake delegators count for pools but have no ActiveStake entry.
                st.process_cert(cardano::cert_t { cardano::stake_deleg_cert { undelegated, pool_id } }, { 1, 2, 7 });
                uint8_vector address(57, 0);
                address[0] = cfg.shelley_network_id;
                utxo_update_list updates(1);
                updates[0].try_emplace(cardano::tx_out_ref { {}, 0 }, cardano::tx_out_data {
                    .address_raw=address, .coin=100
                });
                const auto redeem = cardano::byron_avvm_addr("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
                updates[0].try_emplace(cardano::tx_out_ref { {}, 1 }, cardano::tx_out_data {
                    .address_raw=redeem, .coin=55
                });
                st.process_utxo_updates(std::move(updates));
                st.start_epoch(1);
                const auto bytes = st.to_cbor({}, 1000, node_format::v11_1_3).flat();
                check_node_instant_stake(bytes, st, delegated, 100);
                auto parsed = cbor::zero2::parse(bytes);
                auto &nes = node_new_epoch_state(parsed.get(), st.eras().size()).array();
                auto &mark = nes.skip(3).read().at(2).at(0);
                cbor::encoder expected;
                expected.array(2).map(1).array(2).uint(0).bytes(delegated.hash);
                expected.array(2).uint(100).bytes(pool_id);
                expected.map(1).bytes(pool_id).array(10).uint(100);
                if (conway)
                    expected.tag(30);
                expected.array(2).uint(1).uint(1);
                if (conway)
                    expected.tag(258);
                expected.array(1).bytes(delegated.hash).uint(100);
                expected.bytes(pool.vrf_vkey).uint(pool.pledge).uint(pool.cost);
                expected.tag(30).array(2).uint(pool.margin.numerator).uint(pool.margin.denominator);
                expected.uint(2).array(2).uint(0).bytes(delegated.hash);
                expect_equal(mark.data_raw(), buffer { expected.cbor() });
                auto &stash = nes.skip(2).read();
                if (major == 2) {
                    auto &entries = stash.map();
                    auto &key = entries.read_key();
                    expect_equal(key.at(1).uint(), 1);
                    auto &output = entries.read_val(std::move(key)).array();
                    expect_equal(output.read().bytes(), buffer { redeem });
                    expect_equal(output.read().uint(), 55);
                    expect(entries.done());
                } else {
                    expect_equal(stash.data_raw(), buffer { uint8_vector::from_hex("F6") });
                }
            }
        };
        "save_node and load_node pv11"_test = [] {
            file::tmp tmp_state { "validator-state-node-pv11-test" };
            state st {};
            update_params(st, 1, { .protocol_ver=cardano::protocol_version { 11, 0 } });
            st.start_epoch();
            cardano::pool_params params {};
            params.vrf_vkey = cardano::vrf_vkey::from_hex(
                "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
            params.reward_id.at(0) = 0xE0 | cardano::config::get().shelley_network_id;
            const auto pool_id = cardano::pool_hash::from_hex(
                "000102030405060708090A0B0C0D0E0F101112131415161718191A1B");
            st.register_pool({ pool_id, params });
            params.cost = 1;
            st.register_pool({ pool_id, params });
            st.save_node(tmp_state.path(), cardano::point {});
            state st2 {};
            st2.load_node(tmp_state.path());
            expect(st == st2);
        };
        "register_pool"_test = [] {
            state st {};
            update_params(st, 1, { .protocol_ver=cardano::protocol_version { 8, 0 } });
            st.start_epoch();
            const auto pool_id = cardano::key_hash::from_hex("00000000000000000000000000000000000000000000000000000000");
            cardano::pool_params pp1 {};
            pp1.cost = 200'000'000;
            st.register_pool({ pool_id, pp1 });
            expect(st.pool_params().contains(pool_id));
            expect(st.pool_params().at(pool_id).params.cost == 200'000'000);
            expect(st.pool_params_future().empty());
            cardano::pool_params pp2 {};
            pp2.cost = 300'000'000;
            st.register_pool({ pool_id, pp2 });
            expect(!st.pool_params_future().empty());
            expect(st.pool_params().at(pool_id).params.cost == 200'000'000);
            expect(st.pool_params_future().at(pool_id).params.cost == 300'000'000);
            expect(st.pool_params_mark().empty());
            st.start_epoch();
            expect(st.pool_params().at(pool_id).params.cost == 300'000'000);
            expect(st.pool_params_mark().at(pool_id).params.cost == 200'000'000);
            expect(st.pool_params_future().empty());
        };
        "track_eras"_test = [] {
            state st {};
            expect(nothrow([&] { st.track_era(0, 22); }));
            expect(st.eras().empty());
            expect(nothrow([&] { st.track_era(1, 22); }));
            expect(throws([&] { st.track_era(1, 20); }));
            expect(nothrow([&] { st.track_era(1, 40); }));
            expect(nothrow([&] { st.track_era(2, 100); }));
            expect(throws([&] { st.track_era(2, 99); }));
            expect(throws([&] { st.track_era(3, 99); }));
            expect(nothrow([&] { st.track_era(3, 432101); }));
            expect(throws([&] { st.track_era(2, 102); }));
            const auto eras = st.eras();
            expect(eras.size() == 3) << eras.size();
            expect(eras.at(0) == 22) << eras.at(0);
            expect(eras.at(1) == 100) << eras.at(1);
            expect(eras.at(2) == 432100) << eras.at(2);
        };
    };
};
