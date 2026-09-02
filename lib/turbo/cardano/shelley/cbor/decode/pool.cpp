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
        return { decltype(port)::from_cbor(it.read()), std::string { it.read().text() } };
    }

    relay_dns relay_dns::from_cbor(cbor::zero2::array_reader &it)
    {
        return { std::string { it.read().text() } };
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
