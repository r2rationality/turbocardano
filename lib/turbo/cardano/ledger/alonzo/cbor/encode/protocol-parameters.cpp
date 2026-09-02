/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/alonzo.hpp>

namespace turbo::cardano::ledger::alonzo {
    void state::_params_to_cbor(era_encoder &enc, const protocol_params &params) const
    {
        enc.array(25);
        enc.uint(params.min_fee_a);
        enc.uint(params.min_fee_b);
        enc.uint(params.max_block_body_size);
        enc.uint(params.max_transaction_size);
        enc.uint(params.max_block_header_size);
        enc.uint(params.key_deposit);
        enc.uint(params.pool_deposit);
        enc.uint(params.e_max);
        enc.uint(params.n_opt);
        params.pool_pledge_influence.to_cbor(enc);
        params.expansion_rate.to_cbor(enc);
        params.treasury_growth_rate.to_cbor(enc);
        params.decentralization.to_cbor(enc);
        if (!params.extra_entropy)
            enc.array(1).uint(0);
        else
            enc.array(2).uint(1).bytes(*params.extra_entropy);
        enc.uint(params.protocol_ver.major);
        enc.uint(params.protocol_ver.minor);
        enc.uint(params.min_pool_cost);
        enc.uint(params.lovelace_per_utxo_byte);
        params.plutus_cost_models.to_cbor(enc);
        params.ex_unit_prices.to_cbor(enc);
        params.max_tx_ex_units.to_cbor(enc);
        params.max_block_ex_units.to_cbor(enc);
        enc.uint(params.max_value_size);
        enc.uint(params.max_collateral_pct);
        enc.uint(params.max_collateral_inputs);
    }

    void state::_param_update_to_cbor(era_encoder &enc, const param_update &upd) const
    {
        auto my_enc { enc };
        size_t cnt = _param_update_common_to_cbor(my_enc, upd);
        cnt += _param_to_cbor(my_enc, 16, upd.min_pool_cost);
        cnt += _param_to_cbor(my_enc, 17, upd.lovelace_per_utxo_byte);
        if (upd.plutus_cost_models) {
            ++cnt;
            my_enc.uint(18);
            upd.plutus_cost_models->to_cbor(my_enc);
        }
        if (upd.ex_unit_prices) {
            ++cnt;
            my_enc.uint(19);
            upd.ex_unit_prices->to_cbor(my_enc);
        }
        if (upd.max_tx_ex_units) {
            ++cnt;
            my_enc.uint(20);
            my_enc.array(2).uint(upd.max_tx_ex_units->mem).uint(upd.max_tx_ex_units->steps);
        }
        if (upd.max_block_ex_units) {
            ++cnt;
            my_enc.uint(21);
            my_enc.array(2).uint(upd.max_block_ex_units->mem).uint(upd.max_block_ex_units->steps);
        }
        cnt += _param_to_cbor(my_enc, 22, upd.max_value_size);
        cnt += _param_to_cbor(my_enc, 23, upd.max_collateral_pct);
        cnt += _param_to_cbor(my_enc, 24, upd.max_collateral_inputs);
        enc.map(cnt);
        enc << my_enc;
    }
}

