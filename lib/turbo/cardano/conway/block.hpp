#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>

namespace turbo::cardano::conway {
    struct block_header_base: babbage::block_header_base {
    };

    struct block_header: babbage::block_header {
        using babbage::block_header::block_header;
    };

    struct block_base: babbage::block_base {
        using babbage::block_base::block_base;
    };

    struct vote_info_t {
        voter_t voter {};
        gov_action_id_t action_id {};
        voting_procedure_t voting_procedure {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.voter, self.action_id, self.voting_procedure);
        }

        std::strong_ordering operator<=>(const vote_info_t &o) const
        {
            const auto cmp = voter <=> o.voter;
            if (cmp != std::strong_ordering::equal)
                return cmp;
            return action_id <=> o.action_id;
        }
    };

    using vote_set = set_t<vote_info_t>;
    using proposal_procedure_set = set_t<proposal_procedure_t>;
    using proposal_set = set_t<proposal_t>;

    typedef std::function<void(vote_info_t &&)> vote_observer_t;
    typedef std::function<void(proposal_t &&)> proposal_observer_t;

    struct tx_base: babbage::tx_base {
        using babbage::tx_base::tx_base;
        void foreach_set(cbor::zero2::value &set_raw, const set_observer_t &observer) const override;
        virtual const vote_set &votes() const =0;
        virtual const proposal_set &proposals() const =0;
        virtual std::optional<uint64_t> current_treasury() const =0;
        void parse_witnesses(cbor::zero2::value &) override;
    protected:
        static vote_set parse_votes(cbor::zero2::value &);
        static proposal_procedure_set parse_proposals(cbor::zero2::value &);
        void parse_redeemers(cbor::zero2::value &v) override;
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
        const input_set &ref_inputs() const override;
        const std::optional<tx_output> &collateral_return() const override;
        const std::optional<uint64_t> &collateral_value() const override;
        uint64_t donation() const override;
        const vote_set &votes() const override;
        const proposal_set &proposals() const override;
        std::optional<uint64_t> current_treasury() const override;
    private:
        input_set _inputs {};
        tx_output_list _outputs {};
        uint64_t _fee;
        std::optional<uint64_t> _validity_end;
        cert_list _certs {};
        withdrawal_map _withdrawals {};
        param_update_proposal_list _updates {}; // not really used but keep the structure to efficiently return an empty list
        std::optional<uint64_t> _validity_start {};
        multi_mint_map _mints {};
        signer_set _required_signers {};
        input_set _collateral_inputs {};
        input_set _ref_inputs {};
        std::optional<tx_output> _collateral_return {};
        std::optional<uint64_t> _collateral_value {};
        std::optional<uint64_t> _current_treasury {};
        std::optional<uint64_t> _donation {};
        vote_set _votes {};
        proposal_set _proposals {};
        buffer _raw;
        mutable std::optional<tx_hash> _hash {};
    };

    struct block: block_base {
        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::value &block_tuple, const cardano::config &cfg);
        uint32_t body_size() const override;
        const cardano::block_header_base &header() const override;
        const block_hash &body_hash() const override;
        const tx_list &txs() const override;
        const invalid_tx_set &invalid_txs() const override;
    private:
        babbage::block_header _hdr;
        block_tx_list<tx> _txs;
        block_meta_map _meta;
        invalid_tx_set _invalid_txs;
        mutable std::optional<block_hash> _body_hash {};
        const buffer _raw;

        block(uint64_t era, uint64_t offset, uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &block_tuple, const cardano::config &cfg);
    };

    extern void protocol_params_to_cbor(era_encoder &enc, const protocol_params &params);
}

namespace fmt {
    template<>
    struct formatter<turbo::cardano::conway::vote_info_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::cardano::conway::vote_info_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "action_id: {} voter: {} voting_procedure: {}", v.action_id, v.voter,v.voting_procedure);
        }
    };
}