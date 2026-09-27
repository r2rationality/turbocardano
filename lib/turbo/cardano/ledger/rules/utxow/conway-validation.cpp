/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include "conway-validation.hpp"
#include <algorithm>
#include <turbo/crypto/blake2b.hpp>

namespace turbo::cardano::ledger::rules {
    uint32_t conway_tx_size(const tx_base &tx, std::optional<buffer> auxiliary)
    {
        // Ledger charged size uses the legacy three-element envelope, omitting isValid.
        return numeric_cast<uint32_t>(1 + tx.raw().size() + tx.witness_raw().size()
            + (auxiliary ? auxiliary->size() : 1));
    }

    void conway_validation::observe_output(const tx_out_data &out)
    {
        has_bootstrap |= out.addr().is_byron();
        if (out.datum) {
            if (const auto *hash = std::get_if<datum_hash>(&out.datum->val))
                allowed_datums.emplace(*hash);
            else
                has_inline_datum = true;
        }
    }

    void conway_validation::collect(const conway::tx &tx, std::optional<buffer> auxiliary)
    {
        const auto &body = tx.body();
        if (body.network_id && *body.network_id != tx.block().config().shelley_network_id)
            throw error("transaction network assertion does not match the ledger");
        std::optional<hash_32> auxiliary_hash {};
        if (auxiliary)
            auxiliary_hash = crypto::blake2b::digest<hash_32>(*auxiliary);
        if (body.auxiliary_data_hash != auxiliary_hash)
            throw error("auxiliary data does not match its body commitment");
        integrity_hash = body.script_data_hash;
        treasury = body.current_treasury;
        uses_v3_features = treasury.has_value() || tx.donation() != 0
            || !tx.votes().empty() || !tx.proposals().empty();
        tx.foreach_script([&](const auto &script) { scripts.emplace(script.hash(), script.type()); });
        tx.foreach_datum([&](const auto &datum) { datums.emplace(datum.hash); });
        for (const auto &[id, data]: tx.redeemers())
            redeemers.emplace(id);
        credentials.collect(tx);
        // Lengths and raw witness slices were retained by the mandatory CBOR decoder.
        for (size_t i = 0; i < tx.outputs().size(); ++i) {
            const auto &out = tx.outputs().at(i);
            observe_output(out);
            outputs.observe(out, body.output_sizes.at(i));
        }
        if (tx.collateral_return()) {
            const auto &out = *tx.collateral_return();
            outputs.observe(out, body.collateral_return_size);
            if (!out.addr().is_byron() && out.addr().network() != tx.block().config().shelley_network_id)
                throw error("collateral return network does not match the ledger");
        }
        const auto redeemer_bytes = tx.redeemers_raw();
        const auto datum_bytes = tx.datum_bytes();
        if (!redeemer_bytes.empty())
            integrity_prefix = uint8_vector { redeemer_bytes };
        else if (!datums.empty())
            integrity_prefix.push_back(0xA0); // canonical empty Conway redeemer map
        integrity_prefix.insert(integrity_prefix.end(), datum_bytes.begin(), datum_bytes.end());
    }

    void conway_validation::observe_input(const tx_out_data &out, uint32_t index)
    {
        credentials.observe_payment(out.addr(), redeemer_id { redeemer_tag::spend, index });
        has_bootstrap |= out.addr().is_byron();
        if (out.datum && std::holds_alternative<uint8_vector>(out.datum->val))
            has_inline_datum = true;
        if (out.addr().is_byron())
            return;
        const auto pay = out.addr().pay_id();
        if (pay.type != pay_ident::ident_type::SHELLEY_SCRIPT)
            return;

        const auto script = scripts.find(pay.hash);
        if (script == scripts.end() || script->second == script_type::native)
            return; // missing script is rejected by the witness domain check
        const auto *hash = out.datum ? std::get_if<datum_hash>(&out.datum->val) : nullptr;
        if (script->second == script_type::plutus_v1 && !hash)
            throw error("Plutus V1 spending input requires a datum hash");
        if (script->second == script_type::plutus_v2 && !out.datum)
            throw error("Plutus V2 spending input requires a datum");
        if (hash) {
            if (!datums.contains(*hash))
                throw error("missing spending datum witness");
            allowed_datums.emplace(*hash);
        }
    }

