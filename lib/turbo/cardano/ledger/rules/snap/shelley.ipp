/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Rewards.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::rotate_snapshots()
    {
        timer t { fmt::format("validator::state epoch: {} rotate_snapshots", _epoch), logger::level::trace };
        auto retired_go = std::make_shared<std::optional<ledger_copy>>(std::in_place, std::move(_go));
        _go = std::move(_set);
        _set = std::move(_mark);
        {
            const std::string task_group = fmt::format("ledger-state:rotate-snapshots:epoch-{}", _epoch);
            _sched.wait_all(task_group, [&](const auto &, const auto &submit_f) {
                submit_f({ 1001, task_group, [retired_go] {
                    retired_go->reset();
                }});
                retired_go.reset();
                submit_f({ 1000, task_group, [this] {
                    _mark.pool_dist = _active_pool_dist;
                }});
                submit_f({ 1000, task_group, [this] {
                    _mark.pool_params = _active_pool_params;
                }});
                submit_f({ 1000, task_group, [this] {
                    _mark.delegated_pools.clear();
                    _mark.delegated_pools.reserve(_active_inv_delegs.size());
                    // pool_hash ordering is lexicographic, while partitioned_map uses its first byte.
                    // Concatenating partitions 0..255 therefore produces the sorted sequence expected
                    // by static_map without copying the historical delegator sets.
                    for (size_t pi = 0; pi < inv_delegation_map::num_parts; ++pi) {
                        for (const auto &[pool_id, delegators]: _active_inv_delegs.partition(pi)) {
                            if (!delegators.empty())
                                _mark.delegated_pools.emplace_back(pool_id, true);
                        }
                    }
                }});
                for (size_t pi = 0; pi < _accounts.num_parts; ++pi) {
                    submit_f({ 1000, task_group, [this, pi] {
                        auto &part = _accounts.partition(pi);
                        std::set<stake_ident> retired {};
                        for (auto &[stake_id, acc]: part) {
                            if (acc.ptr || acc.stake || acc.reward || acc.deposit || acc.go_deleg
                                    || acc.set_deleg || acc.mark_deleg || acc.deleg || acc.vote_deleg) {
                                acc.go_deleg = acc.set_deleg;
                                acc.go_stake = acc.set_stake;
                                acc.set_deleg = acc.mark_deleg;
                                acc.set_stake = acc.mark_stake;
                                acc.mark_deleg = acc.deleg;
                                acc.mark_stake = acc.stake + acc.reward;
                            } else {
                                retired.emplace(stake_id);
                            }
                        }
                        for (const auto &stake_id: retired) {
                            part.erase(stake_id);
                        }
                    }});
                }
            });
        }
        if (_params.protocol_ver.keep_pointers()) {
            for (const auto &[stake_ptr, coin]: _stake_pointers) {
                if (const auto ptr_it = _ptr_to_stake.find(stake_ptr); ptr_it != _ptr_to_stake.end()) {
                    auto &acc = _accounts.at(ptr_it->second);
                    acc.mark_stake += coin;
                    if (acc.mark_deleg)
                        _mark.pool_dist.add(*acc.mark_deleg, coin);
                }
            }
        }
    }

    std::optional<cardano::stake_ident> state::_extract_stake_id(const cardano::address &addr) const
    {
        if (addr.has_stake_id()) [[likely]]
            return addr.stake_id();
        if (addr.has_pointer()) [[unlikely]] {
            const auto stake_ptr = addr.pointer();
            if (const auto ptr_it = _ptr_to_stake.find(stake_ptr); ptr_it != _ptr_to_stake.end())
                return ptr_it->second;
            logger::warn("epoch: {} an unrecognized stake pointer has been referenced {} - ignoring it", _epoch, stake_ptr);
        }
        return {};
    }

    void state::_prep_op_stake_dist()
    {
        _operating_stake_dist.clear();
        _operating_stake_dist.total_stake = _set.pool_dist.total_stake();
        for (const auto &[pool_id, coin]: _set.pool_dist) {
            if (_set.delegated_pools.contains(pool_id)) {
                const auto &params = _set.pool_params.at(pool_id).params;
                rational_u64 rel_stake { coin, _set.pool_dist.total_stake() };
                rel_stake.normalize();
                _operating_stake_dist.try_emplace(pool_id, std::move(rel_stake), coin, params.vrf_vkey);
            }
        }
    }
