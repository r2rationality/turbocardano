#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    struct stake_reg_cert {
        stake_ident stake_id {};

        static stake_reg_cert from_cbor(cbor::zero2::array_reader &);
    };

    struct stake_dereg_cert {
        stake_ident stake_id {};

        static stake_dereg_cert from_cbor(cbor::zero2::array_reader &);
    };

    struct stake_deleg_cert {
        stake_ident stake_id {};
        pool_hash pool_id {};

        static stake_deleg_cert from_cbor(cbor::zero2::array_reader &);
    };

    struct pool_reg_cert {
        pool_hash pool_id {};
        pool_params params {};

        static pool_reg_cert from_cbor(cbor::zero2::array_reader &);
    };

    struct pool_retire_cert {
        pool_hash pool_id {};
        cardano::epoch epoch {};

        static pool_retire_cert from_cbor(cbor::zero2::array_reader &);
    };

    struct genesis_deleg_cert {
        key_hash hash;
        pool_hash pool_id;
        cardano::vrf_vkey vrf_vkey;

        static genesis_deleg_cert from_cbor(cbor::zero2::array_reader &);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.hash, self.pool_id, self.vrf_vkey);
        }
    };

    enum class reward_source { reserves, treasury };

    extern reward_source reward_source_from_cbor(cbor::zero2::value &);

    struct instant_reward_cert {
        reward_source source {};
        map_t<stake_ident, amount> rewards {};

        static instant_reward_cert from_cbor(cbor::zero2::array_reader &);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.source, self.rewards);
        }
    };

    struct anchor_t {
        std::string url {};
        datum_hash hash {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.url, self.hash);
        }

        static anchor_t from_cbor(cbor::zero2::value &);
        static anchor_t from_json(const json::value &);
        void to_cbor(era_encoder &) const;
    };
    using optional_anchor_t = nil_optional_t<anchor_t>;

    typedef uint32_t epoch_interval;

    struct positive_coin_t {
        positive_coin_t(const uint64_t coin): _coin { coin }
        {
            if (!_coin) [[unlikely]]
                throw error("positive_coin_t cannot be 0!");
        }

        operator uint64_t() const noexcept
        {
            return _coin;
        }
    private:
        uint64_t _coin;
    };

    struct non_negative_interval {
        uint64_t start;
        uint64_t end;
    };

    struct param_update_t {
        std::optional<uint64_t> min_fee_a {}; // 0
        std::optional<uint64_t> min_fee_b {}; // 1
        std::optional<uint32_t> max_block_body_size {}; // 2
        std::optional<uint32_t> max_transaction_size {}; // 3
        std::optional<uint16_t> max_block_header_size {}; // 4
        std::optional<uint64_t> key_deposit {}; // 5
        std::optional<uint64_t> pool_deposit {}; // 6
        std::optional<uint32_t> e_max {}; // 7
        std::optional<uint64_t> n_opt {}; // 8
        std::optional<rational_u64> pool_pledge_influence {}; // 9
        std::optional<rational_u64> expansion_rate {}; // 10
        std::optional<rational_u64> treasury_growth_rate {}; // 11
        std::optional<uint64_t> min_pool_cost {}; // 16
        std::optional<uint64_t> lovelace_per_utxo_byte {}; // 17
        std::optional<cardano::plutus_cost_models> plutus_cost_models {}; // 18
        std::optional<cardano::ex_unit_prices> ex_unit_prices {}; // 19
        std::optional<ex_units> max_tx_ex_units {}; // 20
        std::optional<ex_units> max_block_ex_units {}; // 21
        std::optional<uint64_t> max_value_size {}; // 22
        std::optional<uint64_t> max_collateral_pct {}; // 23
        std::optional<uint64_t> max_collateral_inputs {}; // 24
        std::optional<pool_voting_thresholds_t> pool_voting_thresholds {}; // 25
        std::optional<drep_voting_thresholds_t> drep_voting_thresholds {}; // 26
        std::optional<uint16_t> committee_min_size {}; // 27
        std::optional<uint32_t> committee_max_term_length {}; // 28
        std::optional<uint32_t> gov_action_lifetime {}; // 29
        std::optional<uint64_t> gov_action_deposit {}; // 30
        std::optional<uint64_t> drep_deposit {}; // 31
        std::optional<uint32_t> drep_activity {}; // 32
        std::optional<rational_u64> min_fee_ref_script_cost_per_byte {}; // 33

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(
                self.min_fee_a,
                self.min_fee_b,
                self.max_block_body_size,
                self.max_transaction_size,
                self.max_block_header_size,
                self.key_deposit,
                self.pool_deposit,
                self.e_max,
                self.n_opt,
                self.pool_pledge_influence,
                self.expansion_rate,
                self.treasury_growth_rate,
                self.min_pool_cost,
                self.lovelace_per_utxo_byte,
                self.plutus_cost_models,
                self.ex_unit_prices,
                self.max_tx_ex_units,
                self.max_block_ex_units,
                self.max_value_size,
                self.max_collateral_pct,
                self.max_collateral_inputs,
                self.pool_voting_thresholds,
                self.drep_voting_thresholds,
                self.committee_min_size,
                self.committee_max_term_length,
                self.gov_action_lifetime,
                self.gov_action_deposit,
                self.drep_deposit,
                self.drep_activity,
                self.min_fee_ref_script_cost_per_byte
            );
        }

        static param_update_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
        bool security_group() const;
        bool network_group() const;
        bool economic_group() const;
        bool technical_group() const;
        bool governance_group() const;
    };

    struct gov_action_id_t {
        tx_hash tx_id {};
        uint16_t idx = 0;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.tx_id, self.idx);
        }

        static gov_action_id_t from_cbor(cbor::zero2::value &v);
        void to_cbor(era_encoder &) const;

        bool operator==(const gov_action_id_t &o) const noexcept
        {
            return tx_id == o.tx_id && idx == o.idx;
        }

        std::strong_ordering operator<=>(const gov_action_id_t &o) const noexcept
        {
            const int cmp = memcmp(tx_id.data(), o.tx_id.data(), tx_id.size());
            if (cmp < 0)
                return std::strong_ordering::less;
            if (cmp > 0)
                return std::strong_ordering::greater;
            return idx <=> o.idx;
        }
    };
    using gov_action_id_list = vector_t<gov_action_id_t>;

    using optional_gov_action_id_t = nil_optional_t<gov_action_id_t>;
    using optional_script_t = nil_optional_t<script_hash>;

    struct constitution_t {
        anchor_t anchor {};
        optional_script_t policy_id {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.anchor, self.policy_id);
        }

        static constitution_t from_cbor(cbor::zero2::value &);
        static constitution_t from_json(const json::value &j);
        void to_cbor(era_encoder &) const;
    };

    struct gov_action_t {
        struct parameter_change_t {
            optional_gov_action_id_t prev_action_id {};
            param_update_t update {};
            optional_script_t policy_id {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.prev_action_id, self.update, self.policy_id);
            }

            static parameter_change_t from_cbor(cbor::zero2::array_reader &it);
            void to_cbor(era_encoder &) const;
        };

        struct hard_fork_init_t {
            optional_gov_action_id_t prev_action_id {};
            protocol_version protocol_ver {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.prev_action_id, self.protocol_ver);
            }

            static hard_fork_init_t from_cbor(cbor::zero2::array_reader &it);
            void to_cbor(era_encoder &) const;
        };

        using withdrawal_map = map_t<reward_id_t, uint64_t>;

        struct treasury_withdrawals_t {
            withdrawal_map withdrawals {};
            optional_script_t policy_id {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.withdrawals, self.policy_id);
            }

            static treasury_withdrawals_t from_cbor(cbor::zero2::array_reader &it);
            void to_cbor(era_encoder &) const;
        };

        struct no_confidence_t {
            optional_gov_action_id_t prev_action_id {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.prev_action_id);
            }

            static no_confidence_t from_cbor(cbor::zero2::array_reader &it);
            void to_cbor(era_encoder &) const;
        };

        struct update_committee_t {
            optional_gov_action_id_t prev_action_id {};
            set_t<credential_t> members_to_remove {};
            map_t<credential_t, uint64_t> members_to_add {};
            rational_u64 new_threshold {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.prev_action_id, self.members_to_remove, self.members_to_add, self.new_threshold);
            }

            static update_committee_t from_cbor(cbor::zero2::array_reader &it);
            void to_cbor(era_encoder &) const;
        };

        struct new_constitution_t {
            optional_gov_action_id_t prev_action_id {};
            constitution_t new_constitution {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.prev_action_id, self.new_constitution);
            }

            static new_constitution_t from_cbor(cbor::zero2::array_reader &it);
            void to_cbor(era_encoder &) const;
        };

        struct info_action_t {
            static info_action_t from_cbor(cbor::zero2::array_reader &it);
            void to_cbor(era_encoder &) const;
        };

        using value_type = std::variant<parameter_change_t, hard_fork_init_t, treasury_withdrawals_t,
            no_confidence_t, update_committee_t, new_constitution_t, info_action_t>;

        value_type val { info_action_t {} };

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.val);
        }

        static gov_action_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
        bool delaying() const;
        int priority() const;
        std::strong_ordering operator<=>(const gov_action_t &o) const;
    };

    enum class vote_t: uint8_t {
        no = 0,
        yes = 1,
        abstain = 2
    };

    struct voting_procedure_t {
        vote_t vote {};
        optional_anchor_t anchor {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.vote, self.anchor);
        }

        static voting_procedure_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
    };

    struct voter_t {
        enum type_t {
            const_comm_key = 0,
            const_comm_script = 1,
            drep_key = 2,
            drep_script = 3,
            pool_key = 4
        };
        type_t type {};
        key_hash hash {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.type, self.hash);
        }

        static voter_t from_cbor(cbor::zero2::value &v);

        std::strong_ordering operator<=>(const voter_t &o) const noexcept
        {
            const int cmp = static_cast<int>(type) - static_cast<int>(o.type);
            if (cmp < 0)
                return std::strong_ordering::less;
            if (cmp > 0)
                return std::strong_ordering::greater;
            return hash <=> o.hash;
        }
    };

    struct proposal_procedure_t {
        uint64_t deposit = 0;
        stake_ident return_addr {};
        gov_action_t action {};
        anchor_t anchor {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.deposit, self.return_addr, self.action, self.anchor);
        }

        static proposal_procedure_t from_cbor(cbor::zero2::value &v);
        void to_cbor(era_encoder &) const;

        std::strong_ordering operator<=>(const proposal_procedure_t &o) const noexcept
        {
            return action <=> o.action;
        }
    };

    struct proposal_t {
        gov_action_id_t id {};
        proposal_procedure_t procedure {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.id, self.procedure);
        }

        static proposal_t from_cbor(const gov_action_id_t &, cbor::zero2::value &v);

        std::strong_ordering operator<=>(const proposal_t &o) const noexcept
        {
            return id <=> o.id;
        }
    };

    struct reg_cert {
        stake_ident stake_id {};
        uint64_t deposit = 0;
    };

    struct unreg_cert {
        stake_ident stake_id {};
        uint64_t deposit = 0;
    };

    struct vote_deleg_cert {
        stake_ident stake_id {};
        drep_t drep {};
    };

    struct stake_vote_deleg_cert {
        stake_ident stake_id {};
        pool_hash pool_id {};
        drep_t drep {};
    };

    struct stake_reg_deleg_cert {
        stake_ident stake_id {};
        pool_hash pool_id {};
        uint64_t deposit = 0;
    };

    struct vote_reg_deleg_cert {
        stake_ident stake_id {};
        drep_t drep {};
        uint64_t deposit = 0;
    };

    struct stake_vote_reg_deleg_cert {
        stake_ident stake_id {};
        pool_hash pool_id {};
        drep_t drep {};
        uint64_t deposit = 0;
    };

    struct auth_committee_hot_cert {
        credential_t cold_id {};
        credential_t hot_id {};
    };

    struct resign_committee_cold_cert {
        credential_t cold_id {};
        optional_anchor_t anchor {};

        static resign_committee_cold_cert from_cbor(cbor::zero2::array_reader &it);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.cold_id, self.anchor);
        }
    };

    struct reg_drep_cert {
        credential_t drep_id {};
        uint64_t deposit = 0;
        optional_anchor_t anchor {};

        static reg_drep_cert from_cbor(cbor::zero2::array_reader &it);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.drep_id, self.deposit, self.anchor);
        }
    };

    struct unreg_drep_cert {
        credential_t drep_id {};
        uint64_t deposit = 0;
    };

    struct update_drep_cert {
        credential_t drep_id {};
        optional_anchor_t anchor {};

        static update_drep_cert from_cbor(cbor::zero2::array_reader &it);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.drep_id, self.anchor);
        }
    };

    using cert_value_t = std::variant<
            stake_reg_cert, stake_dereg_cert, stake_deleg_cert,
            pool_reg_cert, pool_retire_cert,
            genesis_deleg_cert, instant_reward_cert,
            reg_cert, unreg_cert, vote_deleg_cert,
            stake_vote_deleg_cert, stake_reg_deleg_cert, vote_reg_deleg_cert,
            stake_vote_reg_deleg_cert, auth_committee_hot_cert, resign_committee_cold_cert,
            reg_drep_cert, unreg_drep_cert, update_drep_cert
        >;
    struct cert_t {
        cert_value_t val;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.val);
        }

        static cert_t from_cbor(cbor::zero2::value &v);
        std::optional<credential_t> signing_cred() const;
    };
    using cert_list = vector_t<cert_t>;
}

