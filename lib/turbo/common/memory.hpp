#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <cstddef>

namespace turbo::memory {
    extern size_t max_usage_mb();
    extern size_t physical_mb();
    extern size_t my_usage_mb();
}