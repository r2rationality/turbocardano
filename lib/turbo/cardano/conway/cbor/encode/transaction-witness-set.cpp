/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/conway/cbor/encode/transaction-witness-set.hpp>
#include <turbo/cardano/conway/transaction.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cardano::conway {
    namespace {
        void plutus_data_to_cbor(
            era_encoder &enc, plutus::allocator &alloc, const buffer raw)
        {
            plutus::data::from_cbor(alloc, raw).to_cbor(enc);
        }

        void native_script_to_cbor(era_encoder &enc, const buffer raw)
        {
            const auto script = allegra::native_script_t::from_cbor(raw);
            script.to_cbor(enc);
        }

        void redeemer_map_to_cbor(
            era_encoder &enc, plutus::allocator &alloc, const tx_redeemer_map &redeemers)
        {
            if (redeemers.empty()) [[unlikely]]
                throw error("Conway redeemers must be nonempty");
            enc.map_compact(redeemers.size(), [&] {
                for (const auto &[id, redeemer]: redeemers) {
                    enc.array(2).uint(static_cast<uint8_t>(id.tag)).uint(id.ref_idx);
                    enc.array(2);
                    plutus_data_to_cbor(enc, alloc, redeemer.data);
                    redeemer.budget.to_cbor(enc);
                }
            });
        }
    }

    void redeemer_t::to_cbor(era_encoder &enc) const
    {
        plutus::allocator alloc {};
        enc.array(4)
            .uint(static_cast<uint8_t>(value.tag))
            .uint(value.ref_idx);
        plutus_data_to_cbor(enc, alloc, value.data);
        value.budget.to_cbor(enc);
    }

    void redeemers_t::to_cbor(era_encoder &enc) const
    {
        plutus::allocator alloc {};
        redeemer_map_to_cbor(enc, alloc, items);
    }

    void detail::transaction_witness_set_to_cbor(
        era_encoder &enc, const tx_wit_list &items,
        const tx_redeemer_map &redeemers)
    {
        std::array<size_t, 8> counts {};
        const auto field = [](const tx_wit &witness) -> uint8_t {
            return std::visit([](const auto &value) -> uint8_t {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, tx_wit_shelley_vkey>) return 0;
                if constexpr (std::is_same_v<T, tx_wit_shelley_bootstrap>) return 2;
                if constexpr (std::is_same_v<T, tx_wit_datum>) return 4;
                if constexpr (std::is_same_v<T, script_info>) {
                    switch (value.type()) {
                        case script_type::native: return 1;
                        case script_type::plutus_v1: return 3;
                        case script_type::plutus_v2: return 6;
                        case script_type::plutus_v3: return 7;
                        [[unlikely]] default:
                            throw error(fmt::format(
                                "script type {} cannot appear in a Conway witness set",
                                static_cast<uint8_t>(value.type())));
                    }
                }
                throw error(fmt::format(
                    "witness type cannot appear in Conway: {}", typeid(T).name()));
            }, witness);
        };
        for (const auto &witness: items)
            ++counts[field(witness)];

        std::optional<plutus::allocator> alloc {};
        if (counts[4] || !redeemers.empty())
            alloc.emplace();

        const size_t map_size = std::count_if(
            counts.begin(), counts.end(), [](const auto count) { return count; })
            + !redeemers.empty();
        enc.map_compact(map_size, [&] {
            static constexpr std::array<uint8_t, 8> field_order { 0, 2, 1, 3, 6, 7, 4, 5 };
            for (const auto key: field_order) {
                if (key == 5 && !redeemers.empty()) {
                    enc.uint(5);
                    redeemer_map_to_cbor(enc, *alloc, redeemers);
                }
                if (!counts[key])
                    continue;
                enc.uint(key).tag(258);
                enc.array_compact(counts[key], [&] {
                    for (const auto &witness: items) {
                        if (field(witness) != key)
                            continue;
                        std::visit([&](const auto &value) {
                            using T = std::decay_t<decltype(value)>;
                            if constexpr (std::is_same_v<T, tx_wit_shelley_vkey>) {
                                enc.array(2).bytes(value.vkey).bytes(value.sig);
                            } else if constexpr (std::is_same_v<T, tx_wit_shelley_bootstrap>) {
                                enc.array(4).bytes(value.vkey).bytes(value.sig)
                                    .bytes(value.chain_code).bytes(value.attrs);
                            } else if constexpr (std::is_same_v<T, tx_wit_datum>) {
                                plutus_data_to_cbor(enc, *alloc, value.data);
                            } else if constexpr (std::is_same_v<T, script_info>) {
                                if (value.type() == script_type::native)
                                    native_script_to_cbor(enc, value.script());
                                else
                                    enc.bytes(value.script());
                            }
                        }, witness);
                    }
                });
            }
        });
    }

    void transaction_witness_set_t::to_cbor(era_encoder &enc) const
    {
        detail::transaction_witness_set_to_cbor(enc, items, redeemers.items);
    }
}
