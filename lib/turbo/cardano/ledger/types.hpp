#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/scheduler.hpp>
#include <turbo/cardano/common/common.hpp>
#include <turbo/cardano/ledger/pool-rank.hpp>
#include <turbo/parallel/encoder.hpp>
#include <turbo/partitioned-map.hpp>
#include <turbo/static-map.hpp>
#include <unordered_set>

namespace turbo::cardano::ledger {
    using cbor_encoder = parallel::encoder<era_encoder>;
    using zpp_encoder = parallel::encoder<std::monostate>;

    template<typename C>
    struct map_with_get: C
    {
        using C::C;

        const typename C::mapped_type get(const typename C::key_type &id) const
        {
            auto it = C::find(id);
            if (it != C::end())
                return it->second;
            return typename C::mapped_type {};
        }
    };

    template<typename C>
    struct restricted_map: map_with_get<C> {
        
    };

    template<typename C>
    struct distribution: map_with_get<C>
    {
        using map_with_get<C>::map_with_get;

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self._total_stake, const_cast<map_with_get<C> &>(dynamic_cast<const map_with_get<C> &>(self)));
        }

        void add(map_with_get<C>::iterator it, typename C::mapped_type stake)
        {
            if (stake > 0) {
                it->second += stake;
                _total_stake += static_cast<uint64_t>(stake);
            }
        }

        void add(const typename C::key_type &id, typename C::mapped_type stake)
        {
            if (stake > 0) {
                auto [it, created] = map_with_get<C>::try_emplace(id, 0);
                add(it, stake);
            }
        }

        void sub(const typename C::key_type &id, typename C::mapped_type stake, bool remove_zero=true)
        {
            if (stake > 0) {
                auto it = map_with_get<C>::find(id);
                if (it == map_with_get<C>::end()) [[unlikely]]
                    throw error(fmt::format("request to remove stake from an unknown id: {}", id));
                if (it->second < stake) [[unlikely]]
                    throw error(fmt::format("request to delete more stake ({}) than id {} has: {}", stake, id, it->second));
                it->second -= stake;
                if (it->second == 0 && remove_zero)
                    map_with_get<C>::erase(it);
                _total_stake -= static_cast<uint64_t>(stake);
            }
        }

        void clear()
        {
            map_with_get<C>::clear();
            _total_stake = 0;
        }

        uint64_t total_stake() const
        {
            return _total_stake;
        }
    protected:
        uint64_t _total_stake = 0;
    };

    template<typename C>
    struct restricted_distribution: distribution<C>
    {
        using distribution<C>::distribution;

        bool create(const typename C::key_type &id)
        {
            auto [it, created] = distribution<C>::try_emplace(id);
            return created;
        }

        void retire(const typename C::key_type &id)
        {
            auto it = distribution<C>::find(id);
            if (it == distribution<C>::end()) [[unlikely]]
                throw error(fmt::format("retiring an unknown id {}", id));
            distribution<C>::_total_stake -= static_cast<uint64_t>(it->second);
            distribution<C>::erase(it);
        }

        void add(distribution<C>::iterator it, typename C::mapped_type stake)
        {
            if (it == distribution<C>::end()) [[unlikely]]
                throw error(fmt::format("request to increase an unregistered id by {}", stake));
            distribution<C>::add(it, stake);
        }

        void add(const typename C::key_type &id, typename C::mapped_type stake)
        {
            auto it = distribution<C>::find(id);
            if (it == distribution<C>::end()) [[unlikely]]
                throw error(fmt::format("request to increase an unregistered id {} by {}", id, stake));
            add(it, stake);
        }

        void sub(const typename C::key_type &id, typename C::mapped_type stake)
        {
            distribution<C>::sub(id, stake, false);
        }
    };

    enum class reward_type {
        leader,
        member
    };
    struct reward_update {
        reward_type type {};
        pool_hash pool_id {};
        uint64_t amount {};
        std::optional<pool_hash> delegated_pool_id {};

        static reward_update from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            return { it.read().uint() == 0 ? reward_type::member : reward_type::leader, it.read().bytes(), it.read().uint() };
        }

        void to_cbor(era_encoder &) const;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.type, self.pool_id, self.amount, self.delegated_pool_id);
        }

        bool operator<(const auto &b) const
        {
            if (type != b.type)
                return type < b.type;
            return pool_id < b.pool_id;
        }

        bool operator==(const auto &b) const
        {
            return type == b.type && pool_id == b.pool_id && amount == b.amount;
        }
    };
    using reward_update_list = set_t<reward_update>;

    using partitioned_reward_update_dist = partitioned_map<cardano::stake_ident, reward_update_list>;
    using pool_stake_distribution = restricted_distribution<std::map<cardano::pool_hash, uint64_t>>;
    using pool_stake_distribution_copy = static_map<cardano::pool_hash, uint64_t>;
    using pool_update_distribution = distribution<std::map<cardano::pool_hash, size_t>>;
    using stake_distribution = distribution<std::map<cardano::stake_ident, uint64_t>>;
    using stake_pointer_distribution = distribution<std::map<cardano::stake_pointer, uint64_t>>;
    using stake_distribution_copy = static_map<cardano::stake_ident, uint64_t>;
    using reward_distribution_copy = static_map<cardano::stake_ident, uint64_t>;
    using delegation_map = std::map<cardano::stake_ident, cardano::pool_hash>;
    using delegation_map_copy = static_map<cardano::stake_ident, cardano::pool_hash>;
    using inv_delegation_map = partitioned_map<cardano::pool_hash, std::unordered_set<cardano::stake_ident>>;
    // Historical snapshots only need to know whether a pool had any delegators.
    // The full active inverse delegation map remains available for Protocol 11
    // node-state serialization and pool lifecycle processing.
    using delegated_pool_set_copy = static_map<cardano::pool_hash, bool>;

    using ptr_to_stake_map = std::map<cardano::stake_pointer, cardano::stake_ident>;
    using stake_to_ptr_map = std::map<cardano::stake_ident, cardano::stake_pointer>;

    struct pool_reward_item {
        cardano::stake_ident stake_id {};
        reward_type type {};
        uint64_t amount = 0;
        std::optional<cardano::pool_hash> delegated_pool_id {};

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.stake_id, self.type, self.amount, self.delegated_pool_id);
        }
    };

    using pool_set = std::set<cardano::pool_hash>;
    using pool_block_dist = distribution<std::map<cardano::pool_hash, uint64_t>>;
    using pool_reward_list = std::vector<pool_reward_item>;
    using pool_rewards_result = std::tuple<cardano::pool_hash, pool_reward_list, uint64_t>;
    using pool_reward_map = std::map<cardano::pool_hash, pool_reward_list>;
    using pool_retiring_map = std::map<cardano::pool_hash, uint64_t>;

    struct operating_pool_info {
        rational_u64 rel_stake {};
        uint64_t active_stake = 0;
        cardano::vrf_vkey vrf_vkey {};

        static operating_pool_info from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.rel_stake, self.active_stake, self.vrf_vkey);
        }

        bool operator==(const operating_pool_info &o) const
        {
            return rel_stake == o.rel_stake && vrf_vkey == o.vrf_vkey;
        }
    };

    struct operating_pool_map: std::map<pool_hash, operating_pool_info> {
        using base_type = std::map<pool_hash, operating_pool_info>;
        using base_type::base_type;

        uint64_t total_stake = 1; // 1 instead of 0 to mitigate division by zero

        constexpr static auto serialize(auto &archive, const auto &self)
        {
            return archive(self.total_stake, static_cast<const base_type &>(self));
        }

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.total_stake, static_cast<base_type &>(self));
        }

        static operating_pool_map from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;
        void clear();
    };

    struct pool_info {
        pool_params params {};
        cpp_rational_storage reward_base;

        pool_info();
        pool_info(const pool_params &);
        pool_info(pool_params &&);
        pool_info(const pool_info &);
        pool_info(pool_info &&);
        pool_info &operator=(const pool_info &);
        pool_info &operator=(pool_info &&);
        ~pool_info();

        static pool_info from_cbor(cbor::zero2::value &v)
        {
            return { pool_params::from_cbor(v) };
        }

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.params);
        }

        bool operator==(const pool_info &o) const
        {
            return params == o.params;
        }
    };

    using pool_info_map = std::map<pool_hash, pool_info>;
    using pool_deposit_map = std::map<pool_hash, uint64_t>;
    using pool_vrf_key_hash_map = std::map<cardano::vrf_vkey, uint64_t>;
    using nonmyopic_likelihood_map = map_t<pool_hash, pool_rank::likelihood_list>;

    using era_list = std::vector<uint64_t>;
    using stake_update_map = std::unordered_map<stake_ident, int64_t>;
    using pointer_update_map = std::unordered_map<stake_pointer, int64_t>;

    struct account_info {
        uint64_t stake = 0;
        uint64_t reward = 0;
        uint64_t deposit = 0;
        uint64_t mark_stake = 0;
        uint64_t set_stake = 0;
        uint64_t go_stake = 0;
        // the presence of a stake pointer means that the account's stake address is registered currently
        std::optional<stake_pointer> ptr {};
        array_optional_t<pool_hash> deleg {};
        std::optional<pool_hash> mark_deleg {};
        std::optional<pool_hash> set_deleg {};
        std::optional<pool_hash> go_deleg {};
        array_optional_t<drep_t> vote_deleg {};

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.stake, self.reward, self.deposit, self.mark_stake, self.set_stake, self.go_stake,
                self.ptr, self.deleg, self.mark_deleg, self.set_deleg, self.go_deleg, self.vote_deleg);
        }

        static account_info from_cbor(cbor::zero2::value &v);

        bool operator==(const account_info &o) const
        {
            return stake == o.stake && reward == o.reward && deposit == o.deposit
                && mark_stake == o.mark_stake && set_stake == o.set_stake && go_stake == o.go_stake
                && deleg == o.deleg && mark_deleg == o.mark_deleg && set_deleg == o.set_deleg && go_deleg == o.go_deleg
                && ptr == o.ptr;
        }

        const std::optional<pool_hash> &deleg_copy(const size_t idx) const
        {
            switch (idx) {
                case 0: return mark_deleg;
                case 1: return set_deleg;
                case 2: return go_deleg;
                [[unlikely]] default: throw error(fmt::format("unsupported deleg_copy index: {}", idx));
            }
        }

        std::optional<pool_hash> &deleg_copy(const size_t idx)
        {
            return const_cast<std::optional<pool_hash> &>(const_cast<const account_info &>(*this).deleg_copy(idx));
        }

        const uint64_t &stake_copy(const size_t idx) const
        {
            switch (idx) {
                case 0: return mark_stake;
                case 1: return set_stake;
                case 2: return go_stake;
                [[unlikely]] default: throw error(fmt::format("unsupported stake_copy index: {}", idx));
            }
        }

        uint64_t &stake_copy(const size_t idx)
        {
            return const_cast<uint64_t &>(const_cast<const account_info &>(*this).stake_copy(idx));
        }
    };

    struct ledger_copy {
        pool_stake_distribution pool_dist {};
        delegated_pool_set_copy delegated_pools {};
        pool_info_map pool_params {};

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.pool_dist, self.delegated_pools, self.pool_params);
        }

        bool operator==(const ledger_copy &o) const
        {
            return pool_dist == o.pool_dist
                && delegated_pools == o.delegated_pools
                && pool_params == o.pool_params;
        }

        size_t size() const
        {
            return pool_dist.size() + delegated_pools.size() + pool_params.size();
        }

        void clear()
        {
            pool_dist.clear();
            delegated_pools.clear();
            pool_params.clear();
        }
    };

    struct block_update_list;
    struct timed_update_t;
    struct timed_update_list;
    struct utxo_update_list;
    struct updates_t;
    struct state;

    struct parallel_decoder {
        using decode_func = std::function<void(buffer)>;
        using done_func = std::function<void()>;

        explicit parallel_decoder(const std::string &path);
        [[nodiscard]] size_t size() const;
        void add(const decode_func &t);
        buffer at(size_t idx) const;
        void on_done(const done_func &);
        void run(scheduler &sched, const std::string &task_group, int prio=1000, bool report_progress=false);
    private:
        const uint8_vector _data;
        std::vector<buffer> _buffers {};
        std::vector<decode_func> _tasks {};
        std::vector<done_func> _on_done {};
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::cardano::ledger::reward_type>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            switch (v) {
                case turbo::cardano::ledger::reward_type::leader:
                    return fmt::format_to(ctx.out(), "reward_type::leader");

                case turbo::cardano::ledger::reward_type::member:
                    return fmt::format_to(ctx.out(), "reward_type::member");

                [[unlikely]] default:
                    return fmt::format_to(ctx.out(), "reward_type::unsupported");
            }
        }
    };
}
