/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano {
    ipv4_addr ipv4_addr::from_cbor(cbor::zero2::value &v)
    {
        return { v.bytes() };
    }

    ipv6_addr ipv6_addr::from_cbor(cbor::zero2::value &v)
    {
        return { v.bytes() };
    }


    pool_params pool_params::from_cbor(cbor::zero2::array_reader &it)
    {
        // assumes the pool hash has already been consumed!
        return pool_params {
            it.read().bytes(),
            {},
            it.read().uint(),
            it.read().uint(),
            decltype(margin)::from_cbor(it.read()),
            it.read().bytes(),
            decltype(owners)::from_cbor(it.read()),
            decltype(relays)::from_cbor(it.read()),
            decltype(metadata)::from_cbor(it.read())
        };
    }

    pool_metadata pool_metadata::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { std::string { it.read().text() }, it.read().bytes() };
    }

    relay_addr relay_addr::from_cbor(cbor::zero2::array_reader &it)
    {
        return { decltype(port)::from_cbor(it.read()), decltype(ipv4)::from_cbor(it.read()), decltype(ipv6)::from_cbor(it.read()) };
    }

    relay_host relay_host::from_cbor(cbor::zero2::array_reader &it)
    {
        const auto port = decltype(relay_host::port)::from_cbor(it.read());
        const auto host = it.read().text();
        if (host.size() > 128U) [[unlikely]]
            throw error{"conway::relay_host must not be larger than 128 characters but got {}!", host.size()};
        return {std::move(port), std::string{host}};
    }

    relay_dns relay_dns::from_cbor(cbor::zero2::array_reader &it)
    {
        const auto name = it.read().text();
        if (name.size() > 128U) [[unlikely]]
            throw error{"conway::relay_dns name must not be larger than 128 characters but got {}!", name.size()};
        return {std::string{name}};
    }

    relay_info relay_info::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return { relay_addr::from_cbor(it) };
            case 1: return { relay_host::from_cbor(it) };
            case 2: return { relay_dns::from_cbor(it) };
            [[unlikely]] default: throw error(fmt::format("Unsupported relay address format {}!", typ));
        }
    }
}
