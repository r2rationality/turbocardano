#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano::mary {
    struct tx;

    struct block_header_base: shelley::block_header_base {
        using shelley::block_header_base::block_header_base;
    };

    struct block_header: shelley::block_header {
        using shelley::block_header::block_header;
    };

    struct block_base: shelley::block_base {
        using shelley::block_base::block_base;
    };

    struct tx_base: shelley::tx_base {
        using shelley::tx_base::tx_base;
        virtual const multi_mint_map &mints() const =0;
        size_t foreach_mint(const mint_observer_t &) const override;
    protected:
        static multi_mint_map parse_mints(cbor::zero2::value &v);
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
        buffer _raw;
        mutable std::optional<tx_hash> _hash {};
    };

    struct block: block_base {
        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &block, const cardano::config &cfg);
        uint32_t body_size() const override;
        const cardano::block_header_base &header() const override;
        const block_hash &body_hash() const override;
        const tx_list &txs() const override;
    private:
        block_header _hdr;
        block_tx_list<tx> _txs;
        block_meta_map _meta;
        mutable std::optional<block_hash> _body_hash {};
        const buffer _raw;

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &block, const cardano::config &cfg);
    };
}