/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "bytes.hpp"
#if defined(_WIN32)
#   include <windows.h>
#elif defined(__GLIBC__) || defined(__APPLE__)
#   include <string.h>
#endif

namespace turbo {
    void secure_clear(std::span<uint8_t> store)
    {
#if defined(_WIN32)
        SecureZeroMemory(store.data(), store.size());
#elif defined(__GLIBC__) || defined(__APPLE__)
        explicit_bzero(store.data(), store.size());
#else
        std::fill_n<volatile uint8_t *>(store.data(), store.size(), 0);
#endif
    }
}
