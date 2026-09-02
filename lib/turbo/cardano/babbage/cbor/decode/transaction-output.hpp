#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>

namespace turbo::cardano::babbage::detail {
    template<typename SCRIPT_DECODER>
    script_info script_ref_from_cbor(cbor::zero2::value &v, SCRIPT_DECODER &&decode_script)
    {
        auto &tag = v.tag();
        if (tag.id() != 24) [[unlikely]]
            throw error(fmt::format("expected a tag with id 24 but got: {}", tag.id()));
        auto &script_bytes = tag.read();
        uint8_vector storage {};
        buffer script_data {};
        if (script_bytes.indefinite()) {
            script_bytes.to_bytes(storage);
            script_data = storage;
        } else {
            script_data = script_bytes.bytes();
        }
        cbor::zero2::decoder dec { script_data };
        auto script = decode_script(dec.read());
        if (!dec.done()) [[unlikely]]
            throw error("script_ref contains more than one CBOR value");
        return script;
    }

    template<typename VALUE_DECODER, typename SCRIPT_DECODER>
    tx_out_data transaction_output_map_from_cbor(
        cbor::zero2::value &v,
        VALUE_DECODER &&decode_value,
        SCRIPT_DECODER &&decode_script)
    {
        auto &it = v.map();
        std::optional<uint8_vector> address {};
        std::optional<output_value_t> value {};
        std::optional<datum_option_t> datum {};
        std::optional<script_info> script_ref {};
        uint8_t seen = 0;
        while (!it.done()) {
            auto &key = it.read_key();
            const auto id = key.uint();
            if (id > 3) [[unlikely]]
                throw error(fmt::format("unsupported transaction_output key: {}", id));
            const auto mask = static_cast<uint8_t>(1U << id);
            if (seen & mask) [[unlikely]]
                throw error(fmt::format("duplicate transaction_output key: {}", id));
            seen |= mask;
            auto &item = it.read_val(std::move(key));
            switch (id) {
                case 0: address.emplace(item.bytes()); break;
                case 1: value.emplace(decode_value(item)); break;
                case 2: datum.emplace(datum_option_t::from_cbor(item)); break;
                case 3: script_ref.emplace(script_ref_from_cbor(item, decode_script)); break;
                default: std::unreachable();
            }
        }
        if (!address) [[unlikely]]
            throw error("transaction_output is missing its address");
        if (!value) [[unlikely]]
            throw error("transaction_output is missing its value");
        return { std::move(*address), value->coin, std::move(value->assets), std::move(datum), std::move(script_ref) };
    }
}
