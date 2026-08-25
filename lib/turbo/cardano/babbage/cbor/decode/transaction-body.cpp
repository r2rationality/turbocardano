/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>

namespace turbo::cardano::babbage {
    transaction_body_t transaction_body_t::from_cbor(cbor::zero2::value &v)
    {
        transaction_body_t res {};
        auto &it = v.map();
        while (!it.done()) {
            auto &mk = it.read_key();
            const auto typ = mk.uint();
            auto &mv = it.read_val(std::move(mk));
            switch (typ) {
                case 0: res.inputs = shelley::transaction_inputs_t::from_cbor(mv); break;
                case 1: {
                    auto outputs = transaction_outputs_t::from_cbor(mv);
                    static_cast<tx_output_list &>(res.outputs).swap(outputs.value);
                    break;
                }
                case 2: res.fee = mv.uint(); break;
                case 3: res.validity_end.emplace(mv.uint()); break;
                case 4: res.certs = shelley::certificates_t::from_cbor(mv); break;
                case 5: res.withdrawals = shelley::withdrawals_t::from_cbor(mv); break;
                case 6: {
                    auto updates = update_t::from_cbor(mv);
                    static_cast<param_update_proposal_list &>(res.updates).swap(updates.value);
                    break;
                }
                case 7: static_cast<void>(hash_32 { mv.bytes() }); break; // auxiliary_data_hash
                case 8: res.validity_start.emplace(mv.uint()); break;
                case 9: res.mints = mary::mint_t::from_cbor(mv); break;
                case 11: static_cast<void>(hash_32 { mv.bytes() }); break; // script_data_hash
                case 13: res.collateral_inputs = shelley::transaction_inputs_t::from_cbor(mv); break;
                case 14: res.required_signers = alonzo::required_signers_t::from_cbor(mv); break;
                case 15: if (mv.uint() > 1) [[unlikely]] throw error("network_id must be 0 or 1"); break;
                case 16: res.collateral_return.emplace(std::move(transaction_output_t::from_cbor(mv).value)); break;
                case 17: res.collateral_value.emplace(mv.uint()); break;
                case 18: res.ref_inputs = shelley::transaction_inputs_t::from_cbor(mv); break;
                default: throw error(fmt::format("unsupported tx element type: {}", typ));
            }
        }
        res.raw = v.data_raw();
        return res;
    }

}
