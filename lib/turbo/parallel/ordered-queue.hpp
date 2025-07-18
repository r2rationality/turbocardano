#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <atomic>
#include <turbo/common/error.hpp>
#include <turbo/common/mutex.hpp>

namespace turbo::parallel {
    // This queue allows multiple pre-processing threads to report ready items
    // and guarantees that consumers will process items only in the pre-defined order.
    // It works with indices, so the actual storage of the items must be organized separately.
    struct ordered_queue {
        using index_type = uint64_t;
        using optional_index = std::optional<index_type>;
        using index_range = std::pair<index_type, index_type>;
        using optional_index_range = std::optional<index_range>;

        void put(const index_type item_idx)
        {
            if (item_idx == std::numeric_limits<index_type>::max()) [[unlikely]]
                throw error(fmt::format("the index is too large: {}", item_idx));
            mutex::scoped_lock lk { _put_mutex };
            _unordered.emplace(item_idx);
            index_type proposed_next = _next_ordered.load(std::memory_order_relaxed);
            for (auto it = _unordered.begin(); it != _unordered.end() && proposed_next == *it; it = _unordered.erase(it)) {
                proposed_next = *it + 1;
            }
            _next_ordered.store(proposed_next, std::memory_order_release);
        }

        optional_index take()
        {
            for (;;) {
                index_type exp_idx = _next_take.load(std::memory_order_relaxed);
                if (exp_idx == _next_ordered.load(std::memory_order_relaxed))
                    return {};
                if (_next_take.compare_exchange_strong(exp_idx, exp_idx + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                    return exp_idx;
            }
        }

        optional_index_range take_all()
        {
            const optional_index first_item = take();
            if (!first_item)
                return {};
            auto last_item = first_item;
            for (auto next_item = first_item; next_item; next_item = take()) {
                last_item = next_item;
            }
            return index_range { *first_item, *last_item };
        }

        index_type next() const
        {
            return _next_take.load(std::memory_order_relaxed);
        }
    private:
        std::atomic<index_type> _next_take { 0 };
        std::atomic<index_type> _next_ordered { 0 };
        mutex::unique_lock::mutex_type _put_mutex alignas(mutex::alignment) {};
        std::set<index_type> _unordered {};
    };
}