/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/metadata.hpp>
#include <boost/container/small_vector.hpp>
#include <utfcpp/utf8.h>

namespace turbo::cardano::shelley {
    metadatum_t::~metadatum_t()
    {
        const auto has_children = [](const value_type &v) {
            if (const auto *items = std::get_if<array_t>(&v))
                return !items->empty();
            if (const auto *items = std::get_if<map_t>(&v))
                return !items->empty();
            return false;
        };
        if (!has_children(value))
            return;
        boost::container::small_vector<value_type, 8> pending;
        pending.emplace_back(std::move(value));
        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            // Detach child containers before destroying their owning vector.
            const auto detach = [&](metadatum_t &child) {
                if (has_children(child.value))
                    pending.emplace_back(std::move(child.value));
            };
            if (auto *items = std::get_if<array_t>(&current)) {
                for (auto &child: *items)
                    detach(child);
            } else if (auto *items = std::get_if<map_t>(&current)) {
                for (auto &[key, child]: *items) {
                    detach(key);
                    detach(child);
                }
            }
        }
    }

    metadatum_t metadatum_t::from_cbor(cbor::zero2::value &root)
    {
        struct frame {
            cbor::zero2::value *source;
            metadatum_t *target;
            cbor::zero2::value *key = nullptr;
        };
        boost::container::small_vector<frame, 16> pending;
        const auto read = [&](cbor::zero2::value &v, metadatum_t &target) {
            switch (v.type()) {
                case cbor::major_type::array: {
                    array_t items{};
                    if (!v.indefinite()) [[likely]]
                        items.reserve(v.special_uint());
                    target.value = std::move(items);
                    pending.push_back({&v, &target});
                    return;
                }
                case cbor::major_type::map: {
                    map_t items{};
                    if (!v.indefinite()) [[likely]]
                        items.reserve(v.special_uint());
                    target.value = std::move(items);
                    pending.push_back({&v, &target});
                    return;
                }
                case cbor::major_type::nint:
                    target.value = nint64_t{v.nint_raw()};
                    return;
                case cbor::major_type::uint:
                    target.value = v.uint();
                    return;
                case cbor::major_type::bytes: {
                    uint8_vector bytes{};
                    v.to_bytes(bytes);
                    if (bytes.size() > 64) [[unlikely]]
                        throw error{"metadata byte strings must not exceed 64 bytes but got: {}", bytes.size()};
                    target.value = std::move(bytes);
                    return;
                }
                case cbor::major_type::text: {
                    std::string text{};
                    v.to_text(text);
                    if (utf8::find_invalid(text.begin(), text.end()) != text.end()) [[unlikely]]
                        throw error("metadata text contains invalid UTF-8");
                    if (text.size() > 64) [[unlikely]]
                        throw error{"metadata text strings must not exceed 64 bytes but got: {}", text.size()};
                    target.value = std::move(text);
                    return;
                }
                [[unlikely]] default:
                    throw error{"unsupported metadatum value type: {}", v.type()};
            }
        };
        metadatum_t result{uint64_t{0}};
        read(root, result);
        while (!pending.empty()) {
            auto &f = pending.back();
            if (auto *items = std::get_if<array_t>(&f.target->value)) {
                auto &it = f.source->array();
                if (it.done()) {
                    static_cast<void>(f.source->data_raw());
                    pending.pop_back();
                } else {
                    items->emplace_back(uint64_t{0});
                    read(it.read(), items->back());
                }
            } else {
                auto &entries = std::get<map_t>(f.target->value);
                auto &it = f.source->map();
                if (f.key) {
                    auto &key = *f.key;
                    f.key = nullptr;
                    read(it.read_val(std::move(key)), entries.back().second);
                } else if (it.done()) {
                    static_cast<void>(f.source->data_raw());
                    pending.pop_back();
                } else {
                    entries.emplace_back(metadatum_t{uint64_t{0}}, metadatum_t{uint64_t{0}});
                    f.key = &it.read_key();
                    read(*f.key, entries.back().first);
                }
            }
        }
        return result;
    }

    metadata_t metadata_t::from_cbor(cbor::zero2::value &v)
    {
        metadata_t res {};
        if (!v.indefinite()) [[likely]]
            res.dict.reserve(v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            auto &key_v = it.read_key();
            const auto key = key_v.uint();
            auto &value_v = it.read_val(std::move(key_v));
            const auto before = res.dict.size();
            res.dict.emplace_hint(res.dict.end(), key, metadatum_t::from_cbor(value_v));
            if (res.dict.size() == before) [[unlikely]]
                throw error("duplicate metadata label");
        }
        return res;
    }
}
