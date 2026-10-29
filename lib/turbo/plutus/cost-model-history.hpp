#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>
#include <turbo/container/flat_map.hpp>
#include <turbo/json-fwd.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/zpp.hpp>

namespace turbo::plutus {
    struct cost_model_history_t {
        struct update_t {
            uint64_t epoch;
            cardano::plutus_cost_models models;

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.epoch, self.models);
            }
        };
        using update_list = std::vector<update_t>;

        struct active_models_t {
            uint64_t epoch;
            const costs::runtime_models &models;
        };

        struct builder {
            bool append(uint64_t epoch, const cardano::plutus_cost_models &);
            cost_model_history_t finish() &&;
        private:
            update_list _updates{};
            std::optional<uint64_t> _last_epoch{};
        };

        cost_model_history_t() =default;
        explicit cost_model_history_t(update_list updates);
        cost_model_history_t(const cost_model_history_t &) =delete;
        cost_model_history_t &operator=(const cost_model_history_t &) =delete;
        cost_model_history_t(cost_model_history_t &&) =default;
        cost_model_history_t &operator=(cost_model_history_t &&) =default;

        static cost_model_history_t from_json(const json::value &);
        json::value to_json() const;

        static auto serialize(auto &archive, auto &self)
        {
            if constexpr (std::remove_cvref_t<decltype(archive)>::kind() == ::zpp::bits::kind::in) {
                update_list updates{};
                const auto result = archive(updates);
                if (!::zpp::bits::failure(result))
                    self = cost_model_history_t{std::move(updates)};
                return result;
            } else {
                return archive(self._raw_updates());
            }
        }

        const costs::runtime_models &for_epoch(uint64_t epoch) const;
        void prepare() const;
    private:
        struct entry_t {
            explicit entry_t(cardano::plutus_cost_models models): raw{std::move(models)} {}
            const costs::runtime_models &get(uint64_t epoch) const;

            cardano::plutus_cost_models raw;
            mutable std::once_flag ready{};
            mutable std::unique_ptr<const costs::runtime_models> runtime{};
        };
        flat_map<uint64_t, std::unique_ptr<const entry_t>> _entries{};

        void _require_nonempty() const;
        update_list _raw_updates() const;
    };
}
