#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>
#include <turbo/cardano/dijkstra/auxiliary-data.hpp>

namespace turbo::plutus {
    struct allocator;
}

namespace turbo::cardano::dijkstra {
    using transaction_inputs_t = conway::transaction_inputs_t;
    using voting_procedures_t = conway::voting_procedures_t;

    struct value_t {
        output_value_t value {};

        static value_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct mint_t: conway::mint_t {
        using conway::mint_t::mint_t;
        static mint_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct native_script_t {
        enum class type_t: uint8_t {
            signature = 0,
            all = 1,
            any = 2,
            at_least = 3,
            invalid_before = 4,
            invalid_hereafter = 5,
            credential = 6
        };

        type_t type {};
        key_hash key {};
        int64_t required = 0;
        std::vector<native_script_t> scripts {};
        uint64_t slot = 0;
        credential_t required_credential {};

        static native_script_t from_cbor(cbor::zero2::value &);
        static native_script_t from_cbor(buffer);
        static void validate_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct script_t {
        script_info value;

        static script_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct transaction_output_t {
        tx_out_data value {};

        static transaction_output_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct transaction_outputs_t {
        tx_output_list value {};

        static transaction_outputs_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct certificate_t {
        cardano::cert_t value {};

        static certificate_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct certificates_t: shelley::certificates_t {
        using shelley::certificates_t::certificates_t;

        static certificates_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct protocol_param_update_t {
        cardano::param_update_t value {};

        static protocol_param_update_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct governance_action_t {
        cardano::gov_action_t value {};

        static governance_action_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct proposal_procedures_t: conway::proposal_procedure_list {
        using conway::proposal_procedure_list::proposal_procedure_list;

        static proposal_procedures_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct plutus_data_t {
        uint8_vector raw {};

        static plutus_data_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
        void to_cbor(era_encoder &, plutus::allocator &) const;

        bool operator==(const plutus_data_t &) const =default;
    };

    using optional_plutus_data_t = nil_optional_t<plutus_data_t>;
    using required_top_level_guards_t = map_t<credential_t, optional_plutus_data_t>;

    struct direct_deposits_t: withdrawal_map {
        using withdrawal_map::withdrawal_map;

        static direct_deposits_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct account_balance_interval_t {
        struct bounds_t {
            std::optional<uint64_t> lower {};
            std::optional<uint64_t> upper {};

            bool operator==(const bounds_t &) const =default;
        };
        using value_type = std::variant<uint64_t, bounds_t>;

        value_type value {};

        static account_balance_interval_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;

        bool operator==(const account_balance_interval_t &) const =default;
    };

    using account_balance_intervals_t = map_t<credential_t, account_balance_interval_t>;

    struct guards_t {
        using key_set = set_t<key_hash>;
        using credential_list = vector_t<credential_t>;
        using value_type = std::variant<key_set, credential_list>;

        value_type value { key_set {} };

        static guards_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
        bool empty() const;
        signer_set key_hashes() const;
    };

    struct redeemer_t {
        tx_redeemer value {};

        static redeemer_t from_cbor(cbor::zero2::map_reader &);
    };

    struct redeemers_t: alonzo::redeemers_t {
        static redeemers_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct transaction_witness_set_t: shelley::transaction_witness_set_t {
        redeemers_t redeemers {};

        static transaction_witness_set_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct body_extension_t {
        std::optional<hash_32> auxiliary_data_hash {};
        std::optional<hash_32> script_data_hash {};
        std::optional<uint8_t> network_id {};
        guards_t guards {};
        required_top_level_guards_t required_top_level_guards {};
        direct_deposits_t direct_deposits {};
        account_balance_intervals_t account_balance_intervals {};
    };

    struct sub_transaction_body_t: conway::transaction_body_t, body_extension_t {
        static sub_transaction_body_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct sub_transaction_t {
        sub_transaction_body_t body {};
        transaction_witness_set_t witnesses {};
        std::optional<auxiliary_data_t> auxiliary_data {};

        static sub_transaction_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct transaction_body_t: conway::transaction_body_t, body_extension_t {
        std::vector<sub_transaction_t> sub_transactions {};
        account_balance_intervals_t starting_account_balance_intervals {};

        static transaction_body_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct transaction_t {
        transaction_body_t body {};
        transaction_witness_set_t witnesses {};
        std::optional<auxiliary_data_t> auxiliary_data {};

        static transaction_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct tx_base: conway::tx_base {
        using conway::tx_base::tx_base;

        void parse_witnesses(cbor::zero2::value &) override;
    };

    struct tx: tx_base {
        tx(const cardano::block_base &, uint64_t blk_off, cbor::zero2::value &, size_t idx=0,
            bool invalid=false, bool mempool=false);

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
        const conway::vote_set &votes() const override;
        const conway::proposal_set &proposals() const override;
        std::optional<uint64_t> current_treasury() const override;
        void to_cbor(era_encoder &) const;
    private:
        transaction_body_t _body {};
        std::optional<auxiliary_data_t> _auxiliary_data {};
        buffer _raw {};
    };
}
