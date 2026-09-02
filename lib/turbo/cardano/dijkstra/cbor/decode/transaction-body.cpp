/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        transaction_inputs_t transaction_inputs_from_cbor(cbor::zero2::value &v)
        {
            auto *items = &v;
            if (v.type() == cbor::major_type::tag) {
                auto &tag = v.tag();
                if (tag.id() != 258) [[unlikely]]
                    throw error(fmt::format("expected transaction input set tag 258 but got: {}", tag.id()));
                items = &tag.read();
            }
            transaction_inputs_t result {};
            if (!items->indefinite())
                result.reserve(items->special_uint());
            auto &it = items->array();
            while (!it.done()) {
                auto input = shelley::transaction_input_t::from_cbor(it.read()).value;
                const auto before = result.size();
                result.emplace_hint(result.end(), std::move(input));
                if (result.size() == before) [[unlikely]]
                    throw error("duplicate Dijkstra transaction input");
            }
            return result;
        }

        shelley::withdrawals_t withdrawals_from_cbor(cbor::zero2::value &v)
        {
            shelley::withdrawals_t result {};
            if (!v.indefinite())
                result.reserve(v.special_uint());
            auto &it = v.map();
            while (!it.done()) {
                auto &key = it.read_key();
                auto reward = reward_id_t::from_cbor(key);
                auto &amount = it.read_val(std::move(key));
                const auto before = result.size();
                result.emplace_hint(result.end(), std::move(reward), amount.uint());
                if (result.size() == before) [[unlikely]]
                    throw error("duplicate Dijkstra withdrawal reward account");
            }
            return result;
        }

        required_top_level_guards_t required_top_level_guards_from_cbor(cbor::zero2::value &v)
        {
            required_top_level_guards_t res {};
            if (!v.indefinite()) [[likely]]
                res.reserve(v.special_uint());
            auto &it = v.map();
            while (!it.done()) {
                auto &key_v = it.read_key();
                auto key = credential_t::from_cbor(key_v);
                auto &value_v = it.read_val(std::move(key_v));
                optional_plutus_data_t value {};
                if (value_v.is_null())
                    static_cast<void>(value_v.special());
                else
                    value.emplace(plutus_data_t::from_cbor(value_v));
                const auto before = res.size();
                res.emplace_hint(res.end(), std::move(key), std::move(value));
                if (res.size() == before) [[unlikely]]
                    throw error("duplicate required top-level guard credential");
            }
            if (res.empty()) [[unlikely]]
                throw error("required top-level guards must be nonempty when supplied");
            return res;
        }

        account_balance_intervals_t account_balance_intervals_from_cbor(cbor::zero2::value &v)
        {
            account_balance_intervals_t res {};
            if (!v.indefinite()) [[likely]]
                res.reserve(v.special_uint());
            auto &it = v.map();
            while (!it.done()) {
                auto &key_v = it.read_key();
                auto key = credential_t::from_cbor(key_v);
                auto &value_v = it.read_val(std::move(key_v));
                const auto before = res.size();
                res.emplace_hint(res.end(), std::move(key), account_balance_interval_t::from_cbor(value_v));
                if (res.size() == before) [[unlikely]]
                    throw error("duplicate account balance interval credential");
            }
            if (res.empty()) [[unlikely]]
                throw error("account balance intervals must be nonempty when supplied");
            return res;
        }

        std::vector<sub_transaction_t> sub_transactions_from_cbor(cbor::zero2::value &v)
        {
            auto *items = &v;
            if (v.type() == cbor::major_type::tag) {
                auto &tag = v.tag();
                if (tag.id() != 258) [[unlikely]]
                    throw error(fmt::format("expected sub-transaction set tag 258 but got: {}", tag.id()));
                items = &tag.read();
            }
            std::vector<sub_transaction_t> res {};
            flat_set<tx_hash> body_hashes {};
            if (!items->indefinite()) [[likely]] {
                const auto size = items->special_uint();
                res.reserve(size);
                body_hashes.reserve(size);
            }
            auto &it = items->array();
            while (!it.done()) {
                auto tx = sub_transaction_t::from_cbor(it.read());
                const auto hash = tx.body.hash.value_or(crypto::blake2b::digest<tx_hash>(tx.body.raw));
                if (!body_hashes.emplace(hash).second) [[unlikely]]
                    throw error("duplicate Dijkstra sub-transaction body");
                res.emplace_back(std::move(tx));
            }
            if (res.empty()) [[unlikely]]
                throw error("Dijkstra sub-transactions must be nonempty when supplied");
            return res;
        }

        template<typename BODY>
        BODY body_from_cbor(cbor::zero2::value &v, const bool top_level)
        {
            BODY res {};
            std::optional<proposal_procedures_t> proposals {};
            uint32_t seen = 0;
            auto &it = v.map();
            while (!it.done()) {
                auto &key = it.read_key();
                const auto type = key.uint();
                if (type > 27) [[unlikely]]
                    throw error(fmt::format("unsupported Dijkstra transaction body key: {}", type));
                const auto mask = uint32_t { 1 } << type;
                if (seen & mask) [[unlikely]]
                    throw error(fmt::format("duplicate Dijkstra transaction body key: {}", type));
                seen |= mask;
                auto &value = it.read_val(std::move(key));
                switch (type) {
                    case 0: res.inputs = transaction_inputs_from_cbor(value); break;
                    case 1: {
                        auto outputs = transaction_outputs_t::from_cbor(value);
                        static_cast<tx_output_list &>(res.outputs).swap(outputs.value);
                        break;
                    }
                    case 2:
                        if (!top_level) [[unlikely]]
                            throw error("sub-transaction bodies cannot contain a fee");
                        res.fee = value.uint();
                        break;
                    case 3: res.validity_end = value.uint(); break;
                    case 4: res.certs = certificates_t::from_cbor(value); break;
                    case 5:
                        res.withdrawals = withdrawals_from_cbor(value);
                        if (res.withdrawals.empty()) [[unlikely]]
                            throw error("Dijkstra withdrawals must be nonempty when supplied");
                        break;
                    case 7: res.auxiliary_data_hash = hash_32 { value.bytes() }; break;
                    case 8: res.validity_start = value.uint(); break;
                    case 9:
                        res.mints = mint_t::from_cbor(value);
                        if (res.mints.empty()) [[unlikely]]
                            throw error("Dijkstra mint must be nonempty when supplied");
                        break;
                    case 11: res.script_data_hash = hash_32 { value.bytes() }; break;
                    case 13:
                        if (!top_level) [[unlikely]]
                            throw error("sub-transaction bodies cannot contain collateral inputs");
                        res.collateral_inputs = transaction_inputs_from_cbor(value);
                        if (res.collateral_inputs.empty()) [[unlikely]]
                            throw error("Dijkstra collateral inputs must be nonempty when supplied");
                        break;
                    case 14:
                        res.guards = guards_t::from_cbor(value);
                        {
                            auto signers = res.guards.key_hashes();
                            static_cast<signer_set &>(res.required_signers).swap(signers);
                        }
                        break;
                    case 15: {
                        const auto network = value.uint();
                        if (network > 1) [[unlikely]]
                            throw error("Dijkstra network_id must be 0 or 1");
                        res.network_id = numeric_cast<uint8_t>(network);
                        break;
                    }
                    case 16:
                        if (!top_level) [[unlikely]]
                            throw error("sub-transaction bodies cannot contain a collateral return");
                        res.collateral_return.emplace(std::move(transaction_output_t::from_cbor(value).value));
                        break;
                    case 17:
                        if (!top_level) [[unlikely]]
                            throw error("sub-transaction bodies cannot contain total collateral");
                        res.collateral_value = value.uint();
                        break;
                    case 18:
                        res.ref_inputs = transaction_inputs_from_cbor(value);
                        if (res.ref_inputs.empty()) [[unlikely]]
                            throw error("Dijkstra reference inputs must be nonempty when supplied");
                        break;
                    case 19:
                        res.votes = voting_procedures_t::from_cbor(value);
                        if (res.votes.empty()) [[unlikely]]
                            throw error("Dijkstra voting procedures must be nonempty when supplied");
                        break;
                    case 20: proposals.emplace(proposal_procedures_t::from_cbor(value)); break;
                    case 21: res.current_treasury = value.uint(); break;
                    case 22: {
                        const auto donation = value.uint();
                        if (!donation) [[unlikely]]
                            throw error("Dijkstra treasury donation must be positive when supplied");
                        res.donation = donation;
                        break;
                    }
                    case 23:
                        if constexpr (std::is_same_v<BODY, transaction_body_t>) {
                            if (!top_level) [[unlikely]]
                                throw error("sub-transaction bodies cannot contain sub-transactions");
                            res.sub_transactions = sub_transactions_from_cbor(value);
                        } else {
                            throw error("sub-transaction bodies cannot contain sub-transactions");
                        }
                        break;
                    case 24: res.required_top_level_guards = required_top_level_guards_from_cbor(value); break;
                    case 25: res.direct_deposits = direct_deposits_t::from_cbor(value); break;
                    case 26: res.account_balance_intervals = account_balance_intervals_from_cbor(value); break;
                    case 27:
                        if constexpr (std::is_same_v<BODY, transaction_body_t>) {
                            if (!top_level) [[unlikely]]
                                throw error("sub-transaction bodies cannot contain starting account balance intervals");
                            res.starting_account_balance_intervals = account_balance_intervals_from_cbor(value);
                        } else {
                            throw error("sub-transaction bodies cannot contain starting account balance intervals");
                        }
                        break;
                    [[unlikely]] default:
                        throw error(fmt::format("unsupported Dijkstra transaction body key: {}", type));
                }
            }

            const uint32_t required = top_level
                ? (uint32_t { 1 } << 0) | (uint32_t { 1 } << 1) | (uint32_t { 1 } << 2)
                : (uint32_t { 1 } << 0) | (uint32_t { 1 } << 1);
            if ((seen & required) != required) [[unlikely]]
                throw error("Dijkstra transaction body is missing a required field");

            res.raw = v.data_raw();
            const auto tx_id = crypto::blake2b::digest<tx_hash>(res.raw);
            res.hash = tx_id;
            if (proposals) {
                res.proposals.reserve(proposals->size());
                size_t proposal_idx = 0;
                for (auto &&proposal: *proposals) {
                    res.proposals.emplace_hint(
                        res.proposals.end(),
                        gov_action_id_t { tx_id, numeric_cast<uint16_t>(proposal_idx++) },
                        std::move(proposal));
                }
            }
            return res;
        }
    }

    transaction_body_t transaction_body_t::from_cbor(cbor::zero2::value &v)
    {
        return body_from_cbor<transaction_body_t>(v, true);
    }

    sub_transaction_body_t sub_transaction_body_t::from_cbor(cbor::zero2::value &v)
    {
        return body_from_cbor<sub_transaction_body_t>(v, false);
    }
}
