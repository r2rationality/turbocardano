#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <fmt/format.h>

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

        // All arguments after pattern are formatting values. To attach a cause,
        // format the message explicitly and use error(message, cause).
        // Exclude the two-argument cause overload from this template.
        template<typename Arg, typename ...Args>
            requires (sizeof...(Args) > 0 || !std::is_base_of_v<std::exception, std::remove_cvref_t<Arg>>)
        explicit error(fmt::format_string<Arg, Args...> pattern, Arg &&arg, Args &&...args)
            : error { fmt::format(pattern, std::forward<Arg>(arg), std::forward<Args>(args)...) }
        {
        }
    };

    struct error_sys: error {
        explicit error_sys(std::string_view msg);
    };
}
