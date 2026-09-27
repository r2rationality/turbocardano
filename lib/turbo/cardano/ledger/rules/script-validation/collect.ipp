/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Script.Validation.
// Included at namespace scope by lib/turbo/plutus/context.cpp.

    void context::prepare()
    {
        if (_prepared)
            return;
        if (!_inputs_set) [[unlikely]]
            throw error("transaction inputs must be set before transaction-context preparation");

        const auto &pv = _require_protocol_ver();
        _shared.reserve(3);
        for (const auto &[id, _]: redeemers()) {
            const auto &script = _scripts.at(redeemer_script(id));
            const auto typ = script.type();
            if (_shared.contains(typ))
                continue;
            data_encoder enc { _alloc, typ, pv };
            _shared.try_emplace(typ, enc.context_shared(*this));
        }
        _prepared = true;
    }

    prepared_script context::apply_script(allocator &&script_alloc, const script_info &script, const std::initializer_list<term> args, const std::optional<ex_units> &budget) const
    {
        const auto &pv = _require_protocol_ver();
        const flat::script s { script_alloc, script.script(), script.type(), pv.major };
        term t = s.program();
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (std::next(it) != args.end()) {
                if (script.type() == script_type::plutus_v1 || script.type() == script_type::plutus_v2)
                    t = term { script_alloc, apply { t, *it } };
            } else {
                t = term { script_alloc, apply { t, *it } };
            }
        }
        return prepared_script { std::move(script_alloc),  script.hash(), script.type(), t, s.version(), budget };
    }

    term context::term_from_datum(allocator &alc, const datum_hash &hash) const
    {
        return { alc, constant { alc, datums().at(hash) } };
    }

    term context::term_from_datum(allocator &alc, const uint8_vector &datum) const
    {
        return { alc, constant { alc, data::from_cbor(alc, datum) } };
    }

    static script_hash proposal_script_hash(const gov_action_t &ga)
    {
        if (const auto policy = cardano::ledger::rules::proposal_policy(ga))
            return *policy;
        throw error("governance action has no policy script");
    }

    prepared_script context::prepare_script(const tx_redeemer &r) const
    {
        if (!_prepared) [[unlikely]]
            throw error("transaction context must be prepared before preparing scripts");
        allocator script_alloc {};
        auto t_redeemer = term { script_alloc, constant { script_alloc, data::from_cbor(script_alloc, r.data) } };
        switch (r.tag) {
            case redeemer_tag::spend: {
                const auto &in = input_at(r.ref_idx);
                const auto addr = in.data.addr();
                if (const auto pay_id = addr.pay_id(); pay_id.type == pay_ident::ident_type::SHELLEY_SCRIPT) [[likely]] {
                    // Spending already needs this input for its datum; reuse the
                    // payment credential instead of resolving the input a second time.
                    const auto &script = _scripts.at(pay_id.hash);
                    // The formal Conway spec keeps Plutus context construction
                    // abstract. Haskell implements CIP-0069 by giving V3 scripts
                    // only ScriptContext; the spending datum is represented as
                    // Maybe Datum inside ScriptInfo rather than as a separate
                    // argument.
                    if (script.type() == script_type::plutus_v3)
                        return apply_script(std::move(script_alloc), script, { t_redeemer, data(script_alloc, script.type(), r) }, r.budget);
                    std::optional<term> t_datum {};
                    if (in.data.datum) {
                        std::visit([&](const auto &v) {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, datum_hash>) {
                                if (datums().contains(v))
                                    t_datum = term_from_datum(script_alloc, v);
                            } else if constexpr (std::is_same_v<T, uint8_vector>) {
                                t_datum = term_from_datum(script_alloc, v);
                            } else {
                                throw error(fmt::format("unsupported datum type: {}", typeid(T).name()));
                            }
                        }, in.data.datum->val);
                    }
                    if (t_datum)
                        return apply_script(std::move(script_alloc), script, { *t_datum, t_redeemer, data(script_alloc, script.type(), r) }, r.budget);
                    throw error("Plutus V1/V2 spending script is missing its datum");
                }
                throw error(fmt::format("tx {} (spend) input #{}: the output address is not a payment script: {}!", _tx->hash(), r.ref_idx, in));
            }
            case redeemer_tag::mint:
            case redeemer_tag::cert:
            case redeemer_tag::reward:
            case redeemer_tag::vote:
            case redeemer_tag::propose: {
                const auto &script = scripts().at(redeemer_script(r.id()));
                return apply_script(std::move(script_alloc), script, { t_redeemer, data(script_alloc, script.type(), r) }, r.budget);
            }
            [[unlikely]] default:
                throw error(fmt::format("tx: {} unsupported redeemer_tag: {}", _tx->hash(), static_cast<int>(r.tag)));
        }
    }
