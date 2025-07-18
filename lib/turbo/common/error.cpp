/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <cerrno>
#include <cstring>
#include "error.hpp"
#include "format.hpp"

#ifdef TURBO_STACKTRACE
#   include <boost/interprocess/streams/bufferstream.hpp>
#   include <boost/stacktrace.hpp>
#   include <dt/logger.hpp>
#endif

namespace turbo {
    base_error::base_error(const std::string_view msg):
        _msg { msg }
    {
#ifdef TURBO_STACKTRACE
        // skips top 3 frames: safe_dump, base_error, and error
        boost::stacktrace::safe_dump_to(3, _trace.data(), _trace.size());
#endif
    }

    const char *base_error::what() const noexcept
    {
#ifdef TURBO_STACKTRACE
        thread_local std::array<char, 0x2000> buf {};
        boost::interprocess::obufferstream os { buf.data(), buf.size() - 1 };
        os << _msg << '\n';
        os << boost::stacktrace::stacktrace::from_dump(_trace.data(), _trace.size()) << '\n';
        // the bufferstream's constructor arguments ensure that there is always at least one byte available.
        buf[os.buffer().second] = 0;
        logger::debug("stacktrace for a user visible exception: {}", buf.data());
#endif
        return _msg.c_str();
    }

    error::error(const std::string_view msg)
        : base_error { msg }
    {
    }

    error::error(const std::string_view msg, const std::exception &ex)
        : error { fmt::format("{} caused by {}: {}", msg, typeid(ex).name(), ex.what()) }
    {
    }

    error_sys::error_sys(const std::string_view msg)
        : error { fmt::format("{} errno: {} strerror: {}", msg, errno, std::strerror(errno)) }
    {
    }
}
