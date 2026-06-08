/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

extern "C" {
#ifdef __APPLE__
#   include <mach/mach_init.h>
#   include <mach/task_info.h>
#   include <mach/task.h>
#endif
#ifdef _WIN32
#   define NOMINMAX 1
#   include <windows.h>
#   include <psapi.h>
#else
#   include <unistd.h>
#   include <sys/time.h>
#   include <sys/resource.h>
#endif
}
#include <fstream>
#include "error.hpp"
#include "format.hpp"
#include "mutex.hpp"
#include "memory.hpp"

namespace turbo::memory {
    // Win API is not thread safe by default
    static mutex::unique_lock::mutex_type _win_api_mutex alignas(mutex::alignment) {};

    size_t my_usage_mb()
    {
        mutex::scoped_lock lk{_win_api_mutex};
#       ifdef _WIN32
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
            return static_cast<size_t>(pmc.WorkingSetSize >> 20U);
#       elif __linux__
            static size_t page_size = [](){
                const long v = sysconf(_SC_PAGESIZE);
                if (v < 0) [[unlikely]]
                    throw error_sys("sysconf failed");
                return static_cast<size_t>(v);
            }();

            // statm file has always a 0 size despite having the content, so cannot use file::read
            std::ifstream is { "/proc/self/statm" };
            if (!is) [[unlikely]]
                throw error("unable to open /proc/self/statm");
            std::string stat;
            std::getline(is, stat);
            auto pos = stat.find(' ');
            if (pos == stat.npos) [[unlikely]]
                throw error(fmt::format("invalid /proc/self/statm file format: '{}'!", stat));
            const auto rss_begin = stat.find_first_not_of(' ', pos);
            if (rss_begin == stat.npos) [[unlikely]]
                throw error(fmt::format("invalid /proc/self/statm file format: '{}'!", stat));
            pos = stat.find(' ', rss_begin);
            return (std::stoull(stat.substr(rss_begin, pos - rss_begin)) * page_size) >> 20U;
#       else
            struct mach_task_basic_info tinfo{};
            mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
            task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &tinfo, &count);
            return tinfo.resident_size >> 20U;
#       endif
    }

    size_t max_usage_mb()
    {
        mutex::scoped_lock lk{_win_api_mutex};
#       ifdef _WIN32
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
            return static_cast<size_t>(pmc.PeakWorkingSetSize >> 20U);
#       else
            struct rusage ru {};
            if (getrusage(RUSAGE_SELF, &ru) != 0)
                throw error_sys("getrusage failed");
#           ifdef __APPLE__
            return static_cast<size_t>(ru.ru_maxrss >> 20U); // macOS reports in bytes
#           else
            return static_cast<size_t>(ru.ru_maxrss >> 10U); // Linux reports in KB
#           endif
#       endif
    }

    size_t physical_mb()
    {
        mutex::scoped_lock lk{_win_api_mutex};
#       ifdef _WIN32
            MEMORYSTATUSEX status;
            status.dwLength = sizeof(status);
            GlobalMemoryStatusEx(&status);
            return static_cast<size_t>(status.ullTotalPhys >> 20U);
#       else
            const long pages = sysconf(_SC_PHYS_PAGES);
            const long page_size = sysconf(_SC_PAGE_SIZE);
            if (pages < 0 || page_size < 0) [[unlikely]]
                throw error_sys("sysconf failed");
            return (static_cast<size_t>(pages) * static_cast<size_t>(page_size)) >> 20U;
#       endif
    }
}
