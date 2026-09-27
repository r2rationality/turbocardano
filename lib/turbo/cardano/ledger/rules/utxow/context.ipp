/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxow.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            std::unique_ptr<context> _prep_plutus_ctx(tx_context_t &tx) const
            {
                const auto &utxos = _st.utxos();
                tx.native_script_refs.reserve(tx.inputs.size() + tx.ref_inputs.size());
                const auto resolve_input = [&](const auto &id, auto &data) {
                    if (!data) {
                        const auto it = utxos.find(id);
                        if (it == utxos.end()) [[unlikely]]
                            throw error(fmt::format("tx {} references an unknown TXO {}!", tx.tx_id, id));
                        data = it->second;
                    }
                };
                // UTXOW reference scripts come from spending and reference inputs only.
                const auto observe_reference_script = [&](const auto &data) {
                    if (data.script_ref) {
                        const auto &script = *data.script_ref;
                        const auto hash = script.hash();
                        if (tx.era == 7)
                            tx.conway.scripts.emplace(hash, script.type());
                        if (tx.era >= 7)
                            tx.reference_scripts.emplace(hash);
                        if (script.type() == script_type::native)
                            tx.native_script_refs.try_emplace(hash, script);
                    }
                };
                // process inputs before they are moved into the plutus::context
                for (auto &[id, data]: tx.inputs) {
                    resolve_input(id, data);
                    if (tx.era > 1 && tx.era != 7)
                        tx.required_signers.emplace(redeemer_tag::spend, data.addr());
                    tx.balances.in_coin += data.coin;
                    for (const auto &[policy_id, assets]: data.assets) {
                        for (const auto &[name, coin]: assets) {
                            if (coin)
                                tx.balances.in_assets[policy_id][name] += coin;
                        }
                    }
                    observe_reference_script(data);
                }
                for (auto &[id, data]: tx.ref_inputs) {
                    resolve_input(id, data);
                    observe_reference_script(data);
                    if (tx.era == 7)
                        tx.conway.observe_output(data);
                }
                if (tx.era == 7) {
                    uint32_t index = 0;
                    for (const auto &input: tx.inputs)
                        tx.conway.observe_input(input.data, index++);
                }
                cardano::ledger::rules::collateral_balance collateral {};
                for (auto &[id, data]: tx.collateral_inputs) {
                    resolve_input(id, data);
                    if (tx.era == 7)
                        tx.conway.credentials.observe_payment(data.addr());
                    else
                        tx.required_signers.emplace(redeemer_tag::spend, data.addr());
                    if (tx.era == 7 && !tx.conway.redeemers.empty())
                        collateral.add(data);
                    // Collateral neither contributes consumed value nor supplies reference scripts.
                }
                if (tx.era == 7 && !tx.conway.redeemers.empty())
                    tx.collateral_fee = collateral.validate(tx.collateral_return, tx.total_collateral,
                        tx.fee, _st.params().max_collateral_pct);
                if (tx.plutus_ctx) {
                    const auto ledger_protocol_ver = _st.params().protocol_ver;
                    tx.plutus_ctx->protocol_ver = ledger_protocol_ver;
                    tx.plutus_ctx->validate_format();
                    auto p_ctx = std::make_unique<context>(
                        std::move(tx.plutus_ctx->body), std::move(tx.plutus_ctx->wits),
                        tx.plutus_ctx->block, _cr.config()
                    );
                    p_ctx->protocol_ver(ledger_protocol_ver);
                    tx.plutus_ctx.reset();
                    p_ctx->set_inputs(std::move(tx.inputs), std::move(tx.ref_inputs));
                    return p_ctx;
                }
                return nullptr;
            }
