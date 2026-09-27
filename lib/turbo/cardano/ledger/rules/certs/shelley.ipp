/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Certs.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::process_cert(const cert_t &cert, const cert_loc_t &loc)
    {
        _tick(loc.slot);
        std::visit([&](const auto &c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, stake_reg_cert>
                    || std::is_same_v<T, stake_dereg_cert>
                    || std::is_same_v<T, stake_deleg_cert>
                    || std::is_same_v<T, pool_reg_cert>
                    || std::is_same_v<T, pool_retire_cert>
                    || std::is_same_v<T, genesis_deleg_cert>
                    || std::is_same_v<T, instant_reward_cert>) {
                process_cert(c, loc);
            } else {
                throw error(fmt::format("certificate type is not supported in shelley era: {}", typeid(T).name()));
            }
        }, cert.val);
    }

    void state::withdraw_reward(const stake_ident &stake_id, const uint64_t amount)
    {
        auto &acc = _accounts.at(stake_id);
        logger::trace("withdraw_reward stake_id: {} amount: {} reward: {} deposit: {} registered: {} delegated: {}",
            stake_id, cardano::amount { amount }, cardano::amount { acc.reward }, cardano::amount { acc.deposit },
            acc.ptr.has_value(), acc.deleg.has_value());
        if (acc.reward != amount) [[unlikely]] {
            logger::debug("withdraw_reward mismatch epoch: {} stake_id: {} amount: {} reward: {} difference: {} registered: {} ptr: {} deposit: {} stake: {} mark_stake: {} set_stake: {} go_stake: {} deleg: {} mark_deleg: {} set_deleg: {} go_deleg: {} active_pool_dist: {}",
                _epoch, stake_id, cardano::amount { amount }, cardano::amount { acc.reward },
                cardano::amount { amount > acc.reward ? amount - acc.reward : acc.reward - amount }, acc.ptr.has_value(), acc.ptr,
                cardano::amount { acc.deposit }, cardano::amount { acc.stake },
                cardano::amount { acc.mark_stake }, cardano::amount { acc.set_stake },
                cardano::amount { acc.go_stake }, acc.deleg, acc.mark_deleg,
                acc.set_deleg, acc.go_deleg,
                cardano::amount { acc.deleg ? _active_pool_dist.get(*acc.deleg) : 0 });
            throw error(fmt::format("withdrawal from account {} does not drain its reward balance: requested {} but available {}", stake_id, amount, acc.reward));
        }
        acc.reward -= amount;
        if (acc.deleg)
            _active_pool_dist.sub(*acc.deleg, amount);
    }

    void state::_withdraw_reward(const reward_id_t &reward_id, const uint64_t amount)
    {
        if (reward_id.network_id() != _cfg.shelley_network_id) [[unlikely]] {
            throw error(fmt::format(
                "withdrawal reward address has network id {} but expected {}",
                reward_id.network_id(),
                _cfg.shelley_network_id));
        }
        withdraw_reward(static_cast<stake_ident>(reward_id), amount);
    }
