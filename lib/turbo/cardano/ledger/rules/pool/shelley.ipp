/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Certs.
// Included at namespace scope by lib/turbo/cardano/ledger/shelley.cpp.

    void state::register_pool(const pool_reg_cert &reg)
    {
        const auto active_it = _active_pool_params.find(reg.pool_id);
        const auto pv11 = _params.protocol_ver.major >= 11;
        if (active_it == _active_pool_params.end()) {
            if (pv11 && _pool_vrf_key_hashes.contains(reg.params.vrf_vkey)) [[unlikely]]
                throw error(fmt::format("pool {} tried to register an already registered VRF key {}", reg.pool_id, reg.params.vrf_vkey));
            _active_pool_params.try_emplace(reg.pool_id, reg.params);
            if (pv11)
                _add_pool_vrf_key_hash(reg.params.vrf_vkey);
            _pool_deposits[reg.pool_id] = _params.pool_deposit;
            _deposited += _params.pool_deposit;
        } else {
            const auto future_it = _future_pool_params.find(reg.pool_id);
            const auto vrf_belongs_to_pool = reg.params.vrf_vkey == active_it->second.params.vrf_vkey
                || (future_it != _future_pool_params.end()
                    && reg.params.vrf_vkey == future_it->second.params.vrf_vkey);
            if (pv11 && !vrf_belongs_to_pool && _pool_vrf_key_hashes.contains(reg.params.vrf_vkey)) [[unlikely]] {
                throw error(fmt::format("pool {} tried to re-register an already registered VRF key {}", reg.pool_id, reg.params.vrf_vkey));
            }
            auto [f_it, f_created] = _future_pool_params.try_emplace(reg.pool_id, reg.params);
            if (pv11) {
                if (f_created) {
                    _add_pool_vrf_key_hash(reg.params.vrf_vkey);
                } else if (f_it->second.params.vrf_vkey != reg.params.vrf_vkey) {
                    _remove_pool_vrf_key_hash(f_it->second.params.vrf_vkey);
                    _add_pool_vrf_key_hash(reg.params.vrf_vkey);
                }
            }
            if (!f_created)
                f_it->second.params = reg.params;
        }
        // search for already delegated stake ids - needed for the case of re-registration of a retired pool
        if (_active_pool_dist.create(reg.pool_id)) {
            auto [inv_delegs_it, inv_delegs_created] = _active_inv_delegs.try_emplace(reg.pool_id);
            if (!inv_delegs_created) {
                for (const auto &stake_id: inv_delegs_it->second) {
                    const auto &acc = _accounts.at(stake_id);
                    _active_pool_dist.add(reg.pool_id, acc.stake + acc.reward);
                }
            }
        }
        // delete a planned retirement if present
        _pools_retiring.erase(reg.pool_id);
    }

    void state::retire_pool(const pool_hash &pool_id, uint64_t epoch)
    {
        if (_active_pool_params.contains(pool_id)) {
            _pools_retiring[pool_id] = epoch;
        } else {
            logger::warn("retirement of an unknown pool: {}", pool_id);
        }
    }

    void state::process_cert(const pool_reg_cert &c, const cert_loc_t &)
    {
        register_pool(c);
    }

    void state::process_cert(const pool_retire_cert &c, const cert_loc_t &)
    {
        retire_pool(c.pool_id, c.epoch);
    }

    void state::_add_pool_vrf_key_hash(const vrf_vkey &vrf)
    {
        auto [it, created] = _pool_vrf_key_hashes.try_emplace(vrf, 1);
        if (!created && it->second != std::numeric_limits<uint64_t>::max())
            ++it->second;
    }

    void state::_remove_pool_vrf_key_hash(const vrf_vkey &vrf)
    {
        const auto it = _pool_vrf_key_hashes.find(vrf);
        if (it == _pool_vrf_key_hashes.end()) [[unlikely]]
            throw error(fmt::format("VRF key {} is missing from the registered-key occurrence map", vrf));
        if (it->second <= 1)
            _pool_vrf_key_hashes.erase(it);
        else
            --it->second;
    }

    void state::_populate_pool_vrf_key_hashes()
    {
        _pool_vrf_key_hashes.clear();
        const auto add = [&](const auto &pools) {
            for (const auto &[pool_id, info]: pools) {
                static_cast<void>(pool_id);
                _add_pool_vrf_key_hash(info.params.vrf_vkey);
            }
        };
        add(_active_pool_params);
        add(_future_pool_params);
    }
