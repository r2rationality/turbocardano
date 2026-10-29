/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <array>
#include <barrier>
#include <exception>
#include <thread>
#include <turbo/common/test.hpp>
#include <turbo/json.hpp>
#include <turbo/plutus/cost-model-history.hpp>
#include <turbo/plutus/costs-config.hpp>
#include <turbo/zpp.hpp>

using namespace turbo;
using namespace turbo::plutus;

namespace {
    struct legacy_update {
        uint64_t epoch;
        cardano::plutus_cost_models models;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.epoch, self.models);
        }
    };
}

suite plutus_cost_model_history_suite = [] {
    "plutus::cost_model_history"_test = [] {
        "JSON preserves snapshots and compiles each language"_test = [] {
            const auto data = boost::json::parse(R"([
                {"epoch": 10, "models": {}},
                {"epoch": 20, "models": {
                    "0": [123456], "1": [234567], "2": [345678],
                    "3": [-7, 9223372036854775807]
                }}
            ])");
            const auto history = cost_model_history_t::from_json(data);
            expect(history.to_json() == data);
            const auto restored = cost_model_history_t::from_json(boost::json::parse(json::serialize(history.to_json())));
            expect(restored.to_json() == data);
            const auto &models = restored.for_epoch(20);
            expect_equal(uint64_t{123456}, models.for_script(cardano::script_type::plutus_v1,
                builtin_semantics::a).builtin_costs.at(builtin_tag::add_integer).cpu.args[0]);
            expect_equal(uint64_t{234567}, models.for_script(cardano::script_type::plutus_v2,
                builtin_semantics::b).builtin_costs.at(builtin_tag::add_integer).cpu.args[0]);
            expect_equal(uint64_t{345678}, models.for_script(cardano::script_type::plutus_v3,
                builtin_semantics::e).builtin_costs.at(builtin_tag::add_integer).cpu.args[0]);
        };
        "epoch lookup handles boundaries and unordered input"_test = [] {
            const auto history = cost_model_history_t::from_json(boost::json::parse(R"([
                {"epoch": 20, "models": {"0": [222222]}},
                {"epoch": 10, "models": {"0": [111111]}}
            ])"));
            expect(throws([&] { history.for_epoch(9); }));
            for (const uint64_t epoch: {10, 19, 20, 21}) {
                const auto &model = history.for_epoch(epoch).for_script(cardano::script_type::plutus_v1, builtin_semantics::a);
                expect_equal(epoch < 20 ? uint64_t{111111} : uint64_t{222222},model.builtin_costs.at(builtin_tag::add_integer).cpu.args[0]);
            }
            expect_equal(uint64_t{10}, json::value_to<uint64_t>(history.to_json().at(0).at("epoch")));
        };
        "invalid histories are rejected"_test = [] {
            for (const auto text: {
                R"([])",
                R"([{"epoch": 10, "models": {}}, {"epoch": 10, "models": {}}])",
                R"([{"epoch": 10, "models": {"bad": []}}])",
                R"([{"epoch": 10, "models": {"0junk": []}}])",
                R"([{"epoch": 10, "models": {"0": [], "00": []}}])",
                R"([{"epoch": 10, "models": {"0": ["bad"]}}])"
            }) {
                expect(throws([&] { cost_model_history_t::from_json(boost::json::parse(text)); }));
            }
        };
        "ZPP retains the existing update layout in both directions"_test = [] {
            cardano::plutus_cost_models models{};
            models.items.emplace(0, cardano::plutus_cost_model{{123456}, costs::cost_arg_names_v1()});
            const auto bytes = turbo::zpp::serialize(std::vector<legacy_update>{{10, models}});
            const auto history = turbo::zpp::deserialize<cost_model_history_t>(bytes);
            expect(turbo::zpp::serialize(history) == bytes);
            expect(history.to_json() == boost::json::parse(R"([{"epoch": 10, "models": {"0": [123456]}}])"));
        };
        "builder records only change points and enforces append order"_test = [] {
            cardano::plutus_cost_models first{}, second{};
            first.items.emplace(0, cardano::plutus_cost_model{{111111}, costs::cost_arg_names_v1()});
            second.items.emplace(0, cardano::plutus_cost_model{{222222}, costs::cost_arg_names_v1()});
            cost_model_history_t::builder builder{};
            expect(builder.append(0, first));
            expect(!builder.append(10, first));
            expect(throws([&] { builder.append(9, second); }));
            expect(throws([&] { builder.append(10, first); }));
            expect(builder.append(20, second));
            expect(!builder.append(30, second));
            // Returning to an earlier model is still a new change point.
            expect(builder.append(40, first));
            const auto history = std::move(builder).finish();
            expect(history.to_json() == boost::json::parse(R"([
                {"epoch": 0, "models": {"0": [111111]}},
                {"epoch": 20, "models": {"0": [222222]}},
                {"epoch": 40, "models": {"0": [111111]}}
            ])"));
            expect(throws([] { cost_model_history_t::builder{}.finish(); }));
        };
        "raw import and export do not compile unused models"_test = [] {
            cardano::plutus_cost_models valid{}, invalid{};
            valid.items.emplace(0, cardano::plutus_cost_model{{123456}, costs::cost_arg_names_v1()});
            invalid.items.emplace(0, cardano::plutus_cost_model{{1}, {"invalid"}});
            const cost_model_history_t history{{{10, valid}, {20, invalid}}};
            const auto json_before = history.to_json();
            const auto bytes_before = turbo::zpp::serialize(history);
            const auto restored = turbo::zpp::deserialize<cost_model_history_t>(bytes_before);
            expect(restored.to_json() == json_before);
            const auto *first = &history.for_epoch(10);
            expect(&history.for_epoch(19) == first);
            try {
                history.for_epoch(20);
                expect(false) << "invalid model should fail on access";
            } catch (const error &ex) {
                expect(std::string_view{ex.what()}.find("epoch 20") != std::string_view::npos);
            }
            // Preparation retries the failed entry without disturbing the valid one.
            expect(throws([&] { history.prepare(); }));
            expect(history.to_json() == json_before);
            expect(turbo::zpp::serialize(history) == bytes_before);
            expect(&history.for_epoch(10) == first);
        };
        "concurrent first access and prepare share stable models"_test = [] {
            auto history = cost_model_history_t::from_json(boost::json::parse(R"([
                {"epoch": 10, "models": {"0": [123456]}},
                {"epoch": 20, "models": {"0": [234567]}}
            ])"));
            constexpr size_t num_threads = 2;
            std::barrier start{static_cast<std::ptrdiff_t>(num_threads)};
            std::array<const costs::runtime_models *, num_threads> results{};
            std::array<std::exception_ptr, num_threads> failures{};
            {
                std::vector<std::jthread> workers{};
                for (size_t i = 0; i < num_threads; ++i) {
                    workers.emplace_back([&, i] {
                        start.arrive_and_wait();
                        try {
                            if (i == 1)
                                history.prepare();
                            results[i] = &history.for_epoch(10);
                        } catch (...) {
                            failures[i] = std::current_exception();
                        }
                    });
                }
            }
            const auto *model = &history.for_epoch(10);
            for (size_t i = 0; i < num_threads; ++i) {
                expect(!failures[i]);
                expect(results[i] == model);
            }
            const auto moved = std::move(history);
            expect(&moved.for_epoch(10) == model);
            expect_equal(uint64_t{123456}, model->for_script(cardano::script_type::plutus_v1,
                builtin_semantics::a).builtin_costs.at(builtin_tag::add_integer).cpu.args[0]);
            expect_equal(uint64_t{234567}, moved.for_epoch(20).for_script(cardano::script_type::plutus_v1,
                builtin_semantics::a).builtin_costs.at(builtin_tag::add_integer).cpu.args[0]);
        };
        "failed deserialization does not replace the current history"_test = [] {
            auto history = cost_model_history_t::from_json(boost::json::parse(
                R"([{"epoch": 10, "models": {"0": [123456]}}])"));
            const auto before = history.to_json();
            // A complete archive can decode successfully but fail history validation.
            const cost_model_history_t::update_list duplicates{{10, {}}, {10, {}}};
            const auto bytes = turbo::zpp::serialize(duplicates);
            expect(throws([&] { turbo::zpp::deserialize(history, bytes); }));
            expect(history.to_json() == before);
            // Truncation exercises the archive-error path before history construction.
            auto truncated = turbo::zpp::serialize(history);
            truncated.pop_back();
            expect(throws([&] { turbo::zpp::deserialize(history, truncated); }));
            expect(history.to_json() == before);
            const cost_model_history_t empty{};
            expect(throws([&] { empty.prepare(); }));
            expect(throws([&] { empty.to_json(); }));
            expect(throws([&] { turbo::zpp::serialize(empty); }));
        };
    };
};
