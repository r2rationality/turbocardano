/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    using namespace plutus;

    transaction_body_t transaction_body_t::from_cbor(cbor::zero2::value &v)
    {
        transaction_body_t res {};
        std::optional<proposal_procedures_t> proposals {};
        auto &it = v.map();
        while (!it.done()) {
            auto &mk = it.read_key();
            const auto typ = mk.uint();
            auto &mv = it.read_val(std::move(mk));
            switch (typ) {
                case 0: res.inputs = transaction_inputs_t::from_cbor(mv); break;
                case 1: {
                    auto outputs = transaction_outputs_t::from_cbor(mv);
                    static_cast<tx_output_list &>(res.outputs).swap(outputs.value);
                    break;
                }
                case 2: res.fee = mv.uint(); break;
                case 3: res.validity_end.emplace(mv.uint()); break;
                case 4: res.certs = certificates_t::from_cbor(mv); break;
                case 5: res.withdrawals = shelley::withdrawals_t::from_cbor(mv); break;
                case 7: static_cast<void>(hash_32 { mv.bytes() }); break; // auxiliary_data_hash
                case 8: res.validity_start.emplace(mv.uint()); break;
                case 9: res.mints = mint_t::from_cbor(mv); break;
                case 11: static_cast<void>(hash_32 { mv.bytes() }); break; // script_data_hash
                case 13: res.collateral_inputs = transaction_inputs_t::from_cbor(mv); break;
                case 14: res.required_signers = required_signers_t::from_cbor(mv); break;
                case 15: if (mv.uint() > 1) [[unlikely]] throw error("network_id must be 0 or 1"); break;
                case 16: res.collateral_return.emplace(std::move(transaction_output_t::from_cbor(mv).value)); break;
                case 17: res.collateral_value.emplace(mv.uint()); break;
                case 18: res.ref_inputs = transaction_inputs_t::from_cbor(mv); break;
                case 19: res.votes = voting_procedures_t::from_cbor(mv); break;
                case 20: proposals.emplace(proposal_procedures_t::from_cbor(mv)); break;
                case 21: res.current_treasury = mv.uint(); break;
                case 22: res.donation = static_cast<uint64_t>(positive_coin_t { mv.uint() }); break;
                default: throw error(fmt::format("unsupported conway::tx_body element type: {}", typ));
            }
        }
        res.raw = v.data_raw();
        if (proposals) {
            res.proposals.reserve(proposals->size());
            const auto tx_id = crypto::blake2b::digest<tx_hash>(res.raw);
            res.hash = tx_id;
            size_t prop_idx = 0;
            for (auto &&p: *proposals)
                res.proposals.emplace_hint(res.proposals.end(), gov_action_id_t { tx_id, numeric_cast<uint16_t>(prop_idx++) }, std::move(p));
        }
        return res;
    }

}
