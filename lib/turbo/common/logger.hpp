#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <exception>
#include <functional>

#if defined(__GNUC__) && !defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Warray-bounds"
#   pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
#ifndef SPDLOG_FMT_EXTERNAL
#   define SPDLOG_FMT_EXTERNAL 1
#endif
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#if defined(__GNUC__) && !defined(__clang__)
#   pragma GCC diagnostic pop
#endif
#include "format.hpp"

namespace turbo::logger {
    using level = spdlog::level::level_enum;

    extern std::string &init_log_path(std::optional<std::string_view> path={});

    extern bool &tracing_enabled();
    extern spdlog::logger create(const std::string &path);

    inline spdlog::logger &get()
    {
        static spdlog::logger logger = create(init_log_path());
        return logger;
    }

    template<typename... Args>
    void log(const level lev, const std::string_view &fmt, Args&&... a)
    {
        get().log(lev, fmt::runtime(fmt), std::forward<Args>(a)...);
    }

    template<typename... Args>
    void trace(const std::string_view &fmt, Args&&... a)
    {
        log(level::trace, fmt, std::forward<Args>(a)...);
    }

    template<typename... Args>
    void debug(const std::string_view &fmt, Args&&... a)
    {
        log(level::debug, fmt, std::forward<Args>(a)...);
    }

    template<typename... Args>
    void info(const std::string_view &fmt, Args&&... a)
    {
        log(level::info, fmt, std::forward<Args>(a)...);
    }

    template<typename... Args>
    void warn(const std::string_view &fmt, Args&&... a)
    {
        log(level::warn, fmt, std::forward<Args>(a)...);
    }

    template<typename... Args>
    void error(const std::string_view &fmt, Args&&... a)
    {
        log(level::err, fmt, std::forward<Args>(a)...);
    }

    using action = std::function<void()>;
    using optional_action = std::optional<action>;

    template<typename F, typename C=std::nullptr_t>
    std::exception_ptr run_log_errors(F &&main, const std::optional<C> &cleanup={},
            const std::source_location &loc=std::source_location::current())
    {
        std::exception_ptr cur_ex{};
        try {
            std::forward<F>(main)();
        } catch (const std::exception &ex) {
            cur_ex = std::current_exception();
            error("block at {}:{} failed with std::exception: {}", loc.file_name(), loc.line(), ex.what());
        } catch (...) {
            cur_ex = std::current_exception();
            error("block at {}:{} failed with an unknown error", loc.file_name(), loc.line());
        }
        if constexpr (!std::is_null_pointer_v<C>) {
            if (cleanup)
                (*cleanup)();
        }
        return cur_ex;
    }

    template<typename F, typename C=std::nullptr_t>
    void run_log_errors_rethrow(F &&main, const std::optional<C> &cleanup={},
            const std::source_location &loc=std::source_location::current())
    {
        if (const auto cur_ex = run_log_errors(std::forward<F>(main), cleanup, loc))
            std::rethrow_exception(cur_ex);
    }
}