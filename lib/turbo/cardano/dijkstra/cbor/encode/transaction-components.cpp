/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>
#include <turbo/cardano/babbage/cbor/encode/transaction-output.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        void native_script_to_cbor(era_encoder &enc, const buffer raw)
        {
            native_script_t::from_cbor(raw).to_cbor(enc);
        }

        void transaction_output_to_cbor(
            era_encoder &enc, plutus::allocator &alloc, const tx_out_data &output)
        {
            babbage::detail::transaction_output_to_cbor_semantic(
                enc, output, alloc, native_script_to_cbor);
        }
    }

    void value_t::to_cbor(era_encoder &enc) const
    {
        if (value.assets.empty()) {
            enc.uint(value.coin);
        } else {
            enc.array(2).uint(value.coin);
            value.assets.to_cbor(enc);
        }
    }

    void mint_t::to_cbor(era_encoder &enc) const
    {
        if (empty()) [[unlikely]]
            throw error("Dijkstra mint maps must be nonempty");
        enc.map_compact(size(), [&] {
            for (const auto &[policy, assets]: *this) {
                if (assets.empty()) [[unlikely]]
                    throw error("Dijkstra mint policy maps must be nonempty");
                enc.bytes(policy);
                enc.map_compact(assets.size(), [&] {
                    for (const auto &[name, amount]: assets) {
                        if (!amount) [[unlikely]]
                            throw error("Dijkstra mint amounts must be nonzero");
                        name.to_cbor(enc);
                        if (amount > 0)
                            enc.uint(numeric_cast<uint64_t>(amount));
                        else
                            enc.nint(numeric_cast<uint64_t>(-(amount + 1)));
                    }
                });
            }
        });
    }

    void script_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2).uint(static_cast<uint8_t>(value.type()));
        if (value.type() == script_type::native) {
            const auto script = native_script_t::from_cbor(value.script());
            script.to_cbor(enc);
        } else {
            enc.bytes(value.script());
        }
    }

    void transaction_output_t::to_cbor(era_encoder &enc) const
    {
        plutus::allocator alloc {};
        transaction_output_to_cbor(enc, alloc, value);
    }

    void transaction_outputs_t::to_cbor(era_encoder &enc) const
    {
        plutus::allocator alloc {};
        enc.array_compact(value.size(), [&] {
            for (const auto &output: value)
                transaction_output_to_cbor(enc, alloc, output);
        });
    }

    void direct_deposits_t::to_cbor(era_encoder &enc) const
    {
        if (empty()) [[unlikely]]
            throw error("Dijkstra direct deposits must be nonempty");
        enc.map_compact(size(), [&] {
            for (const auto &[account, amount]: *this)
                enc.bytes(account).uint(amount);
        });
    }

    void account_balance_interval_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &interval) {
            using T = std::decay_t<decltype(interval)>;
            if constexpr (std::is_same_v<T, uint64_t>) {
                enc.uint(interval);
            } else {
                if (!interval.lower && !interval.upper) [[unlikely]]
                    throw error("both account balance interval bounds cannot be nil");
                enc.array(2);
                if (interval.lower)
                    enc.uint(*interval.lower);
                else
                    enc.s_null();
                if (interval.upper)
                    enc.uint(*interval.upper);
                else
                    enc.s_null();
            }
        }, value);
    }

    void guards_t::to_cbor(era_encoder &enc) const
    {
        if (empty()) [[unlikely]]
            throw error("Dijkstra guards must be nonempty");
        enc.tag(258);
        std::visit([&](const auto &items) {
            enc.array_compact(items.size(), [&] {
                for (const auto &item: items) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, key_hash>)
                        enc.bytes(item);
                    else
                        item.to_cbor(enc);
                }
            });
        }, value);
    }
}
