#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

namespace turbo::parallel {
    template<typename T>
    T atomic_max(std::atomic<T> &val, const T &candidate)
    {
        for (;;) {
            auto prev_val = val.load(std::memory_order_relaxed);
            if (candidate <= prev_val)
                return prev_val;
            if (val.compare_exchange_weak(prev_val, candidate, std::memory_order_relaxed, std::memory_order_relaxed))
                return prev_val;
        }
    }
}
