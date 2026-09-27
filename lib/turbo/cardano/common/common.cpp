/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>
#include <turbo/cardano/common/common.hpp>
#include <turbo/cardano/common/native-script.hpp>
#include <turbo/crypto/crc32.hpp>
#include <turbo/plutus/context.hpp>

namespace turbo::cardano {

    block_info block_info::from_block(const cardano::block_container &blk)
    {
        return {
            blk->hash(), blk.offset(), blk.size(),
            numeric_cast<uint32_t>(blk->slot()),
            numeric_cast<uint32_t>(blk->height ()),
            crypto::crc32::digest(blk.raw()),
            blk->issuer_hash(),
            numeric_cast<uint16_t>(blk->header().size()),
            numeric_cast<uint8_t>(blk->header_offset()),
            numeric_cast<uint8_t>(blk->era())
        };
    }

    void tx_base::foreach_witness_byron_vkey(const byron_vkey_wit_observer_t &observer) const
    {
        for (const auto &w: _wits) {
            std::visit([&](const auto &bwit) {
                using T = std::decay_t<decltype(bwit)>;
                if constexpr (std::is_same_v<T, tx_wit_byron_vkey> || std::is_same_v<T, tx_wit_byron_redeemer>) {
                    observer(bwit);
                }
            }, w);
        }
    }

    void tx_base::foreach_witness_shelley_bootstrap(const shelley_bootstrap_observer_t &observer) const
    {
        for (const auto &w: _wits) {
            if (std::holds_alternative<tx_wit_shelley_bootstrap>(w))
                observer(std::get<tx_wit_shelley_bootstrap>(w));
        }
    }

    void tx_base::foreach_cert(const cert_observer_t &observer) const
    {
        for (const auto &c: certs())
            observer(c);
    }

    void tx_base::foreach_input(const input_observer_t &observer) const
    {
        for (const auto &txi: inputs())
            observer(txi);
    }

    void tx_base::foreach_output(const output_observer_t &observer) const
    {
        for (const auto &txo: outputs())
            observer(txo);
    }

    void tx_base::foreach_witness(const witness_observer_t &observer) const
    {
        for (const auto &wit: witnesses())
            observer(wit);
    }

    void tx_base::foreach_witness_shelley_vkey(const shelley_vkey_observer_t &observer) const
    {
        foreach_witness([&](const auto &wit) {
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, tx_wit_shelley_vkey>) {
                    observer(v);
                }
            }, wit);
        });
    }

    void tx_base::foreach_script(const script_observer_t &observer, const plutus::context *ctx) const
    {
        foreach_witness([&](const auto &wit) {
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, script_info>) {
                    observer(v);
                }
            }, wit);
        });
        if (ctx) {
            for (const auto &txo: ctx->inputs()) {
                if (txo.data.script_ref)
                    observer(*txo.data.script_ref);
            }
            for (const auto &txo: ctx->ref_inputs()) {
                if (txo.data.script_ref)
                    observer(*txo.data.script_ref);
            }
        }
    }

    void tx_base::foreach_datum(const datum_observer_t &observer) const
    {
        foreach_witness([&](const auto &wit) {
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, tx_wit_datum>) {
                    observer(v);
                }
            }, wit);
        });
    }

    void tx_base::foreach_redeemer(const redeemer_observer_t &observer) const
    {
        for (const auto &[_, redeemer]: redeemers())
            observer(redeemer);
    }

#include <turbo/cardano/ledger/rules/utxow/witnesses.ipp>

#include <turbo/cardano/ledger/rules/utxos/witnesses.ipp>

    json::object tx_base::to_json(const tail_relative_stake_map &tail_relative_stake) const
    {
        json::array inputs {};
        foreach_input([&](const auto &tx_in) {
            inputs.emplace_back(tx_in.to_json());
        });
        json::array outputs {};
        foreach_output([&](const auto &tx_out) {
            outputs.emplace_back(tx_out.to_json());
        });
        return json::object {
                { "hash", fmt::format("{}", hash()) },
                { "offset", offset() },
                { "size", size() },
                { "slot", block().slot_object().to_json() },
                { "fee", fmt::format("{}", fee()) },
                { "inputs", std::move(inputs) },
                { "outputs", std::move(outputs) },
                { "relativeStake", slot_relative_stake(tail_relative_stake, block().slot()) }
        };
    }

    json::object multi_balance::to_json(const size_t offset, const size_t max_items) const
    {
        const auto end_offset = std::min(offset + max_items, size());
        json::object j {};
        size_t i = 0;
        for (const auto &[asset_name, amount]: *this) {
            if (i >= offset)
                j.emplace(asset_name, amount);
            if (++i >= end_offset)
                break;
        }
        return j;
    }

    size_t block_base::tx_count() const
    {
        return txs().size();
    }

    void block_base::foreach_tx(const tx_observer_t &observer) const
    {
        for (const auto &t: txs()) {
            if (!t->invalid()) [[likely]]
                observer(*t);
        }
    }

    void block_base::foreach_invalid_tx(const tx_observer_t &observer) const
    {
        for (const auto &tx_idx: invalid_txs()) {
                observer(*txs().at(tx_idx));
        }
    }

    bool block_kes_signature::verify() const
    {
        byte_array<sizeof(vkey) + 2 * 8> ocert_data {};
        if (vkey_hot.size() != sizeof(vkey)) [[unlikely]]
            throw error("vkey size mismatch!");
        memcpy(ocert_data.data(), vkey_hot.data(), sizeof(vkey));
        const uint64_t ctr = host_to_net<uint64_t>(counter);
        memcpy(ocert_data.data() + sizeof(vkey), &ctr, 8);
        const uint64_t kp = host_to_net<uint64_t>(period);
        memcpy(ocert_data.data() + sizeof(vkey) + 8, &kp, 8);
        if (!crypto::ed25519::verify(vkey_sig, vkey_cold, ocert_data)) [[unlikely]] {
            logger::debug("an operational certificate has failed verification for issuer: {}", vkey_cold);
            return false;
        }
        if (!period_slots) [[unlikely]]
            throw error("slotsPerKESPeriod must be positive");
        const uint64_t block_period = slot / period_slots;
        if (period > block_period) [[unlikely]]
            throw error(fmt::format("KES period {} is greater than the current period {}", period, block_period));
        const uint64_t t = block_period - period;
        if (t >= max_evolutions) [[unlikely]]
            throw error(fmt::format("KES evolution {} exceeds the certificate limit {}", t, max_evolutions));
        const kes_signature kes_sig { sig };
        if (!kes_sig.verify(t, vkey_hot.first<32>(), header_body)) [[unlikely]] {
            logger::debug("a KES signature has failed verification for issuer: {}", vkey_cold);
            return false;
        }
        return true;
    }

}
