/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/metadata.hpp>
#include <utfcpp/utf8.h>

namespace turbo::cardano::shelley {
    metadatum_t metadatum_t::from_cbor(cbor::zero2::value &v)
    {
        switch (v.type()) {
            case cbor::major_type::array: {
                array_t items {};
                if (!v.indefinite()) [[likely]]
                    items.reserve(v.special_uint());
                auto &it = v.array();
                while (!it.done())
                    items.emplace_back(from_cbor(it.read()));
                return metadatum_t { std::move(items) };
            }
            case cbor::major_type::map: {
                map_t items {};
                if (!v.indefinite()) [[likely]]
                    items.reserve(v.special_uint());
                auto &it = v.map();
                while (!it.done()) {
                    auto &key_v = it.read_key();
                    auto key = from_cbor(key_v);
                    auto &value_v = it.read_val(std::move(key_v));
                    items.emplace_back(std::move(key), from_cbor(value_v));
                }
                return metadatum_t { std::move(items) };
            }
            case cbor::major_type::nint: {
                const auto magnitude = v.nint();
                if (magnitude > uint64_t { 1 } << 63) [[unlikely]]
                    throw error("metadata negative integer is smaller than min_int64");
                const auto value = magnitude == uint64_t { 1 } << 63
                    ? std::numeric_limits<int64_t>::min()
                    : -numeric_cast<int64_t>(magnitude);
                return metadatum_t { metadatum_t::value_type { std::in_place_type<int64_t>, value } };
            }
            case cbor::major_type::uint:
                return metadatum_t { metadatum_t::value_type { std::in_place_type<nint64_t>, v.uint() } };
            case cbor::major_type::bytes: {
                uint8_vector bytes {};
                v.to_bytes(bytes);
                if (bytes.size() > 64) [[unlikely]]
                    throw error(fmt::format("metadata byte strings must not exceed 64 bytes but got: {}", bytes.size()));
                return metadatum_t { std::move(bytes) };
            }
            case cbor::major_type::text: {
                std::string text {};
                v.to_text(text);
                if (utf8::find_invalid(text.begin(), text.end()) != text.end()) [[unlikely]]
                    throw error("metadata text contains invalid UTF-8");
                if (text.size() > 64) [[unlikely]]
                    throw error(fmt::format("metadata text strings must not exceed 64 bytes but got: {}", text.size()));
                return metadatum_t { std::move(text) };
            }
            [[unlikely]] default:
                throw error(fmt::format("unsupported metadatum value type: {}", v.type()));
        }
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
