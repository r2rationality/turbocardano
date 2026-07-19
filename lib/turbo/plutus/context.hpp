#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/mocks.hpp>
#include <turbo/cardano/conway/block.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus {
    using namespace cardano;

    struct stored_txo {
        tx_out_ref id {};
        tx_out_data data {};
    };
    using stored_txo_list = std::vector<stored_txo>;

    struct stored_tx_context {
        tx_hash tx_id {};
        size_t num_redeemers = 0;
        uint8_vector body {};
        uint8_vector wits {};
        storage::block_info block {};
        stored_txo_list inputs {};
        stored_txo_list ref_inputs {};

        bool operator<(const stored_tx_context &o) const
        {
            return tx_id < o.tx_id;
        }
    };

    struct prepared_script {
        allocator alloc;
        script_hash hash;
        script_type typ;
        term expr;
        version ver {};
        std::optional<ex_units> budget {};
    };

    struct context {
        using datum_map = std::map<datum_hash, data>;
        using policy_list = std::vector<script_hash>;
        using cert_list = std::vector<cert_t>;
        using redeemer_map = std::map<redeemer_id, tx_redeemer>;

        context(const std::string &, const cardano::config &c_cfg=cardano::config::get());
        context(stored_tx_context &&, const cardano::config &c_cfg=cardano::config::get());
        context(uint8_vector &&tx_body_data, uint8_vector &&tx_wits_data, const storage::block_info &block, const cardano::config &cfg=cardano::config::get());
        context(uint8_vector &&tx_body_data, uint8_vector &&tx_wits_data, const storage::block_info &block,
            stored_txo_list &&inputs, stored_txo_list &&ref_inputs, const cardano::config &cfg=cardano::config::get());

        void set_inputs(stored_txo_list &&inputs_, stored_txo_list &&ref_inputs_);

        // Freeze configuration and materialize shared transaction data. After this returns,
        // prepare_script() is read-only and may be called concurrently for different scripts.
        void prepare();

        void cost_models(const costs::runtime_models &models)
        {
            _require_configurable("change cost models");
            _cost_models = models;
        }

        void protocol_ver(const protocol_version &pv)
        {
            _require_configurable("change the protocol version");
            _protocol_ver = pv;
            _shared.clear();
        }

        const protocol_version &protocol_ver() const
        {
            return _require_protocol_ver();
        }

        const tx_base &tx() const;
        script_hash redeemer_script(const redeemer_id &) const;

        prepared_script apply_script(allocator &&script_alloc, const script_info &script, std::initializer_list<term> args, const std::optional<ex_units> &budget) const;
        prepared_script prepare_script(const tx_redeemer &r) const;
        void eval_script(prepared_script &ps) const;
        static ex_units validate_redeemer_budgets(const redeemer_map &, const ex_units &limit);
        ex_units validate_redeemer_budgets(const ex_units &limit) const
        {
            return validate_redeemer_budgets(_redeemers, limit);
        }

        cardano::slot slot() const
        {
            return { _block_info.slot, _cfg };
        }

        const stored_txo_list &inputs() const
        {
            return _inputs;
        }

        const stored_txo_list &ref_inputs() const
        {
            return _ref_inputs;
        }

        const script_info_map &scripts() const
        {
            return _scripts;
        }

        const redeemer_map &redeemers() const
        {
            return _redeemers;
        }
    private:
        struct parsed_script {
            term expr;
            version ver;
        };

        struct data_encoder;
        friend data_encoder;

        const cardano::config &_cfg;
        uint8_vector _tx_body_bytes;
        uint8_vector _tx_wits_bytes;
        storage::block_info _block_info;
        std::optional<protocol_version> _protocol_ver {};
        tx_container _tx;
        stored_txo_list _inputs {};
        stored_txo_list _ref_inputs {};
        datum_map _datums {};
        script_info_map _scripts {};
        redeemer_map _redeemers {};
        std::reference_wrapper<const costs::runtime_models> _cost_models = costs::defaults();
        allocator _alloc {};
        std::map<script_type, plutus::data> _shared {};
        bool _inputs_set = false;
        bool _prepared = false;

        const cardano::config &config() const
        {
            return _cfg;
        }

        const costs::runtime_models &cost_models() const
        {
            return _cost_models;
        }

        const protocol_version &_require_protocol_ver() const;
        void _require_configurable(std::string_view operation) const;

        term data(allocator &script_allocator, script_type typ, const tx_redeemer &) const;

        const datum_map &datums() const
        {
            return _datums;
        }

        term term_from_datum(allocator &, const datum_hash &hash) const;
        term term_from_datum(allocator &, const uint8_vector &datum) const;

        credential_t cert_cred_at(uint64_t) const;
        const cert_t &cert_at(uint64_t) const;
        buffer mint_at(uint64_t r_idx) const;
        const reward_id_t &withdraw_at(uint64_t r_idx) const;
        const stored_txo &input_at(uint64_t r_idx) const;
        const proposal_t &proposal_at(uint64_t r_idx) const;
        const voter_t &voter_at(uint64_t r_idx) const;
        const conway::proposal_set &proposals() const;
        const conway::vote_set &votes() const;
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::plutus::stored_txo>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            return fmt::format_to(ctx.out(), "txo-id: {} txo-data: {}", v.id, v.data);
        }
    };

    template<>
    struct formatter<turbo::plutus::stored_tx_context>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            using namespace turbo;
            return fmt::format_to(ctx.out(), "txo-id: {} body: {} wits: {} block at slot: {} inputs: {} ref_inputs: {}",
                v.tx_id, crypto::blake2b::digest(v.body), crypto::blake2b::digest(v.wits), v.block.slot, v.inputs, v.ref_inputs);
        }
    };
}
