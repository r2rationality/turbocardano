#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/zpp.hpp>

namespace turbo::zpp_stream {
    struct write_stream {
        static constexpr size_t chunk_size = 1 << 14; // Small enough to fit into the cache available to a single thread
        static constexpr size_t zstd_level = 3;

        write_stream(const write_stream&) = delete;

        write_stream(write_stream &&o):
            _s { std::move(o._s) },
            _zstd_buf { std::move(o._zstd_buf) },
            _buf { std::move(o._buf) }
        {
            _out.position() = o._out.position();
        }

        explicit write_stream(const std::string &path):
            _s { path }
        {
            _buf.reserve(chunk_size + 0x10000);
        }

        ~write_stream()
        {
            flush();
        }

        write_stream &operator=(write_stream &&o)
        {
            _s = std::move(o._s);
            _zstd_buf = std::move(o._zstd_buf);
            _buf = std::move(o._buf);
            _out.position() = o._out.position();
            return *this;
        }

        void flush()
        {
            if (!_buf.empty()) [[likely]] {
                zstd::compress(_zstd_buf, _buf, zstd_level);
                if (_zstd_buf.size() > std::numeric_limits<uint32_t>::max()) [[unlikely]]
                    throw std::runtime_error("the compressed size exceeds the maximum allowed size!");
                const uint32_t sz = _zstd_buf.size();
                _s.write(&sz, sizeof(sz));
                _s.write(_zstd_buf.data(), _zstd_buf.size());
                _buf.clear();
                _out.reset();
            }
        }

        template<typename T>
        void write(const T &v)
        {
            _out(v).or_throw();
            if (_buf.size() >= chunk_size) [[unlikely]]
                flush();
        }
    private:
        file::write_stream _s;
        uint8_vector _zstd_buf {};
        uint8_vector _buf {};
        ::zpp::bits::out<uint8_vector> _out { _buf };
    };

    struct read_stream {
        read_stream(const read_stream&) = delete;

        read_stream(const std::string &path):
            _s_size { std::filesystem::file_size(path) },
            _s { path }
        {
        }

        read_stream(read_stream &&o):
            _s_size { o._s_size }, _s_pos { o._s_pos },
            _s { std::move(o._s) }, _zstd_buf { std::move(o._zstd_buf) },
            _buf { std::move(o._buf) }
        {
            _in.position() = o._in.position();
        }

        bool eof() const
        {
            return _s_pos >= _s_size && _in.position() >= _buf.size();
        }

        template<typename T>
        T read()
        {
            if (_in.position() >= _buf.size()) [[unlikely]] {
                try {
                    uint32_t zstd_sz;
                    _s.read(&zstd_sz, sizeof(zstd_sz));
                    _s_pos += sizeof(zstd_sz);
                    _zstd_buf.resize(zstd_sz);
                    _s.read(_zstd_buf.data(), _zstd_buf.size());
                    _s_pos += _zstd_buf.size();
                    zstd::decompress(_buf, _zstd_buf);
                    _in.reset();
                } catch (const std::exception &ex) {
                    throw error(fmt::format("read failed while at byte {} out of {}: {}", _s_pos, _s_size, ex.what()));
                }
            }
            T v;
            _in(v).or_throw();
            return v;
        }

        template<typename T>
        std::optional<T> next()
        {
            if (!eof())
                return read<T>();
            return {};
        }
    private:
        const uint64_t _s_size;
        uint64_t _s_pos = 0;
        file::read_stream _s;
        uint8_vector _zstd_buf {};
        uint8_vector _buf {};
        ::zpp::bits::in<uint8_vector> _in { _buf };
    };
}