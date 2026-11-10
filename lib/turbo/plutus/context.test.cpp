/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <limits>
#include <turbo/common/test.hpp>
#include <turbo/plutus/context.hpp>

using namespace turbo;
using namespace turbo::cardano;
using namespace turbo::plutus;

namespace {
    void check_proposal_data(const cbor::encoder &action, const cbor::encoder &expected)
    {
        const script_info script{script_type::plutus_v3, uint8_vector::from_hex("450100002499")};
        reward_id_t return_account{};
        return_account[0] = 0xE0;
        cbor::encoder body{}, witnesses{};
        body.map(5).uint(0).array(0).uint(1).array(0).uint(2).uint(0);
        body.uint(9).map(1).bytes(script.hash()).map(1).bytes(uint8_vector{}).uint(1);
        body.uint(20).array(1).array(4).uint(0).bytes(return_account);
        body << action;
        body.array(2).text("").bytes(block_hash{});
        witnesses.map(2).uint(5).map(1).array(2).uint(1).uint(0).array(2).uint(0).array(2).uint(1).uint(1);
        witnesses.uint(7).array(1).bytes(script.script());
        storage::block_info block{};
        block.era = 7;
        context ctx{std::move(body.cbor()), std::move(witnesses.cbor()), block};
        ctx.protocol_ver({10, 0});
        ctx.set_inputs({}, {});
        ctx.prepare();
        const auto prepared = ctx.prepare_script(ctx.redeemers().begin()->second);
        const auto script_context = prepared.expr.visit([](const auto &node) -> data {
            if constexpr (std::is_same_v<std::decay_t<decltype(node)>, apply>) {
                return node.arg.visit([](const auto &arg) -> data {
                    if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, turbo::plutus::constant>)
                        return std::get<data>(*arg);
                    throw error{"expected a context constant"};
                });
            }
            throw error{"expected a script application"};
        });
        const auto &tx_info = std::get<data_constr>(*script_context)->second.at(0);
        const auto &proposals = std::get<data_constr>(*tx_info)->second.at(13);
        const auto &proposal = std::get<data::list_type>(*proposals).at(0);
        const auto &actual = std::get<data_constr>(*proposal)->second.at(2);
        allocator alloc{};
        expect(actual == data::from_cbor(alloc, expected.cbor()));
    }
}

