/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/conway/block.hpp>

namespace daedalus_turbo::cardano::conway {
    using namespace plutus;

    void protocol_params_to_cbor(era_encoder &enc, const protocol_params &params)
    {
        enc.array(31);
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
        enc.array(2)
            .uint(params.protocol_ver.major)
            .uint(params.protocol_ver.minor);
        enc.uint(params.min_pool_cost);
        enc.uint(params.lovelace_per_utxo_byte);
        params.plutus_cost_models.to_cbor(enc);
        params.ex_unit_prices.to_cbor(enc);
        params.max_tx_ex_units.to_cbor(enc);
        params.max_block_ex_units.to_cbor(enc);
        enc.uint(params.max_value_size);
        enc.uint(params.max_collateral_pct);
        enc.uint(params.max_collateral_inputs);
        params.pool_voting_thresholds.to_cbor(enc);
        params.drep_voting_thresholds.to_cbor(enc);
        enc.uint(params.committee_min_size);
        enc.uint(params.committee_max_term_length);
        enc.uint(params.gov_action_lifetime);
        enc.uint(params.gov_action_deposit);
        enc.uint(params.drep_deposit);
        enc.uint(params.drep_activity);
        params.min_fee_ref_script_cost_per_byte.to_cbor(enc);
    }

    proposal_procedure_set tx_base::parse_proposals(cbor::zero2::value &v)
    {
        return proposal_procedure_set::from_cbor(v);
    }

    vote_set tx_base::parse_votes(cbor::zero2::value &v)
    {
        vote_set res {};
        if (!v.indefinite()) [[likely]]
            res.reserve(v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            auto voter = voter_t::from_cbor(key);
            auto &val = it.read_val(std::move(key));
            auto &v_it = val.map();
            while (!v_it.done()) {
                auto &v_key = v_it.read_key();
                auto ga_id = gov_action_id_t::from_cbor(v_key);
                res.emplace_hint(res.end(), std::move(voter), std::move(ga_id), voting_procedure_t::from_cbor(v_it.read_val(std::move(v_key))));
            }
        }
        return res;
    }

    void tx_base::foreach_set(cbor::zero2::value &set_raw, const set_observer_t &observer) const
    {
        switch (const auto typ = set_raw.type(); typ) {
            case cbor::major_type::array:
                babbage::tx_base::foreach_set(set_raw, observer);
                break;
            case cbor::major_type::tag: {
                auto &reader = set_raw.tag();
                if (reader.id() != 258) [[unlikely]]
                    throw error(fmt::format("expected a tag with id 258 but got: {}!", reader.id()));
                auto &list = reader.read();
                babbage::tx_base::foreach_set(list, observer);
                break;
            }
            default:
                throw error(fmt::format("unsupported set type: {}", typ));
        }
    }

    void tx_base::parse_redeemers(cbor::zero2::value &v)
    {
        if (v.type() == cbor::major_type::array)
            return babbage::tx_base::parse_redeemers(v);
        if (!v.indefinite()) [[likely]]
            _wits.reserve(_wits.size() + v.special_uint());
        auto &it = v.map();
        while (!it.done()) {
            _wits.emplace_back(tx_redeemer::from_cbor(it));
        }
    }