    static uint8_vector encode_language_views(const flat_set<script_type> &languages, const plutus_cost_models &models)
    {
        cbor::encoder encoded {};
        encoded.map(languages.size());
        // CBOR short-lex map order: integer V2/V3 tags precede V1's historical bytestring tag.
        const auto append = [&](script_type language) {
            const auto id = static_cast<uint64_t>(language) - 1;
            const auto &values = models.at(id).raw_values();
            cbor::encoder model {};
            if (language == script_type::plutus_v1)
                model.array();
            else
                model.array(values.size());
            for (const auto value: values) {
                if (value >= 0)
                    model.uint(static_cast<uint64_t>(value));
                else
                    model.nint(static_cast<uint64_t>(-(value + 1)));
            }
            if (language == script_type::plutus_v1) {
                model.s_break();
                cbor::encoder tag {};
                tag.uint(id);
                encoded.bytes(tag.cbor()).bytes(model.cbor());
            } else {
                encoded.uint(id).raw_cbor(model.cbor());
            }
        };
        for (const auto language: languages)
            if (language != script_type::plutus_v1)
                append(language);
        if (languages.contains(script_type::plutus_v1))
            append(script_type::plutus_v1);
        return std::move(encoded.cbor());
    }

    language_view_cache make_language_views(const plutus_cost_models &models)
    {
        language_view_cache views {};
        for (size_t mask = 0; mask < views.size(); ++mask) {
            flat_set<script_type> languages {};
            bool available = true;
            for (size_t id = 0; id < 3; ++id) {
                if (mask & (1U << id)) {
                    available &= models.contains(id);
                    languages.emplace(static_cast<script_type>(id + 1));
                }
            }
            if (available)
                views[mask] = encode_language_views(languages, models);
        }
        return views;
    }

    std::optional<hash_32> script_integrity_hash(buffer prefix,
        const flat_set<script_type> &languages, const plutus_cost_models &models,
        bool has_data, const language_view_cache *views)
    {
        if (!has_data && languages.empty())
            return {};
        cbor::encoder encoded {};
        encoded.raw_cbor(prefix);
        if (views) {
            size_t mask = 0;
            for (const auto language: languages) {
                const auto id = static_cast<unsigned>(language);
                if (id < 1 || id > 3)
                    throw error("unsupported Conway Plutus language");
                mask |= 1U << (id - 1);
            }
            if ((*views)[mask].empty())
                throw error("missing language view");
            encoded.raw_cbor((*views)[mask]);
        } else {
            encoded.raw_cbor(encode_language_views(languages, models));
        }
        return crypto::blake2b::digest<hash_32>(encoded.cbor());
    }

    void conway_validation::validate(const protocol_params &params, const language_view_cache *views,
        uint64_t actual_treasury) const
    {
        if (treasury && *treasury != actual_treasury)
            throw error("current treasury assertion does not match the ledger");
        flat_set<script_type> languages {};
        size_t needed_redeemers = 0;
        for (const auto &[pointer, hash]: credentials.purposes) {
            const auto script = scripts.find(hash);
            if (script == scripts.end())
                throw error("missing required script");
            if (script->second != script_type::native) {
                ++needed_redeemers;
                if (!redeemers.contains(pointer))
                    throw error("missing required redeemer pointer");
                languages.emplace(script->second);
            }
        }
        if (needed_redeemers != redeemers.size())
            throw error("unexpected redeemer pointer");
        for (const auto &hash: datums)
            if (!allowed_datums.contains(hash))
                throw error("unrelated datum witness");
        for (const auto language: languages) {
            if (!params.plutus_cost_models.contains(static_cast<uint64_t>(language) - 1)
                    || has_bootstrap || (uses_v3_features && language != script_type::plutus_v3)
                    || (has_inline_datum && language == script_type::plutus_v1))
                throw error("Plutus language is not allowed for this transaction");
        }
        if (script_integrity_hash(integrity_prefix, languages, params.plutus_cost_models,
                !redeemers.empty() || !datums.empty(), views) != integrity_hash)
            throw error("script integrity hash mismatch");
        outputs.validate(params);
    }
}