suite plutus_context_suite = [] {
    "plutus::context"_test = [] {
        "V3 parameter proposals preserve ledger data"_test = [] {
            struct parameter_case {
                uint64_t key;
                std::string_view wire;
                std::string_view data;
            };
            const parameter_case cases[] = {
                { 0, "00", "00" },
                { 1, "01", "01" },
                { 2, "02", "02" },
                { 3, "194010", "194010" },
                { 4, "04", "04" },
                { 5, "05", "05" },
                { 6, "06", "06" },
                { 7, "07", "07" },
                { 8, "08", "08" },
                { 9, "D81E820204", "820102" },
                { 10, "D81E820003", "820001" },
                { 11, "D81E820103", "820103" },
                { 16, "10", "10" },
                { 17, "11", "11" },
                { 18, "A103822001", "A103822001" },
                { 19, "82D81E820102D81E820103", "82820102820103" },
                { 20, "820102", "820102" },
                { 21, "820304", "820304" },
                { 22, "16", "16" },
                { 23, "17", "17" },
                { 24, "1818", "1818" },
                { 25, "85D81E820102D81E820103D81E820104D81E820105D81E820106", "85820102820103820104820105820106" },
                { 26, "8AD81E820102D81E820103D81E820104D81E820105D81E820106D81E820107D81E820108D81E820109D81E82010AD81E82010B",
                    "8A82010282010382010482010582010682010782010882010982010A82010B" },
                { 27, "181B", "181B" },
                { 28, "181C", "181C" },
                { 29, "181D", "181D" },
                { 30, "181E", "181E" },
                { 31, "181F", "181F" },
                { 32, "1820", "1820" },
                { 33, "D81E820205", "820205" }
            };
            for (const auto parameters: { std::span<const parameter_case>{},
                std::span<const parameter_case>{cases + 3, 1}, std::span<const parameter_case>{cases} }) {
                cbor::encoder update{}, expected{}, action{}, expected_action{};
                update.map(parameters.size());
                expected.map(parameters.size());
                for (const auto &[key, wire, encoded]: parameters) {
                    update.uint(key).cbor() << uint8_vector::from_hex(wire);
                    expected.uint(key).cbor() << uint8_vector::from_hex(encoded);
                }
                action.array(4).uint(0).s_null();
                action << update;
                action.s_null();
                expected_action.tag(121).array(3).tag(122).array(0);
                expected_action << expected;
                expected_action.tag(122).array(0);
                check_proposal_data(action, expected_action);
            }
        };
        "V3 constitutions wrap the optional policy"_test = [] {
            for (const bool has_policy: {false, true}) {
                cbor::encoder action{}, expected{};
                action.array(3).uint(5).s_null().array(2).array(2).text("constitution").bytes(block_hash{});
                expected.tag(126).array(2).tag(122).array(0).tag(121).array(1);
                if (has_policy) {
                    action.bytes(script_hash{});
                    expected.tag(121).array(1).bytes(script_hash{});
                } else {
                    action.s_null();
                    expected.tag(122).array(0);
                }
                check_proposal_data(action, expected);
            }
        };
        "validated purpose bindings survive ownership transfer and freeze with the context"_test = [] {
            storage::block_info block {};
            block.era = 7;
            script_hash policy {};
            policy[0] = 1;
            cbor::encoder body {}, witnesses {};
            body.map(4).uint(0).array(0).uint(1).array(0).uint(2).uint(0);
            body.uint(9).map(1).bytes(policy).map(1).bytes(uint8_vector {}).uint(1);
            witnesses.map(0);
            context ctx { std::move(body.cbor()), std::move(witnesses.cbor()), block };
            ctx.protocol_ver({ 11, 0 });
            ctx.set_inputs({}, {});
            const redeemer_id pointer { redeemer_tag::mint, 0 };
            expect(ctx.redeemer_script(pointer) == policy); // independent body resolution
            flat_map<redeemer_id, script_hash> bindings {};
            bindings.emplace(pointer, policy);
            ctx.set_script_purposes(std::move(bindings));
            bindings.clear();
            expect(ctx.redeemer_script(pointer) == policy);
            expect(throws([&] { ctx.redeemer_script({ redeemer_tag::mint, 1 }); }));
            ctx.prepare();
            expect(throws([&] { ctx.set_script_purposes({}); }));
        };
        "withdrawal redeemer indices follow ledger account order"_test = [] {
            const auto key0 = reward_id_t::from_hex<reward_id_t>("E0" + std::string(56, '0'));
            const auto script0 = reward_id_t::from_hex<reward_id_t>("F0" + std::string(56, 'F'));
            const auto script1 = reward_id_t::from_hex<reward_id_t>("F1" + std::string(56, '2'));
            storage::block_info block {};
            block.era = 7;
            cbor::encoder body {}, witnesses {};
            body.map(4).uint(0).array(0).uint(1).array(0).uint(2).uint(0);
            body.uint(5).map(3);
            for (const auto &account: { script1, key0, script0 })
                body.bytes(account).uint(1);
            witnesses.map(0);
            context ctx { std::move(body.cbor()), std::move(witnesses.cbor()), block };
            expect(ctx.redeemer_script({ redeemer_tag::reward, 0 }) == script_hash { script0.hash() });
            // The network-0 key occupies index 1, before the network-1 script.
            expect(ctx.redeemer_script({ redeemer_tag::reward, 2 }) == script_hash { script1.hash() });
        };
        "total redeemer budget"_test = [] {
            const context::redeemer_map redeemers {
                { redeemer_id { redeemer_tag::spend, 0 },
                    tx_redeemer { redeemer_tag::spend, 0, {}, ex_units { 4, 7 } } },
                { redeemer_id { redeemer_tag::mint, 0 },
                    tx_redeemer { redeemer_tag::mint, 0, {}, ex_units { 6, 13 } } }
            };
            expect_equal(ex_units { 10, 20 }, context::validate_redeemer_budgets(redeemers, { 10, 20 }));
            expect(throws([&] { context::validate_redeemer_budgets(redeemers, { 9, 20 }); }));
            expect(throws([&] { context::validate_redeemer_budgets(redeemers, { 10, 19 }); }));
        };
        "total redeemer budget cannot overflow"_test = [] {
            const auto max = std::numeric_limits<uint64_t>::max();
            const context::redeemer_map redeemers {
                { redeemer_id { redeemer_tag::spend, 0 },
                    tx_redeemer { redeemer_tag::spend, 0, {}, ex_units { max, max } } },
                { redeemer_id { redeemer_tag::mint, 0 },
                    tx_redeemer { redeemer_tag::mint, 0, {}, ex_units { 1, 1 } } }
            };
            expect(throws([&] { context::validate_redeemer_budgets(redeemers, { max, max }); }));
        };
        "stored context validation"_test = [] {
            stored_tx_context stored_ctx {};
            expect_equal(stored_tx_context::current_format_version, stored_ctx.format_version);
            expect(throws([&] { stored_ctx.validate_format(); }));
            stored_ctx.protocol_ver = { 10, 2 };
            expect(nothrow([&] { stored_ctx.validate_format(); }));
            --stored_ctx.format_version;
            expect(throws([&] { stored_ctx.validate_format(); }));
        };
    };
};
