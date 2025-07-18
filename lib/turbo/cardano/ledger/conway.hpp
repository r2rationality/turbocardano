#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/conway/block.hpp>
#include <turbo/cardano/ledger/babbage.hpp>

namespace turbo::cardano::ledger::conway {
    using namespace cardano::conway;

    struct vrf_state: babbage::vrf_state {
        vrf_state(babbage::vrf_state &&);
    };

    struct drep_info_t {
        uint64_t deposited = 0;
        optional_anchor_t anchor {};
        uint64_t expire_epoch = 0;
        set_t<credential_t> delegs {};
        size_t num_updates = 0;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.deposited, self.anchor, self.expire_epoch, self.delegs);
        }

        static uint64_t compute_expire_epoch(const protocol_params &pp, uint64_t current_epoch);
        void to_cbor(era_encoder &) const;
    };

    struct gov_action_state_t {
        proposal_procedure_t proposal {};
        uint64_t proposed_in = 0;
        uint64_t expires_after = 0;
        cert_loc_t loc {};
        std::map<credential_t, voting_procedure_t> committee_votes {};
        std::map<credential_t, voting_procedure_t> drep_votes {};
        std::map<pool_hash, voting_procedure_t> pool_votes {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.proposal, self.proposed_in, self.expires_after, self.loc,
                self.committee_votes, self.drep_votes, self.pool_votes);
        }

        void to_cbor(era_encoder &, const gov_action_id_t &) const;
    };

    struct committee_t {
        struct new_t {};
        struct resigned_t {};
        struct hot_key_t {
            using value_type = std::variant<new_t, resigned_t, credential_t>;
            value_type val { new_t {} };

            void to_cbor(era_encoder &) const;
        };

        using member_map = std::map<credential_t, uint64_t>;
        using member_key_map = std::map<credential_t, hot_key_t>;

        member_map members {};
        rational_u64 threshold { 2, 3 };

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.members, self.threshold);
        }

        static committee_t from_json(const json::value &);
        void to_cbor(era_encoder &) const;
        size_t active_size(const member_key_map &hot_key) const;
    };

    struct prev_actions_t {
        gov_action_id_list param_updates {};
        gov_action_id_list hard_forks {};
        gov_action_id_list committee_updates {};
        gov_action_id_list constitution_updates {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.param_updates, self.hard_forks, self.committee_updates, self.constitution_updates);
        }

        void to_cbor(era_encoder &) const;
    };

    using optional_committee_t = array_optional_t<committee_t>;

    struct enact_state_t {
        optional_committee_t committee {};
        constitution_t constitution {};
        protocol_params params {};
        protocol_params prev_params {};
        uint64_t treasury = 0;
        stake_distribution withdrawals {};
        prev_actions_t prev_actions {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.committee, self.constitution,
                self.params, self.prev_params, self.treasury,
                self.withdrawals, self.prev_actions);
        }

        void to_cbor(era_encoder &) const;
    };

    using proposal_map = std::map<gov_action_id_t, gov_action_state_t>;
    using gov_action_item_t = std::pair<gov_action_id_t, gov_action_state_t>;
    using proposal_list = std::vector<gov_action_item_t>;
    using drep_distr_t = std::map<drep_t, uint64_t>;
    using drep_info_map = std::map<credential_t, drep_info_t>;
    using drep_info_map_copy = static_map<credential_t, drep_info_t>;

    struct proposal_map_copy: std::vector<gov_action_item_t> {
        using vector::vector;

        const gov_action_state_t &at(const gov_action_id_t &id) const
        {
            for (const auto &[gid, gas]: *this) {
                if (gid == id)
                    return gas;
            }
            throw error(fmt::format("unknown gov_action_id_t: {}", id));
        }
    };

    struct pulsing_data_t {
        proposal_map_copy proposals {};
        drep_info_map_copy drep_state {};
        drep_distr_t drep_voting_power {};
        pool_stake_distribution pool_voting_power {};
        bool drep_state_updated = false;

        void to_zpp(zpp_encoder &) const;
        void from_zpp(parallel_decoder &);
    };

    struct state: babbage::state {
        state();
        state(babbage::state &&);

        using babbage::state::process_cert;

        bool committee_accepted(const gov_action_state_t &ga) const;
        bool dreps_accepted(const gov_action_state_t &ga) const;
        bool pools_accepted(const gov_action_state_t &ga) const;
        bool accepted_by_everyone(const gov_action_id_t &gid, const gov_action_state_t &gas) const;

        void from_zpp(parallel_decoder &) override;
        void to_zpp(zpp_encoder &) const override;
        void start_epoch(std::optional<uint64_t> new_epoch) override;
        bool has_drep(const credential_t &id) const override;
        void process_cert(const cert_t &, const cert_loc_t &loc) override;
        void run_pulser_if_ready() override;

        virtual void process_cert(const reg_cert &, const cert_loc_t &);
        virtual void process_cert(const unreg_cert &, const cert_loc_t &);
        virtual void process_cert(const vote_deleg_cert &, const cert_loc_t &);
        virtual void process_cert(const stake_vote_deleg_cert &, const cert_loc_t &);
        virtual void process_cert(const stake_reg_deleg_cert &, const cert_loc_t &);
        virtual void process_cert(const vote_reg_deleg_cert &, const cert_loc_t &);
        virtual void process_cert(const stake_vote_reg_deleg_cert &, const cert_loc_t &);
        virtual void process_cert(const auth_committee_hot_cert &, const cert_loc_t &);
        virtual void process_cert(const resign_committee_cold_cert &, const cert_loc_t &);
        virtual void process_cert(const reg_drep_cert &, const cert_loc_t &);
        virtual void process_cert(const unreg_drep_cert &, const cert_loc_t &);
        virtual void process_cert(const update_drep_cert &, const cert_loc_t &);
        virtual void process_proposal(const proposal_t &, const cert_loc_t &);
        virtual void process_vote(const vote_info_t &, const cert_loc_t &);

        virtual bool has_gov_action(const gov_action_id_t &) const;
        virtual const gov_action_state_t &gov_action(const gov_action_id_t &) const;
        virtual const optional_committee_t &committee() const;
        virtual const pulsing_data_t &pulser_data() const;

        uint64_t num_dormant_epochs() const
        {
            return _num_dormant_epochs;
        }

        const drep_info_map &drep_state() const
        {
            return _drep_state;
        }
    protected:
        enum class default_vote_t {
            abstain, no_confidence, no
        };

        struct voting_threshold_t {
            struct no_voting_threshold_t {};
            struct no_voting_allowed_t {};
            using value_type = std::variant<no_voting_threshold_t, no_voting_allowed_t, rational_u64>;
            value_type val {};
        };

        struct ratify_state_t {
            enact_state_t new_state {};
            proposal_list enacted {};
            set_t<gov_action_id_t> expired {};
            bool delayed = false;

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.new_state, self.enacted, self.expired, self.delayed);
            }

            void to_cbor(era_encoder &) const;
        };

        enact_state_t _enact_state {};
        ratify_state_t _ratify_state {};
        pulsing_data_t _pulsing_data {};
        committee_t::member_key_map _committee_hot_keys {};
        drep_info_map _drep_state {};
        uint64_t _num_dormant_epochs = 0;
        proposal_map _proposals {};
        uint64_t _donations = 0;
        std::optional<uint64_t> _conway_start_epoch {};
        bool _ratify_ready = false;

        // previously public method
        virtual void delegate_vote(const stake_ident &, const drep_t &, const cert_loc_t &);
        void retire_stake(uint64_t slot, const stake_ident &stake_id, std::optional<uint64_t> deposit) override;

        void _add_encode_task(cbor_encoder &, const encode_cbor_func &) const override;
        void _apply_conway_params(protocol_params &p) const;

        void _account_to_cbor(const account_info &acc, era_encoder &enc) const override;
        void _delegation_gov_to_cbor(era_encoder &enc) const override;
        void _donations_to_cbor(era_encoder &) const override;
        void _params_to_cbor(era_encoder &enc, const protocol_params &params) const override;
        void _protocol_state_to_cbor(era_encoder &) const override;
        void _stake_pointers_to_cbor(era_encoder &) const override;
        void _stake_pointer_stake_to_cbor(era_encoder &) const override;

        void _process_block_updates(block_update_list &&) override;
        void _process_timed_update(tx_out_ref_list &, timed_update_t &&) override;
        void _tick(uint64_t slot) override;

        // governance: Ratify related internal methods

        // supporting methods

        static bool _check_threshold(const voting_threshold_t &t, const rational_u64 &r);
        rational_u64 _param_update_threshold(const param_update_t &upd, const drep_voting_thresholds_t &t) const;
        voting_threshold_t _committee_voting_threshold(const enact_state_t &st, const gov_action_t &ga) const;
        voting_threshold_t _pool_voting_threshold(const enact_state_t &st, const gov_action_t &ga) const;
        voting_threshold_t _drep_voting_threshold(const enact_state_t &st, const gov_action_t &ga) const;
        default_vote_t _pool_default_vote(const pool_hash &) const;

        drep_distr_t _compute_drep_voting_power() const;
        pool_stake_distribution _compute_pool_voting_power() const;

        // state modifying methods

        void _transfer_treasury_withdrawals(const stake_distribution &rewards);
        static void _enact_proposal(enact_state_t &st, const gov_action_id_t &gid, const gov_action_t &ga);
        void _gov_remove_proposal(const gov_action_id_t &gid);
        void _gov_finalize();
        void _gov_enact();
        void _gov_make_pulsing_snapshot();
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::cardano::ledger::conway::vrf_state>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<const turbo::cardano::ledger::babbage::vrf_state &>(v));
        }
    };
}