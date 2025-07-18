#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/mary/block.hpp>

namespace turbo::cardano::alonzo {
    struct tx;

    struct block_header_base: mary::block_header_base {
        using mary::block_header_base::block_header_base;
    };

    struct block_header: mary::block_header {
        using mary::block_header::block_header;
    };

    struct block_base: mary::block_base {
        using mary::block_base::block_base;
        static block_hash compute_body_hash(const buffer &txs_raw, const buffer &wits_raw, const buffer &meta_raw, const buffer &invalid_raw);
    };

    struct tx_base: mary::tx_base {
        using mary::tx_base::tx_base;
        virtual const signer_set &required_signers() const =0;
        virtual const input_set &collateral_inputs() const =0;
        void foreach_collateral(const std::function<void(const tx_input &)> &observer) const override;
        void foreach_required_signer(const signer_observer_t &observer) const override;
        void parse_witnesses(cbor::zero2::value &) override;
    protected:
        signer_set parse_signers(cbor::zero2::value &);
        virtual void parse_redeemers(cbor::zero2::value &v);
    };

    struct tx: tx_base {
        tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx_raw, size_t idx=0, bool invalid=false);
        const tx_hash &hash() const override;
        const input_set &inputs() const override;
        const tx_output_list &outputs() const override;
        uint64_t fee() const override;
        std::optional<uint64_t> validity_end() const override;
        const withdrawal_map &withdrawals() const override;
        const cert_list &certs() const override;
        const param_update_proposal_list &updates() const override;
        buffer raw() const override;
        const multi_mint_map &mints() const override;
        std::optional<uint64_t> validity_start() const override;
        const signer_set &required_signers() const override;
        const input_set &collateral_inputs() const override;
    private:
        input_set _inputs {};
        tx_output_list _outputs {};
        uint64_t _fee;
        std::optional<uint64_t> _validity_end;
        cert_list _certs {};
        withdrawal_map _withdrawals {};
        param_update_proposal_list _updates {};
        std::optional<uint64_t> _validity_start {};
        multi_mint_map _mints {};
        signer_set _required_signers {};
        input_set _collateral_inputs {};
        buffer _raw;
        mutable std::optional<tx_hash> _hash {};
    };

    struct block: block_base {
        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &block, const cardano::config &cfg);
        uint32_t body_size() const override;
        const cardano::block_header_base &header() const override;
        const block_hash &body_hash() const override;
        const cardano::tx_list &txs() const override;
        const invalid_tx_set &invalid_txs() const override;
    private:
        block_header _hdr;
        block_tx_list<tx> _txs;
        block_meta_map _meta;
        invalid_tx_set _invalid_txs;
        mutable std::optional<block_hash> _body_hash {};
        const buffer _raw;

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg);
    };
}