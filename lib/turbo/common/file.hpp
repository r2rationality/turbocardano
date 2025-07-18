#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <cstdio>
#include <atomic>
#include <filesystem>
#include <string>
#include "bytes.hpp"
#include "format.hpp"

namespace turbo::file {
    static constexpr size_t max_open_files = 8192;

    extern void set_max_open_files();

    struct tmp {
        tmp(const std::string &name): _path { (std::filesystem::temp_directory_path() / name).string() }
        {
        }

        ~tmp()
        {
            std::filesystem::remove(_path);
        }

        const std::string &path() const
        {
            return _path;
        }

        operator const std::string &() const
        {
            return _path;
        }

        operator std::filesystem::path() const
        {
            return std::filesystem::path { _path };
        }
    private:
        std::string _path;
    };

    struct tmp_directory {
        tmp_directory(const std::string &name)
            : _path { (std::filesystem::temp_directory_path() / name).string() }
        {
            std::filesystem::create_directories(_path);
        }

        ~tmp_directory()
        {
            std::filesystem::remove_all(_path);
        }

        const std::string &path() const
        {
            return _path;
        }

        operator const std::string &() const
        {
            return _path;
        }

        operator std::filesystem::path() const
        {
            return std::filesystem::path { _path };
        }
    private:
        std::string _path;
    };

    struct stream {
        static size_t open_files()
        {
            return _open_files().load(std::memory_order_relaxed);
        }

        static size_t max_open_files()
        {
            return _max_open_files().load(std::memory_order_relaxed);
        }
    protected:
        static std::atomic_size_t &_open_files()
        {
            static std::atomic_size_t v = 0;
            return v;
        }

        static std::atomic_size_t &_max_open_files()
        {
            static std::atomic_size_t v = 0;
            return v;
        }

        static void _report_open_file()
        {
            auto open = _open_files().fetch_add(1, std::memory_order_relaxed) + 1;
            for (;;) {
                auto max = _max_open_files().load(std::memory_order_relaxed);
                if (open <= max)
                    break;
                if (_max_open_files().compare_exchange_weak(max, open, std::memory_order_relaxed, std::memory_order_relaxed))
                    break;
            }
        }
    };

    // C-style IO is used since on Mac OS the standard C++ library has very slow I/O performance.
    // At the same time C-style IO works well on Mac, Linux, and Windows.
    struct read_stream: protected stream {
        explicit read_stream(const std::string &path, const size_t buf_size=0):
            _path { path }, _buf(buf_size)
        {
            _f = std::fopen(_path.c_str(), "rb");
            if (_f == NULL) [[unlikely]]
                throw error_sys(fmt::format("failed to open a file for reading {}", _path));
            if (std::setvbuf(_f, reinterpret_cast<char *>(_buf.data()), _buf.empty() ? _IONBF : _IOFBF, _buf.size()) != 0) [[unlikely]]
                throw error_sys(fmt::format("failed to disable read buffering for {}", _path));
            _report_open_file();
        }

        read_stream(read_stream &&o): _f { o._f }, _path { std::move(o._path) }, _buf { std::move(o._buf) }
        {
            o._f = NULL;
        }

        read_stream(const read_stream &) =delete;

        ~read_stream()
        {
            close();
        }

        bool eof() const
        {
            return std::feof(_f) != 0;
        }

        void close()
        {
            if (_f != NULL) {
                if (std::fclose(_f) != 0)
                    throw error_sys(fmt::format("failed to close file {}!", _path));
                _f = NULL;
                _open_files().fetch_sub(1, std::memory_order_relaxed);
            }
        }

        void seek(std::streampos off);

        size_t try_read(std::span<uint8_t> buf)
        {
            return std::fread(buf.data(), 1, buf.size(), _f);
        }

        void read(void *data, size_t num_bytes)
        {
            if (const auto num_read = try_read(std::span { reinterpret_cast<uint8_t *>(data), num_bytes }); num_read != num_bytes)
                throw error_sys(fmt::format("could read only {} bytes instead of {} from {} ferror: {} feof: {}",
                    num_read, num_bytes, _path, std::ferror(_f), std::feof(_f)));
        }
    protected:
        std::FILE *_f = NULL;
        std::string _path {};
        uint8_vector _buf;
    };

