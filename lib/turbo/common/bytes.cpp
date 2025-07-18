/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "bytes.hpp"

namespace turbo {
    void secure_clear(std::span<uint8_t> store)
    {
        std::fill_n<volatile uint8_t *>(store.data(), store.size(), 0);
    }
}