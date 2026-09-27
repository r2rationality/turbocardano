#pragma once
/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <turbo/cardano/common/cert.hpp>
#include <turbo/cardano/ledger/rules/utxo/amount.hpp>

namespace turbo::cardano::ledger::rules {
    struct deposit_change {
        amount_sum in_coin {}, out_coin {};

        bool empty() const { return in_coin == amount_sum {} && out_coin == amount_sum {}; }

        deposit_change &operator+=(const deposit_change &other)
        {
            in_coin += other.in_coin;
            out_coin += other.out_coin;
            return *this;
        }
    };

    // Utxo.updateCertDeposits: the caller supplies either the current certificate
    // input state or a sparse hypothetical view. CERTS premises are separate.
    template<typename Certificate, typename View>
    deposit_change certificate_deposits(const Certificate &c, const protocol_params &params, View &view)
    {
        using T = Certificate;
        deposit_change change {};
        if constexpr (std::is_same_v<T, stake_reg_cert> || std::is_same_v<T, reg_cert>
                || std::is_same_v<T, stake_reg_deleg_cert> || std::is_same_v<T, vote_reg_deleg_cert>
                || std::is_same_v<T, stake_vote_reg_deleg_cert>) {
            const uint64_t deposit = [&] {
                if constexpr (std::is_same_v<T, stake_reg_cert> || std::is_same_v<T, reg_cert>)
                    return params.key_deposit;
                else
                    return c.deposit;
            }();
            view.add_stake(c.stake_id, deposit);
            change.out_coin = deposit;
        } else if constexpr (std::is_same_v<T, stake_dereg_cert> || std::is_same_v<T, unreg_cert>) {
            auto &deposit = view.stake(c.stake_id);
            change.in_coin = deposit;
            deposit = 0;
        } else if constexpr (std::is_same_v<T, reg_drep_cert>) {
            view.add_drep(c.drep_id, c.deposit);
            change.out_coin = c.deposit;
        } else if constexpr (std::is_same_v<T, unreg_drep_cert>) {
            auto &deposit = view.drep(c.drep_id);
            change.in_coin = deposit;
            deposit = 0;
        } else if constexpr (std::is_same_v<T, pool_reg_cert>) {
            auto &present = view.pool(c.pool_id);
            if (!present)
                change.out_coin = params.pool_deposit;
            present = true;
        }
        return change;
    }
}
