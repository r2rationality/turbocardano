/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <charconv>
#include <turbo/json.hpp>
#include <turbo/plutus/cost-model-history.hpp>
#include <turbo/plutus/costs-config.hpp>

namespace turbo::plutus {
    cost_model_history_t::cost_model_history_t(update_list updates)
    {
        if (updates.empty()) [[unlikely]]
            throw error{"cost model history must not be empty"};
        std::sort(updates.begin(), updates.end(), [](const auto &a, const auto &b) { return a.epoch < b.epoch; });
        for (size_t i = 1; i < updates.size(); ++i) {
            if (updates[i - 1].epoch == updates[i].epoch) [[unlikely]]
                throw error{"duplicate cost model epoch: {}", updates[i].epoch};
        }
        _entries.reserve(updates.size());
        for (auto &update: updates)
            _entries.emplace_hint(_entries.end(), update.epoch, std::make_unique<const entry_t>(std::move(update.models)));
    }

    bool cost_model_history_t::builder::append(const uint64_t epoch, const cardano::plutus_cost_models &models)
    {
        if (_last_epoch && epoch <= *_last_epoch) [[unlikely]]
            throw error{"cost model epochs must increase: {} after {}", epoch, *_last_epoch};
        const bool changed = _updates.empty() || _updates.back().models != models;
        if (changed)
            _updates.emplace_back(epoch, models);
        _last_epoch = epoch;
        return changed;
    }

    cost_model_history_t cost_model_history_t::builder::finish() &&
    {
        return cost_model_history_t{std::move(_updates)};
    }

    cost_model_history_t cost_model_history_t::from_json(const json::value &history)
    {
        update_list updates{};
        updates.reserve(history.as_array().size());
        for (const auto &update: history.as_array()) {
            const auto epoch = json::value_to<uint64_t>(update.at("epoch"));
            cardano::plutus_cost_models models{};
            for (const auto &[key, values]: update.at("models").as_object()) {
                try {
                    uint64_t id = 0;
                    const auto end = key.data() + key.size();
                    const auto [ptr, ec] = std::from_chars(key.data(), end, id);
                    if (ec != std::errc{} || ptr != end) [[unlikely]]
                        throw error{"invalid cost model language id"};
                    static const std::vector<std::string> empty_names{};
                    const auto &names = id == 0 ? costs::cost_arg_names_v1()
                        : id == 1 ? costs::cost_arg_names_v2()
                        : id == 2 ? costs::cost_arg_names_v3() : empty_names;
                    auto raw = json::value_to<cardano::plutus_cost_model::raw_value_type>(values);
                    if (!models.items.emplace(id, cardano::plutus_cost_model{std::move(raw), names}).second) [[unlikely]]
                        throw error{"duplicate cost model language id: {}", id};
                } catch (const std::exception &ex) {
                    throw error{"cost model epoch {} language {}: {}", epoch, key, ex.what()};
                }
            }
            updates.emplace_back(epoch, std::move(models));
        }
        return cost_model_history_t{std::move(updates)};
    }

    json::value cost_model_history_t::to_json() const
    {
        _require_nonempty();
        json::array history{};
        history.reserve(_entries.size());
        // Full snapshots, with ledger language IDs (0 = V1) and ordered raw parameters.
        for (const auto &[epoch, entry]: _entries) {
            json::object models{};
            for (const auto &[id, model]: entry->raw.items)
                models.emplace(fmt::format("{}", id), json::value_from(model.raw_values()));
            history.emplace_back(json::object{{"epoch", epoch}, {"models", std::move(models)}});
        }
        return history;
    }

    const costs::runtime_models &cost_model_history_t::for_epoch(const uint64_t epoch) const
    {
        const auto it = _entries.upper_bound(epoch);
        if (it == _entries.begin()) [[unlikely]]
            throw error{"can't find a cost model for epoch {}", epoch};
        const auto &selected = *std::prev(it);
        return selected.second->get(selected.first);
    }

    void cost_model_history_t::prepare() const
    {
        _require_nonempty();
        for (const auto &[epoch, entry]: _entries)
            entry->get(epoch);
    }

    const costs::runtime_models &cost_model_history_t::entry_t::get(const uint64_t epoch) const
    {
        std::call_once(ready, [&] {
            try {
                // Construct directly in heap storage; optional<runtime_models> would reserve
                // the large inline tables even for change points that are never evaluated.
                runtime.reset(new costs::runtime_models(costs::ingest(raw)));
            } catch (const std::exception &ex) {
                throw error{"cost model compilation at epoch {}: {}", epoch, ex.what()};
            }
        });
        return *runtime;
    }

    void cost_model_history_t::_require_nonempty() const
    {
        if (_entries.empty()) [[unlikely]]
            throw error{"cost model history must not be empty"};
    }

    cost_model_history_t::update_list cost_model_history_t::_raw_updates() const
    {
        _require_nonempty();
        update_list updates{};
        updates.reserve(_entries.size());
        for (const auto &[epoch, entry]: _entries)
            updates.emplace_back(epoch, entry->raw);
        return updates;
    }
}