namespace fmt {
    template<>
    struct formatter<turbo::cardano::reward_source>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            switch (v) {
                case turbo::cardano::reward_source::reserves:
                    return fmt::format_to(ctx.out(), "reward_source::reserves");

                case turbo::cardano::reward_source::treasury:
                    return fmt::format_to(ctx.out(), "reward_source::treasury");

                default:
                    throw turbo::error(fmt::format("unsupported reward_source value: {}", static_cast<int>(v)));
                break;
            }
        }
    };

    template<>
    struct formatter<turbo::cardano::pool_reg_cert>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "pool_reg_cert pool_id: {} params: ({})", v.pool_id, v.params);
        }
    };

    template<>
    struct formatter<turbo::cardano::pool_retire_cert>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "pool_retire_cert pool_id: {} epoch: {}", v.pool_id, v.epoch);
        }
    };

    template<>
    struct formatter<turbo::cardano::positive_coin_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::cardano::positive_coin_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", turbo::cardano::amount { v });
        }
    };

    template<>
    struct formatter<turbo::cardano::gov_action_id_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::cardano::gov_action_id_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}#{}", v.tx_id, v.idx);
        }
    };

    template<>
    struct formatter<turbo::cardano::voter_t::type_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::cardano::voter_t::type_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using type_t = turbo::cardano::voter_t::type_t;
            switch (v) {
                case type_t::const_comm_key: return fmt::format_to(ctx.out(), "committee-key");
                case type_t::const_comm_script: return fmt::format_to(ctx.out(), "committee-script");
                case type_t::drep_key: return fmt::format_to(ctx.out(), "drep-key");
                case type_t::drep_script: return fmt::format_to(ctx.out(), "drep-script");
                case type_t::pool_key: return fmt::format_to(ctx.out(), "pool-key");
                default: throw turbo::error(fmt::format("unsupported voter_t::type_t value: {}", static_cast<int>(v)));
            }
        }
    };

    template<>
    struct formatter<turbo::cardano::voter_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::cardano::voter_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}: {}", v.type, v.hash);
        }
    };

    template<>
    struct formatter<turbo::cardano::vote_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::cardano::vote_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using turbo::cardano::vote_t;
            switch (v) {
                case vote_t::yes: return fmt::format_to(ctx.out(), "yes");
                case vote_t::no: return fmt::format_to(ctx.out(), "no");
                case vote_t::abstain: return fmt::format_to(ctx.out(), "abstain");
                default: throw turbo::error(fmt::format("unsupported vote_t value: {}", static_cast<int>(v)));
            }
        }
    };

    template<>
    struct formatter<turbo::cardano::voting_procedure_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::cardano::voting_procedure_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{} anchor: {}", v.vote, v.anchor);
        }
    };

    template<>
    struct formatter<turbo::cardano::anchor_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "url: {} hash: {}", v.url, v.hash);
        }
    };

    template<>
    struct formatter<turbo::cardano::optional_anchor_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const std::optional<turbo::cardano::anchor_t> &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };

    template<>
    struct formatter<turbo::cardano::cert_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return std::visit([&](const auto &cv) {
                using T = std::decay_t<decltype(cv)>;
                if constexpr (std::is_same_v<T, turbo::cardano::pool_reg_cert> || std::is_same_v<T, turbo::cardano::pool_retire_cert>) {
                    return fmt::format_to(ctx.out(), "{}", cv);
                } else {
                    return fmt::format_to(ctx.out(), "unsupported cert_t value");
                }
            }, v.val);
        }
    };

    template<>
    struct formatter<turbo::cardano::param_update_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::cardano::param_update_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[ ");
            if (v.min_fee_a)
                out_it = fmt::format_to(out_it, "min_fee_a: {} ", *v.min_fee_a);
            if (v.min_fee_b)
                out_it = fmt::format_to(out_it, "min_fee_b: {} ", *v.min_fee_b);
            if (v.max_block_body_size)
                out_it = fmt::format_to(out_it, "max_block_body_size: {} ", *v.max_block_body_size);
            if (v.max_transaction_size)
                out_it = fmt::format_to(out_it, "max_transaction_size: {} ", *v.max_transaction_size);
            if (v.max_block_header_size)
                out_it = fmt::format_to(out_it, "max_block_header_size: {} ", *v.max_block_header_size);
            if (v.key_deposit)
                out_it = fmt::format_to(out_it, "key_deposit: {} ", *v.key_deposit);
            if (v.pool_deposit)
                out_it = fmt::format_to(out_it, "pool_deposit: {} ", *v.pool_deposit);
            if (v.e_max)
                out_it = fmt::format_to(out_it, "e_max: {} ", *v.e_max);
            if (v.n_opt)
                out_it = fmt::format_to(out_it, "n_opt: {} ", *v.n_opt);
            if (v.pool_pledge_influence)
                out_it = fmt::format_to(out_it, "pool_pledge_influence: {} ", *v.pool_pledge_influence);
            if (v.expansion_rate)
                out_it = fmt::format_to(out_it, "expansion_rate: {} ", *v.expansion_rate);
            if (v.treasury_growth_rate)
                out_it = fmt::format_to(out_it, "treasury_growth_rate: {} ", *v.treasury_growth_rate);
            if (v.min_pool_cost)
                out_it = fmt::format_to(out_it, "min_pool_cost: {} ", *v.min_pool_cost);
            if (v.lovelace_per_utxo_byte)
                out_it = fmt::format_to(out_it, "lovelace_per_utxo_byte: {} ", *v.lovelace_per_utxo_byte);
            if (v.pool_pledge_influence)
                out_it = fmt::format_to(out_it, "pool_pledge_influence: {} ", *v.pool_pledge_influence);
            if (v.plutus_cost_models)
                out_it = fmt::format_to(out_it, "plutus_cost_models: {} ", *v.plutus_cost_models);
            if (v.ex_unit_prices)
                out_it = fmt::format_to(out_it, "ex_unit_prices: {} ", *v.ex_unit_prices);
            if (v.max_tx_ex_units)
                out_it = fmt::format_to(out_it, "max_tx_ex_units: {} ", *v.max_tx_ex_units);
            if (v.max_block_ex_units)
                out_it = fmt::format_to(out_it, "max_block_ex_units: {} ", *v.max_block_ex_units);
            if (v.max_value_size)
                out_it = fmt::format_to(out_it, "max_value_size: {} ", *v.max_value_size);
            if (v.max_collateral_pct)
                out_it = fmt::format_to(out_it, "max_collateral_pct: {} ", *v.max_collateral_pct);
            if (v.max_collateral_inputs)
                out_it = fmt::format_to(out_it, "max_collateral_inputs: {} ", *v.max_collateral_inputs);
            if (v.pool_voting_thresholds)
                out_it = fmt::format_to(out_it, "pool_voting_thresholds: {} ", *v.pool_voting_thresholds);
            if (v.drep_voting_thresholds)
                out_it = fmt::format_to(out_it, "drep_voting_thresholds: {} ", *v.drep_voting_thresholds);
            if (v.committee_min_size)
                out_it = fmt::format_to(out_it, "committee_min_size: {} ", *v.committee_min_size);
            if (v.committee_max_term_length)
                out_it = fmt::format_to(out_it, "committee_max_term_length: {} ", *v.committee_max_term_length);
            if (v.gov_action_lifetime)
                out_it = fmt::format_to(out_it, "gov_action_lifetime: {} ", *v.gov_action_lifetime);
            if (v.gov_action_deposit)
                out_it = fmt::format_to(out_it, "gov_action_deposit: {} ", *v.gov_action_deposit);
            if (v.drep_deposit)
                out_it = fmt::format_to(out_it, "drep_deposit: {} ", *v.drep_deposit);
            if (v.drep_activity)
                out_it = fmt::format_to(out_it, "drep_activity: {} ", *v.drep_activity);
            if (v.min_fee_ref_script_cost_per_byte)
                out_it = fmt::format_to(out_it, "min_fee_ref_script_cost_per_byte: {}", *v.min_fee_ref_script_cost_per_byte);
            return fmt::format_to(out_it, "]");
        }
    };
}