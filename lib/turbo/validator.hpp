#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/indexer.hpp>

namespace turbo::cardano::ledger {
    struct state;
}

namespace turbo::validator {
    static constexpr std::string_view validate_task{"validate"};
    static constexpr std::string_view validate_leaders_task{"validate-epoch"};
    static constexpr uint64_t snapshot_format_version = 2;

    struct snapshot {
        uint64_t epoch;
        uint64_t end_offset;
        uint64_t last_slot;
        bool exportable;
        uint64_t format_version = snapshot_format_version;

        static snapshot from_json(const json::value &j);
        snapshot(const cardano::ledger::state &st);
        snapshot(uint64_t epoch_, uint64_t end_offset_, uint64_t last_slot_, bool exportable_,
            uint64_t format_version_=snapshot_format_version);
        json::object to_json() const;

        bool operator==(const snapshot &o) const
        {
            return epoch == o.epoch && end_offset == o.end_offset && last_slot == o.last_slot
                && exportable == o.exportable && format_version == o.format_version;
        }

        bool operator<(const snapshot &b) const
        {
            return end_offset < b.end_offset;
        }
    };

    struct snapshot_set: std::set<snapshot> {
        using set::set;

        using action_t = std::function<void(const snapshot &)>;
        using const_iterator = typename set<snapshot>::const_iterator;
        using best_predicate_t = std::function<bool(const snapshot &)>;

        const_iterator next_excessive() const;
        void remove_excessive(const action_t &on_remove, const action_t &on_keep);
        const snapshot *best(const best_predicate_t &pred) const;
    };

    extern indexer::indexer_map default_indexers(const std::string &data_dir, scheduler &sched=scheduler::get());

    struct incremental {
        incremental(chunk_registry &cr);
        ~incremental();
        [[nodiscard]] cardano::amount unspent_reward(const cardano::stake_ident &id) const;
        [[nodiscard]] cardano::tail_relative_stake_map tail_relative_stake() const;
        [[nodiscard]] cardano::optional_point core_tip() const;
        [[nodiscard]] cardano::optional_slot can_export(const cardano::optional_point &immutable_tip) const;
        std::string node_export(const std::filesystem::path &ledger_dir, const cardano::optional_point &immutable_tip, int prio=1000) const;
        [[nodiscard]] const cardano::ledger::state &state() const;
        [[nodiscard]] const snapshot_set &snapshots() const;
        void load_snapshot(cardano::ledger::state &st, const snapshot &snap) const;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::validator::snapshot>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::validator::snapshot &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "epoch: {} last_slot: {} end_offset: {} version: {} {}",
                v.epoch, v.last_slot, v.end_offset, v.format_version, v.exportable ? "exportable" : "");
        }
    };
}
