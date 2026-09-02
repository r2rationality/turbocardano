/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano {
    param_update_t param_update_t::from_cbor(cbor::zero2::value &v)
    {
        param_update_t upd {};
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto typ = key.uint();
            auto &u = it.read_val(std::move(key));
            switch (typ) {
                case 0: upd.min_fee_a.emplace(u.uint()); break;
                case 1: upd.min_fee_b.emplace(u.uint()); break;
                case 2: upd.max_block_body_size.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 3: upd.max_transaction_size.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 4: upd.max_block_header_size.emplace(numeric_cast<uint16_t>(u.uint())); break;
                case 5: upd.key_deposit.emplace(u.uint()); break;
                case 6: upd.pool_deposit.emplace(u.uint()); break;
                case 7: upd.e_max.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 8: upd.n_opt.emplace(u.uint()); break;
                case 9: upd.pool_pledge_influence = decltype(upd.pool_pledge_influence)::value_type::from_cbor(u); break;
                case 10: upd.expansion_rate = decltype(upd.expansion_rate)::value_type::from_cbor(u); break;
                case 11: upd.treasury_growth_rate = decltype(upd.treasury_growth_rate)::value_type::from_cbor(u); break;
                case 16: upd.min_pool_cost.emplace(u.uint()); break;
                case 17: upd.lovelace_per_utxo_byte.emplace(u.uint()); break;
                case 18: upd.plutus_cost_models = conway::cost_models_t::from_cbor(u).value; break;
                case 19: upd.ex_unit_prices = decltype(upd.ex_unit_prices)::value_type::from_cbor(u); break;
                case 20: upd.max_tx_ex_units = decltype(upd.max_tx_ex_units)::value_type::from_cbor(u); break;
                case 21: upd.max_block_ex_units = decltype(upd.max_block_ex_units)::value_type::from_cbor(u); break;
                case 22: upd.max_value_size.emplace(u.uint()); break;
                case 23: upd.max_collateral_pct.emplace(u.uint()); break;
                case 24: upd.max_collateral_inputs.emplace(u.uint()); break;
                case 25: upd.pool_voting_thresholds = decltype(upd.pool_voting_thresholds)::value_type::from_cbor(u); break;
                case 26: upd.drep_voting_thresholds = decltype(upd.drep_voting_thresholds)::value_type::from_cbor(u); break;
                case 27: upd.committee_min_size.emplace(numeric_cast<uint16_t>(u.uint())); break;
                case 28: upd.committee_max_term_length.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 29: upd.gov_action_lifetime.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 30: upd.gov_action_deposit.emplace(u.uint()); break;
                case 31: upd.drep_deposit.emplace(u.uint()); break;
                case 32: upd.drep_activity.emplace(numeric_cast<uint32_t>(u.uint())); break;
                case 33: upd.min_fee_ref_script_cost_per_byte = decltype(upd.min_fee_ref_script_cost_per_byte)::value_type::from_cbor(u); break;
                [[unlikely]] default: throw error(fmt::format("unsupported conway param update: {}", u.to_string()));
            }
        }
        return upd;
    }
}
