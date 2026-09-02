/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>
#include <turbo/cardano/common/cbor/decode/script.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        struct vkey_witness_id {
            crypto::ed25519::vkey vkey {};
            crypto::ed25519::signature sig {};

            auto operator<=>(const vkey_witness_id &) const =default;
        };

        cbor::zero2::value &set_items(cbor::zero2::value &v)
        {
            if (v.type() == cbor::major_type::array)
                return v;
            auto &tag = v.tag();
            if (tag.id() != 258) [[unlikely]]
                throw error(fmt::format("expected witness set tag 258 but got: {}", tag.id()));
            return tag.read();
        }

        template<typename ID, typename DECODE>
        void decode_set(transaction_witness_set_t &res, cbor::zero2::value &v, DECODE decode)
        {
            auto &items = set_items(v);
            flat_set<ID> seen {};
            if (!items.indefinite()) [[likely]] {
                const auto size = items.special_uint();
                seen.reserve(size);
                res.items.reserve(res.items.size() + size);
            }
            auto &it = items.array();
            while (!it.done()) {
                auto &item = it.read();
                auto id = decode(item);
                if (!seen.emplace(std::move(id)).second) [[unlikely]]
                    throw error("duplicate Dijkstra transaction witness");
            }
            if (seen.empty()) [[unlikely]]
                throw error("Dijkstra witness sets must be nonempty when supplied");
        }

        void decode_bootstrap(transaction_witness_set_t &res, cbor::zero2::value &v)
        {
            decode_set<crypto::ed25519::vkey>(res, v, [&](auto &item) {
                auto witness = tx_wit_shelley_bootstrap::from_cbor(item);
                static_cast<void>(byte_array<32> { witness.chain_code });
                const auto id = witness.vkey;
                res.items.emplace_back(std::move(witness));
                return id;
            });
        }

        template<script_type TYPE>
        void decode_scripts(transaction_witness_set_t &res, cbor::zero2::value &v)
        {
            auto decode = [&](auto &script) {
                auto info = ::turbo::cardano::detail::script_info_from_cbor(
                    TYPE, script, native_script_t::validate_cbor);
                if constexpr (TYPE == script_type::native) {
                    const auto raw = script.data_raw();
                    res.items.emplace_back(std::move(info));
                    return raw;
                } else {
                    const auto hash = info.hash();
                    res.items.emplace_back(std::move(info));
                    return hash;
                }
            };
            if constexpr (TYPE == script_type::native)
                decode_set<buffer>(res, v, decode);
            else
                decode_set<script_hash>(res, v, decode);
        }
    }

    transaction_witness_set_t transaction_witness_set_t::from_cbor(cbor::zero2::value &v)
    {
        transaction_witness_set_t res {};
        uint16_t seen_types = 0;
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto type = key.uint();
            if (type > 7) [[unlikely]]
                throw error(fmt::format("unsupported Dijkstra transaction_witness_set key: {}", type));
            const auto mask = static_cast<uint16_t>(1U << type);
            if (seen_types & mask) [[unlikely]]
                throw error(fmt::format("duplicate Dijkstra transaction_witness_set key: {}", type));
            seen_types |= mask;
            auto &value = it.read_val(std::move(key));
            switch (type) {
                case 0:
                    decode_set<vkey_witness_id>(res, value, [&](auto &item) {
                        auto witness = tx_wit_shelley_vkey::from_cbor(item);
                        const vkey_witness_id id { witness.vkey, witness.sig };
                        res.items.emplace_back(std::move(witness));
                        return id;
                    });
                    break;
                case 1: decode_scripts<script_type::native>(res, value); break;
                case 2: decode_bootstrap(res, value); break;
                case 3: decode_scripts<script_type::plutus_v1>(res, value); break;
                case 4:
                    decode_set<buffer>(res, value, [&](auto &item) {
                        res.items.emplace_back(tx_wit_datum::from_cbor(item));
                        return item.data_raw();
                    });
                    break;
                case 5: res.redeemers = redeemers_t::from_cbor(value); break;
                case 6: decode_scripts<script_type::plutus_v2>(res, value); break;
                case 7: decode_scripts<script_type::plutus_v3>(res, value); break;
                default: std::unreachable();
            }
        }
        res.raw = v.data_raw();
        return res;
    }
}
