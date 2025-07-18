#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace turbo {
    struct base_error: std::exception {
        static constexpr size_t stacktrace_depth = 0x20;

        explicit base_error(std::string_view msg);
        const char *what() const noexcept override;
    private:
        std::string _msg;
#ifdef TURBO_STACKTRACE
        std::array<std::byte, sizeof(void*) * stacktrace_depth> _trace {};
#endif
    };

    struct error: base_error {
        explicit error(std::string_view msg);
        explicit error(std::string_view msg, const std::exception &ex);
    };

    struct error_sys: error {
        explicit error_sys(std::string_view msg);
    };
}