    // C-style IO is used since on Mac OS the standard C++ library has very slow I/O performance.
    // At the same time C-style IO works well on Mac, Linux, and Windows.
    struct write_stream: protected stream {
        explicit write_stream(const std::string &path, const size_t buf_size=0):
            _path { path }, _buf(buf_size)
        {
            auto dir_path = std::filesystem::path { _path }.parent_path();
            if (!dir_path.empty())
                std::filesystem::create_directories(dir_path);
            _f = std::fopen(_path.c_str(), "wb");
            if (_f == NULL)
                throw error_sys(fmt::format("failed to open a file for writing {}", _path));
            if (std::setvbuf(_f, reinterpret_cast<char *>(_buf.data()), _buf.empty() ? _IONBF : _IOFBF, _buf.size()) != 0)
                throw error_sys(fmt::format("failed to disable write buffering for {}", _path));
            _report_open_file();
        }

        write_stream(write_stream &&ws)
            : _f { ws._f }, _path { std::move(ws._path) }, _buf { std::move(ws._buf) }
        {
            ws._f = NULL;
        }

        ~write_stream()
        {
            close();
        }

        write_stream &operator=(write_stream &&ws)
        {
            _f = ws._f;
            _path = std::move(ws._path);
            _buf = std::move(ws._buf);
            ws._f = NULL;
            return *this;
        }

        void close()
        {
            if (_f != NULL) {
                if (std::fclose(_f) != 0)
                    throw error(fmt::format("failed to close file {}!", _path));
                _f = NULL;
                _buf.clear();
                _buf.shrink_to_fit();
                _open_files().fetch_sub(1, std::memory_order_relaxed);
            }
        }

        void seek(std::streampos off);
        uint64_t tellp();

        void write(const void *data, const size_t num_bytes)
        {
            if (num_bytes > 0 && std::fwrite(data, 1, num_bytes, _f) != num_bytes)
                throw error_sys(fmt::format("failed to write {} bytes to {}", num_bytes, _path));
        }

        void write(const buffer data)
        {
            write(data.data(), data.size());
        }
    protected:
        FILE *_f = NULL;
        std::string _path {};
        uint8_vector _buf;
    };

    inline void read(const std::string &path, uint8_vector &buffer)
    {
        const auto file_size = std::filesystem::file_size(path);
        buffer.resize(file_size);
        read_stream is { path };
        is.read(buffer.data(), buffer.size());
    }

    inline uint8_vector read(const std::string &path)
    {
        uint8_vector buf {};
        read(path, buf);
        return buf;
    }

    inline uint8_vector read_all(const std::span<const std::string> &paths)
    {
        uint8_vector data {};
        for (const auto &p: paths)
            data << read(p);
        return data;
    }

    inline void read_span(const std::span<uint8_t> &v, const std::string &path, size_t num_bytes=0)
    {
        if (num_bytes == 0)
            num_bytes = std::filesystem::file_size(path);
        if (v.size() != num_bytes)
            throw error(fmt::format("span size: {} != the size of the file: {}", v.size(), num_bytes));
        read_stream is { path };
        is.read(v.data(), v.size());
    }

    inline void write(const std::string &path, const buffer &buffer) {
        const auto tmp_path = fmt::format("{}.tmp", path);
        {
            write_stream os { tmp_path };
            os.write(buffer.data(), buffer.size());
        }
        std::filesystem::rename(tmp_path, path);
    }

    inline uint64_t disk_used(const std::string &path)
    {
        uint64_t sz = 0;
        for (auto &e: std::filesystem::recursive_directory_iterator(path)) {
            if (e.is_regular_file()) {
                // On Mac file size is not cached so it is possible that the file does not exist any more
                // when its size is checked
                std::error_code ec {};
                auto e_sz = e.file_size(ec);
                if (!ec)
                    sz += e_sz;
            }
        }
        return sz;
    }

    inline uint64_t dir_size_recursive(const std::string_view path, const std::optional<std::string> &ext={})
    {
        uint64_t total_size = 0;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(path)) {
            if (ext && entry.path().extension() != *ext)
                continue;
            total_size += entry.file_size();
        }
        return total_size;
    }

    inline uint64_t dir_size(const std::string_view path, const std::optional<std::string> &ext={})
    {
        uint64_t total_size = 0;
        for (const auto &entry : std::filesystem::directory_iterator(path)) {
            if (ext && entry.path().extension() != *ext)
                continue;
            total_size += entry.file_size();
        }
        return total_size;
    }

    inline uint64_t disk_available(const std::string &path)
    {
        auto storage = std::filesystem::space(path);
        return storage.available;
    }

    using path_list = std::vector<std::filesystem::path>;
    using path_str_list = std::vector<std::string>;

    extern void set_install_path(std::string_view bin_path);
    extern std::string install_path(std::string_view rel_path);
    extern path_str_list files_with_ext(const std::string_view &dir, const std::string_view &ext);
    extern path_list files_with_ext_path(const std::string_view &dir, const std::string_view &ext);
}

namespace fmt {
    template<>
    struct formatter<std::filesystem::path>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const std::filesystem::path &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v.string());
        }
    };
}
