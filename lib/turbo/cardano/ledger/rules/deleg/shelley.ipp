/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Certs.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::process_cert(const stake_reg_cert &c, const cert_loc_t &loc)
    {
        register_stake(loc.slot, c.stake_id, {}, loc.tx_idx, loc.cert_idx);
    }

    void state::process_cert(const stake_dereg_cert &c, const cert_loc_t &loc)
    {
        retire_stake(loc.slot, c.stake_id, {});
    }

    void state::process_cert(const stake_deleg_cert &c, const cert_loc_t &loc)
    {
        const auto acc_it = _accounts.find(c.stake_id);
        if (acc_it == _accounts.end() || !acc_it->second.ptr) [[unlikely]] {
            logger::debug("slot: {} stake_deleg_cert stake_id: {} pool_id: {} account_known: {} registered: {} reward: {} deposit: {} delegated: {} tx_idx: {} cert_idx: {}",
                cardano::slot { loc.slot, _cfg }, c.stake_id, c.pool_id,
                acc_it != _accounts.end(),
                acc_it != _accounts.end() && acc_it->second.ptr.has_value(),
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.reward : 0 },
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.deposit : 0 },
                acc_it != _accounts.end() && acc_it->second.deleg.has_value(),
                loc.tx_idx, loc.cert_idx);
        }
        delegate_stake(c.stake_id, c.pool_id);
    }

    void state::register_stake(const uint64_t slot, const stake_ident &stake_id, const std::optional<uint64_t> deposit, const size_t tx_idx, const size_t cert_idx)
    {
        const auto deposit_size = deposit ? *deposit : _params.key_deposit;
        auto [acc_it, acc_created] = _accounts.try_emplace(stake_id);
        if (acc_created || !acc_it->second.ptr) {
            _deposited += deposit_size;
            acc_it->second.deposit += deposit_size;
        }
        stake_pointer ptr { slot, tx_idx, cert_idx };
        _ptr_to_stake[ptr] = stake_id;
        acc_it->second.ptr = ptr;
    }

    void state::retire_stake(const uint64_t slot, const stake_ident &stake_id, const std::optional<uint64_t> deposit)
    {
        const auto deposit_size = deposit ? *deposit : _params.key_deposit;
        auto &acc = _accounts.at(stake_id);
        logger::trace("retire_stake stake_id: {} slot: {} cert_deposit: {} account_reward: {} account_deposit: {} registered: {} delegated: {}",
            stake_id, cardano::slot { slot, _cfg }, cardano::amount { deposit_size }, cardano::amount { acc.reward },
            cardano::amount { acc.deposit }, acc.ptr.has_value(), acc.deleg.has_value());
        if (acc.ptr) {
            if (acc.deposit >= deposit_size) [[likely]]
                acc.deposit -= deposit_size;
            else
                throw error(fmt::format("expected stake deposit: {} is more than the actual one: {}", deposit_size, acc.deposit));
            if (_deposited >= deposit_size) [[likely]]
                _deposited -= deposit_size;
            else
                throw error("trying to remove a deposit while having insufficient deposits");
            _ptr_to_stake.erase(*acc.ptr);
            acc.ptr.reset();
        } else {
            logger::trace("slot: {}/{} can't find the retiring stake's pointer", _epoch, slot);
        }
        if (acc.deleg) {
            const auto stake = acc.stake + acc.reward;
            _active_pool_dist.sub(*acc.deleg, stake);
            _active_inv_delegs.at(*acc.deleg).erase(stake_id);
        }
        _treasury += acc.reward;
        acc.reward = 0;
        acc.deleg.reset();
    }

    void state::delegate_stake(const stake_ident &stake_id, const pool_hash &pool_id)
    {
        const auto pool_known = _active_pool_params.contains(pool_id);
        const auto acc_it = _accounts.find(stake_id);
        if (!pool_known || acc_it == _accounts.end() || !acc_it->second.ptr) [[unlikely]] {
            logger::debug("delegate_stake stake_id: {} pool_id: {} pool_known: {} account_known: {} registered: {} reward: {} deposit: {} delegated: {}",
                stake_id, pool_id, pool_known,
                acc_it != _accounts.end(),
                acc_it != _accounts.end() && acc_it->second.ptr.has_value(),
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.reward : 0 },
                cardano::amount { acc_it != _accounts.end() ? acc_it->second.deposit : 0 },
                acc_it != _accounts.end() && acc_it->second.deleg.has_value());
        }
        if (!pool_known) [[unlikely]]
            throw error(fmt::format("trying to delegate {} to an unknown pool: {}", stake_id, pool_id));
        auto &acc = _accounts.at(stake_id);
        const auto stake = acc.stake + acc.reward;
        const bool deleg_created = !acc.deleg;
        if (!acc.deleg)
            acc.deleg = pool_id;
        if (deleg_created || *acc.deleg != pool_id) {
            _active_inv_delegs[pool_id].emplace(stake_id);
            _active_pool_dist.add(pool_id, stake);
        }
        if (*acc.deleg != pool_id) {
            _active_inv_delegs[*acc.deleg].erase(stake_id);
            // ignore retired pools
            if (_active_pool_params.contains(*acc.deleg)) {
                _active_pool_dist.sub(*acc.deleg, stake);
            }
            *acc.deleg = pool_id;
        }
    }
