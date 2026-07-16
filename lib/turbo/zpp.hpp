#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#if defined(__clang_major__) && (__clang_major__ >= 19)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wmissing-template-arg-list-after-template-kw"
#endif
#include <zpp_bits.h>
#if defined(__clang_major__) && (__clang_major__ >= 19)
#    pragma clang diagnostic pop
#endif
#include <turbo/common/zstd.hpp>

namespace turbo::zpp {
    template<typename T>
    size_t deserialize(T &v, const buffer zpp_data)
    {
        ::zpp::bits::in in { zpp_data };
        in(v).or_throw();
        return in.position();
    }

    template<typename T>
    T deserialize(const buffer zpp_data)
    {
        ::zpp::bits::in in { zpp_data };
        T v;
        in(v).or_throw();
        return v;
    }

    template<typename T>
    void load(T &v, const std::string &path)
    {
        const auto zpp_data = file::read(path);
        ::zpp::bits::in in { static_cast<buffer>(zpp_data) };
        in(v).or_throw();
    }

    template<typename T>
    T load(const std::string &path)
    {
        T v;
        const auto zpp_data = file::read(path);
        ::zpp::bits::in in { static_cast<buffer>(zpp_data) };
        in(v).or_throw();
        return v;
    }

    template<typename T>
    void load_zstd(T &v, const std::string &path)
    {
        const auto zpp_data = zstd::read(path);
        ::zpp::bits::in in { static_cast<buffer>(zpp_data) };
        in(v).or_throw();
    }

    template<typename T>
    T load_zstd(const std::string &path)
    {
        const auto zpp_data = zstd::read(path);
        ::zpp::bits::in in { static_cast<buffer>(zpp_data) };
        T v;
        in(v).or_throw();
        return v;
    }

    template<typename T>
    void serialize(uint8_vector &zpp_data, const T &v)
    {
        ::zpp::bits::out out { zpp_data };
        out(v).or_throw();
    }

    template<typename T>
    uint8_vector serialize(const T &v)
    {
        uint8_vector zpp_data {};
        serialize(zpp_data, v);
        return zpp_data;
    }

    template<typename T>
    void save(const std::string &path, const T &v)
    {
        uint8_vector zpp_data {};
        ::zpp::bits::out out { zpp_data };
        out(v).or_throw();
        file::write(path, zpp_data);
    }

    template<typename T>
    void save_zstd(const std::string &path, const T &v)
    {
        uint8_vector zpp_data {};
        ::zpp::bits::out out { zpp_data };
        out(v).or_throw();
        zstd::write(path, zpp_data);
    }
}