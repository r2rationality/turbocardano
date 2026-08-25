#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/auxiliary-data.hpp>
#include <turbo/cardano/babbage/block.hpp>

namespace turbo::cardano::conway {
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
    using proposal_procedure_list = vector_t<proposal_procedure_t>;
    using proposal_set = set_t<proposal_t>;

    typedef std::function<void(vote_info_t &&)> vote_observer_t;
    typedef std::function<void(proposal_t &&)> proposal_observer_t;

    struct voting_procedures_t: vote_set {
        using vote_set::vote_set;
        static voting_procedures_t from_cbor(cbor::zero2::value &);
    };

    struct proposal_procedures_t: proposal_procedure_list {
        using proposal_procedure_list::proposal_procedure_list;
        static proposal_procedures_t from_cbor(cbor::zero2::value &);
    };

    struct required_signers_t: alonzo::required_signers_t {
        using alonzo::required_signers_t::required_signers_t;
        static required_signers_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_inputs_t: shelley::transaction_inputs_t {
        using shelley::transaction_inputs_t::transaction_inputs_t;
        static transaction_inputs_t from_cbor(cbor::zero2::value &);
    };

    struct certificate_t {
        cardano::cert_t value {};

        static certificate_t from_cbor(cbor::zero2::value &);
    };

    struct script_t {
        script_info value;

        static script_t from_cbor(cbor::zero2::value &);
    };

    struct value_t {
        output_value_t value {};

        static value_t from_cbor(cbor::zero2::value &);
    };

    struct mint_t: mary::mint_t {
        using mary::mint_t::mint_t;
        static mint_t from_cbor(cbor::zero2::value &);
    };

    struct cost_models_t {
        plutus_cost_models value {};

        static cost_models_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_output_t {
        tx_out_data value {};

        static transaction_output_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_outputs_t {
        tx_output_list value {};

        static transaction_outputs_t from_cbor(cbor::zero2::value &);
    };

    struct certificates_t: shelley::certificates_t {
        using shelley::certificates_t::certificates_t;
        static certificates_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_body_t: babbage::transaction_body_t {
        voting_procedures_t votes {};
        proposal_set proposals {};
        std::optional<uint64_t> current_treasury {};
        std::optional<uint64_t> donation {};

        static transaction_body_t from_cbor(cbor::zero2::value &);
    };

    struct redeemer_t {
        tx_redeemer value {};

        static redeemer_t from_cbor(cbor::zero2::value &);
        static redeemer_t from_cbor(cbor::zero2::map_reader &);
    };

    struct redeemers_t: alonzo::redeemers_t {
        static redeemers_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_witness_set_t: babbage::transaction_witness_set_t {
        static transaction_witness_set_t from_cbor(cbor::zero2::value &);
    };

    struct transaction_t {
        transaction_body_t body {};
        transaction_witness_set_t witnesses {};
        bool valid = true;
        std::optional<auxiliary_data_t> auxiliary_data {};

        static transaction_t from_cbor(cbor::zero2::value &);
    };

    struct tx_base: babbage::tx_base {
        using babbage::tx_base::tx_base;
        virtual const vote_set &votes() const =0;
        virtual const proposal_set &proposals() const =0;
        virtual std::optional<uint64_t> current_treasury() const =0;
        void parse_witnesses(cbor::zero2::value &) override;
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
        transaction_body_t _body;
    };
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
