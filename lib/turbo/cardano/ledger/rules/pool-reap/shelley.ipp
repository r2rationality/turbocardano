/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.PoolReap.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    std::pair<uint64_t, uint64_t> state::_retire_pools()
    {
        uint64_t refunds_user = 0;
        uint64_t refunds_treasury = 0;
        for (auto it = _pools_retiring.begin(); it != _pools_retiring.end(); ) {
            if (_epoch >= it->second) {
                const auto &pool_id = it->first;
                const auto &pool_info = _active_pool_params.at(pool_id);
                const auto pd_it = _pool_deposits.find(pool_id);
                if (pd_it == _pool_deposits.end()) [[unlikely]]
                    throw error("retiring pool {} does not have a deposit record!");
                const auto pool_deposit = pd_it->second;
                _pool_deposits.erase(pd_it);
                for (const auto &stake_id: _active_inv_delegs.at(pool_id))
                    _accounts.at(stake_id).deleg.reset();
                _active_inv_delegs.erase(pool_id);
                const stake_ident reward_stake_id = pool_info.params.reward_id;
                if (auto acc_it = _accounts.find(reward_stake_id); acc_it != _accounts.end() && acc_it->second.ptr) {
                    acc_it->second.reward += pool_deposit;
                    if (const auto rew_acc_it = _accounts.find(reward_stake_id); rew_acc_it != _accounts.end() && rew_acc_it->second.deleg) {
                        if (_active_pool_params.contains(*rew_acc_it->second.deleg)) {
                            _active_pool_dist.add(*rew_acc_it->second.deleg, pool_deposit);
                            refunds_user += pool_deposit;
                        }
                    }
                } else {
                    logger::trace("epoch: {} can't return the deposit of a retiring pool {}, so it goes to the treasury", _epoch, it->first);
                    _treasury += pool_deposit;
                    refunds_treasury += pool_deposit;
                }

                if (_deposited < pool_deposit) [[unlikely]]
                    throw error("trying to remove a deposit while having insufficient deposits");
                _deposited -= pool_deposit;
                _active_pool_dist.retire(it->first);
                if (_params.protocol_ver.major >= 11)
                    _remove_pool_vrf_key_hash(pool_info.params.vrf_vkey);
                _active_pool_params.erase(pool_id);
                it = _pools_retiring.erase(it);
            } else {
                ++it;
            }
        }
        return std::make_pair(refunds_user, refunds_treasury);
    }
