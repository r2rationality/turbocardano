#pragma once
/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <span>

namespace turbo::cardano::ledger::rules {
    // LEDGERS: retain the existing sorted flat event storage and expose transaction
    // spans without allocations. Locations, not hash partitions, define ledger order.
    template<typename Updates, typename Location, typename Process>
    void foreach_transaction(Updates &updates, Location &&location, Process &&process)
    {
        size_t first = 0;
        while (first < updates.size()) {
            const auto loc = location(updates[first]);
            auto end = first + 1;
            while (end < updates.size()) {
                const auto next = location(updates[end]);
                if (loc.slot != next.slot || loc.tx_idx != next.tx_idx)
                    break;
                ++end;
            }
            process(std::span { updates.data() + first, end - first });
            first = end;
        }
    }
}
