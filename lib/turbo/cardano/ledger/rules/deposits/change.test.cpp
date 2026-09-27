/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include "change.hpp"
#include <map>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace cardano;
    using namespace ledger::rules;

    struct deposit_view {
        std::map<credential_t, amount_sum> stake_values {}, drep_values {};
        std::map<pool_hash, bool> pools {};
        amount_sum &stake(const credential_t &id) { return stake_values[id]; }
        amount_sum &drep(const credential_t &id) { return drep_values[id]; }
        bool &pool(const pool_hash &id) { return pools[id]; }
        void add_stake(const credential_t &id, uint64_t value) { stake(id) += value; }
        void add_drep(const credential_t &id, uint64_t value) { drep(id) += value; }
    };
}

suite deposit_change_suite = [] {
    "Deposits hypothetical fold adds repeated registrations and removes the stored amount"_test = [] {
        protocol_params params {};
        params.key_deposit = 10;
        deposit_view view {};
        const credential_t id {};
        view.stake(id) = 7; // Registered under a different parameter value.
        deposit_change total {};
        total += certificate_deposits(stake_reg_cert { id }, params, view);
        total += certificate_deposits(reg_cert { id, 999 }, params, view);
        // The invalid branch does not check CERTS premises or use the claimed refund.
        total += certificate_deposits(unreg_cert { id, 999 }, params, view);
        expect(total.out_coin == amount_sum { 20 });
        expect(total.in_coin == amount_sum { 27 });
        expect(view.stake(id) == amount_sum {});
        total += certificate_deposits(stake_reg_cert { id }, params, view);
        expect(view.stake(id) == amount_sum { 10 });
        expect(total.out_coin == amount_sum { 30 });
    };

    "Deposits pool registration is charged once and DRep refunds retain wide sums"_test = [] {
        protocol_params params {};
        params.pool_deposit = 500;
        deposit_view view {};
        const pool_reg_cert pool {};
        const auto first = certificate_deposits(pool, params, view);
        const auto repeat = certificate_deposits(pool, params, view);
        expect(first.out_coin == amount_sum { 500 });
        expect(repeat.empty());
        const credential_t id {};
        certificate_deposits(reg_drep_cert { id, UINT64_MAX }, params, view);
        certificate_deposits(reg_drep_cert { id, 1 }, params, view);
        const auto refund = certificate_deposits(unreg_drep_cert { id, 0 }, params, view);
        expect(refund.in_coin.low == 0_u);
        expect(refund.in_coin.high == 1_u);
        expect(view.drep(id) == amount_sum {});
    };
};
