/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#ifndef _WIN32
#    include <sys/resource.h>
#endif
#include "file.hpp"

namespace turbo::file {
    void set_max_open_files()
    {
        static size_t current_max_open_files = 0;
        if (current_max_open_files != max_open_files) {
#           ifdef _WIN32
            if (_setmaxstdio(max_open_files) != max_open_files)
                throw error_sys(fmt::format("can't increase the max number of open files to {}!", max_open_files));
#           else
            struct rlimit lim;
            if (getrlimit(RLIMIT_NOFILE, &lim) != 0)
                throw error_sys("getrlimit failed");
            if (lim.rlim_cur < max_open_files || lim.rlim_max < max_open_files) {
                lim.rlim_cur = max_open_files;
                lim.rlim_max = max_open_files;
                if (setrlimit(RLIMIT_NOFILE, &lim) != 0)
                    throw error_sys(fmt::format("failed to increase the max number of open files to {}", max_open_files));
                if (getrlimit(RLIMIT_NOFILE, &lim) != 0)
                    throw error_sys("getrlimit failed");
            }
#           endif
            current_max_open_files = max_open_files;
        }
    }

    void read_stream::seek(std::streampos off)
    {
#if     _WIN32
        if (_fseeki64(_f, off, SEEK_SET) != 0)
#else
        if (fseek(_f, off, SEEK_SET) != 0)
#endif
            throw error_sys(fmt::format("failed to seek in {}", _path));
    }

    void write_stream::seek(std::streampos off)
    {
#if     _WIN32
        if (_fseeki64(_f, off, SEEK_SET) != 0)
#else
        if (fseek(_f, off, SEEK_SET) != 0)
#endif
            throw error_sys(fmt::format("failed to seek in {}", _path));
    }

    uint64_t write_stream::tellp()
    {
#if     _WIN32
        auto pos = _ftelli64(_f);
#else
        auto pos = ftell(_f);
#endif
        if (pos < 0)
            throw error_sys(fmt::format("failed to tell the stream position in {}", _path));
        return pos;
    }

    static std::filesystem::path &_install_dir()
    {
        static std::filesystem::path dir_path = std::filesystem::current_path();
        return dir_path;
    }

    // These methods are designed to work with both Unix and Windows paths
    // in exactly the same fashion independently of the host platform
    static char _path_separator(const std::string_view path_str)
    {
        if (path_str.find('/')  != std::string::npos)
            return '/';
        if (path_str.find('\\') != std::string::npos)
            return '\\';
        return std::filesystem::path::preferred_separator;
    }

    static std::filesystem::path _path_append(const std::filesystem::path &base_path, const std::filesystem::path &rel_path)
    {
        const auto bp_str = base_path.string();
        const auto sep = _path_separator(bp_str);
        if (!bp_str.empty() && bp_str.back() != sep)
            return bp_str + _path_separator(base_path.string()) + rel_path.string();
        return bp_str + rel_path.string();
    }

    static std::filesystem::path _path_parent(const std::filesystem::path &path)
    {
        const auto pathstr = path.string();
        const auto sep = _path_separator(pathstr);
        if (const auto pos = pathstr.rfind(sep); pos != std::string::npos) {
            if (sep == '\\') {
                if (pos > 2)
                    return pathstr.substr(0, pos);
                if (pathstr.size() > 2)
                    return pathstr.substr(0, 3);
            } else {
                if (pos > 0)
                    return pathstr.substr(0, pos);
                if (pathstr.size() > 1)
                    return "/";
            }
        }
        return {};
    }

    static bool _path_absolute(const std::filesystem::path &path)
    {
        const auto pathstr = path.string();
        const auto sep = _path_separator(pathstr);
        if (sep == '\\' && pathstr.size() >= 3 && pathstr[1] == ':' && pathstr[2] == '\\')
            return true;
        if (sep == '/' && !pathstr.empty() && pathstr.front() == '/')
            return true;
        return false;
    }

    void set_install_path_exact(const std::filesystem::path &p)
    {
        if (!_path_absolute(p)) [[unlikely]]
            throw error(fmt::format("file::set_install_path_exact requires and absolute path but got: {}", p));
        _install_dir() = p;
    }

    void set_install_path(const std::string_view bin_path)
    {
        std::filesystem::path bp{bin_path};
        if (bp.empty()) [[unlikely]]
            throw error(fmt::format("set_install_path: bin_path must not be empty!"));
        if (!_path_absolute(bp))
            bp = _path_append(std::filesystem::current_path(), bp);
        auto ip = bp;
        for (size_t i = 0; i < 2; ++i) {
            auto pp = _path_parent(ip);
            if (pp.empty() || pp == ip) [[unlikely]]
                throw error(fmt::format("set_install_path: bin_path must have at least two parent directories: {}!", bin_path));
            ip = pp;
        }
        set_install_path_exact(ip);
    }

    std::string install_path(const std::string_view rel_path)
    {
        std::filesystem::path rp{rel_path};
        if (_path_absolute(rp))
            return rp.string();
        if (rp.empty())
            return _install_dir().string();
        return _path_append(_install_dir(), rp).string();
    }

    std::vector<std::filesystem::path> files_with_ext_path(const std::string_view &dir, const std::string_view &ext)
    {
        std::vector<std::filesystem::path> res {};
        for (auto &entry: std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension().string() == ext)
                res.emplace_back(entry.path());
        }
        std::sort(res.begin(), res.end());
        return res;
    }

    std::vector<std::string> files_with_ext(const std::string_view &dir, const std::string_view &ext)
    {
        std::vector<std::string> res {};
        for (const auto &p: files_with_ext_path(dir, ext)) {
            res.emplace_back(p.string());
        }
        return res;
    }
}