    void tx_base::parse_witnesses(cbor::zero2::value &v)
    {
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto typ = key.uint();
            auto &val = it.read_val(std::move(key));
            switch (typ) {
                case 0: parse_witnesses_type<tx_wit_shelley_vkey>(val); break;
                case 1: parse_witnesses_script(script_type::native, val); break;
                case 2: parse_witnesses_type<tx_wit_shelley_bootstrap>(val); break;
                case 3: parse_witnesses_script(script_type::plutus_v1, val); break;
                case 4: parse_witnesses_type<tx_wit_datum>(val); break;
                case 5: parse_redeemers(val); break;
                case 6: parse_witnesses_script(script_type::plutus_v2, val); break;
                case 7: parse_witnesses_script(script_type::plutus_v3, val); break;
                default: throw error(fmt::format("unsupported shelley::tx witness type: {}", typ));
            }
        }
        _wits_raw = v.data_raw();
    }

    tx::tx(const cardano::block_base &blk, const uint64_t blk_off, cbor::zero2::value &tx_raw, const size_t idx, const bool invalid):
            tx_base { blk, blk_off, idx, invalid }
    {
        std::optional<proposal_procedure_set> proposals {};
        auto &it = tx_raw.map();
        while (!it.done()) {
            auto &mk = it.read_key();
            const auto typ = mk.uint();
            auto &mv = it.read_val(std::move(mk));
            switch (typ) {
                case 0: _inputs = parse_inputs(mv); break;
                case 1: _outputs = parse_outputs(mv); break;
                case 2: _fee = mv.uint(); break;
                case 3: _validity_end.emplace(mv.uint()); break;
                case 4: _certs = parse_certs(mv); break;
                case 5: _withdrawals = parse_withdrawals(mv); break;
                case 7: break; // metadata_hash
                case 8: _validity_start.emplace(mv.uint()); break;
                case 9: _mints = parse_mints(mv); break;
                case 11: break; // script_data_hash
                case 13: _collateral_inputs = parse_inputs(mv); break;
                case 14: _required_signers = parse_signers(mv); break;
                case 15: break; // network_id
                case 16: _collateral_return.emplace(tx_output::from_cbor(mv)); break;
                case 17: _collateral_value.emplace(mv.uint()); break;
                case 18: _ref_inputs = parse_inputs(mv); break;
                case 19: _votes = parse_votes(mv); break;
                case 20: proposals.emplace(parse_proposals(mv)); break;
                case 21: _current_treasury = mv.uint(); break;
                case 22: _donation = mv.uint(); break;
                default: throw error(fmt::format("unsupported conway::tx_body element type: {}", typ));
            }
        }
        _raw = tx_raw.data_raw();
        if (proposals) {
            _proposals.reserve(proposals->size());
            size_t prop_idx = 0;
            for (auto &&p: *proposals) {
                _proposals.emplace_hint(_proposals.end(), gov_action_id_t { hash(), numeric_cast<uint16_t>(prop_idx++) }, std::move(p));
            }
        }
    }

    const cert_list &tx::certs() const
    {
        return _certs;
    }

    const input_set &tx::collateral_inputs() const
    {
        return _collateral_inputs;
    }

    const std::optional<tx_output> &tx::collateral_return() const
    {
        return _collateral_return;
    }

    const std::optional<uint64_t> &tx::collateral_value() const
    {
        return _collateral_value;
    }

    uint64_t tx::fee() const
    {
        return _fee;
    }

    const tx_hash &tx::hash() const
    {
        if (!_hash)
            _hash.emplace(blake2b<tx_hash>(_raw));
        return *_hash;
    }

    const input_set &tx::inputs() const
    {
        return _inputs;
    }

    const tx_output_list &tx::outputs() const
    {
        return _outputs;
    }

    buffer tx::raw() const
    {
        return _raw;
    }

    const input_set &tx::ref_inputs() const
    {
        return _ref_inputs;
    }

    const signer_set &tx::required_signers() const
    {
        return _required_signers;
    }

    std::optional<uint64_t> tx::current_treasury() const
    {
        return _current_treasury;
    }

    uint64_t tx::donation() const
    {
        return _donation.value_or(0);
    }

    const multi_mint_map &tx::mints() const
    {
        return _mints;
    }

    const proposal_set &tx::proposals() const
    {
        return _proposals;
    }

    const vote_set &tx::votes() const
    {
        return _votes;
    }

    const param_update_proposal_list &tx::updates() const
    {
        return _updates;
    }

    std::optional<uint64_t> tx::validity_start() const
    {
        return _validity_start;
    }

    std::optional<uint64_t> tx::validity_end() const
    {
        return _validity_end;
    }

    const withdrawal_map &tx::withdrawals() const
    {
        return _withdrawals;
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::value &blk, const cardano::config &cfg):
        block { era, offset, hdr_offset, blk.array(), blk, cfg }
    {
    }

    block::block(const uint64_t era, const uint64_t offset, const uint64_t hdr_offset, cbor::zero2::array_reader &it, cbor::zero2::value &blk, const cardano::config &cfg):
        block_base { offset, hdr_offset },
        _hdr { era, it.read(), cfg },
        _txs { parse_txs<tx>(*this, blk.data_begin(), it) },
        _meta { it.read() },
        _invalid_txs { it.read() },
        _raw { blk.data_raw() }
    {
        for (const auto tx_idx: _invalid_txs) {
            mark_invalid_tx(tx_idx);
        }
    }

    uint32_t block::body_size() const
    {
        return numeric_cast<uint32_t>(_raw.size());
    }

    const cardano::block_header_base &block::header() const
    {
        return _hdr;
    }

    const block_hash &block::body_hash() const
    {
        if (!_body_hash)
            _body_hash.emplace(compute_body_hash(_txs.raw, _txs.wits_raw, _meta.raw, _invalid_txs.raw));
        return *_body_hash;
    }

    const tx_list &block::txs() const
    {
        return _txs.txs_view;
    }

    const invalid_tx_set &block::invalid_txs() const
    {
        return _invalid_txs;
    }
}
