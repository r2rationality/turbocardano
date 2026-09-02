/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        cbor::zero2::value &set_items(cbor::zero2::value &v)
        {
            if (v.type() == cbor::major_type::array)
                return v;
            auto &tag = v.tag();
            if (tag.id() != 258) [[unlikely]]
                throw error(fmt::format("expected guard set tag 258 but got: {}", tag.id()));
            return tag.read();
        }
    }

    guards_t guards_t::from_cbor(cbor::zero2::value &v)
    {
        auto &items = set_items(v);
        const auto size = items.indefinite()
            ? std::optional<size_t> {}
            : std::optional<size_t> { items.special_uint() };
        auto &it = items.array();
        if (it.done()) [[unlikely]]
            throw error("Dijkstra guards must be nonempty when supplied");

        auto &first = it.read();
        if (first.type() == cbor::major_type::array) {
            credential_list credentials {};
            flat_set<credential_t> seen {};
            if (size) {
                credentials.reserve(*size);
                seen.reserve(*size);
            }
            auto add = [&](auto &item) {
                if (item.type() != cbor::major_type::array) [[unlikely]]
                    throw error("Dijkstra guard encodings cannot mix key hashes and credentials");
                auto credential = credential_t::from_cbor(item);
                if (!seen.emplace(credential).second) [[unlikely]]
                    throw error("duplicate Dijkstra guard credential");
                credentials.emplace_back(std::move(credential));
            };
            add(first);
            while (!it.done())
                add(it.read());
            return { std::move(credentials) };
        }

        key_set keys {};
        if (size)
            keys.reserve(*size);
        auto add = [&](auto &item) {
            if (item.type() != cbor::major_type::bytes) [[unlikely]]
                throw error("Dijkstra guard encodings cannot mix key hashes and credentials");
            const auto before = keys.size();
            keys.emplace_hint(keys.end(), item.bytes());
            if (keys.size() == before) [[unlikely]]
                throw error("duplicate Dijkstra guard key hash");
        };
        add(first);
        while (!it.done())
            add(it.read());
        return { std::move(keys) };
    }

    bool guards_t::empty() const
    {
        return std::visit([](const auto &items) { return items.empty(); }, value);
    }

    signer_set guards_t::key_hashes() const
    {
        signer_set res {};
        std::visit([&](const auto &items) {
            using T = std::decay_t<decltype(items)>;
            res.reserve(items.size());
            for (const auto &item: items) {
                if constexpr (std::is_same_v<T, key_set>) {
                    res.emplace_hint(res.end(), item);
                } else if (!item.script) {
                    res.emplace_hint(res.end(), item.hash);
                }
            }
        }, value);
        return res;
    }
}
