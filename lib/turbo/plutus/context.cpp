/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cbor/zero2.hpp>
#include <turbo/history.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/plutus/flat.hpp>
#include <turbo/plutus/machine.hpp>
#include <turbo/zpp-stream.hpp>

namespace turbo::plutus {
    using namespace cardano;

    struct context::data_encoder {
        using data = plutus::data;

        data_encoder(allocator &alloc, script_type typ, protocol_version pv): _alloc { alloc }, _typ { typ }, _pv { pv }
        {
        }

        data constr(uint64_t tag, std::initializer_list<data> il)
        {
            return data::constr(_alloc, tag, il);
        }

        data constr(uint64_t tag, data::list_type &&l)
        {
            return data::constr(_alloc, tag, std::move(l));
        }

        // a specific name to no allow collisions with int
        data boolean(bool v)
        {
            return constr(v ? 1 : 0, {});
        }

        data unsupported_conway_v1_v2_cert(const char *typ)
        {
            throw error(fmt::format("{} is not supported in a {} script context", typ, _typ));
            // A noop, to make Visual C++ happy
            return data::bint(_alloc, 0);
        }

        data conway_deposit(const uint64_t coin)
        {
            // Node compatibility: the Plutus API/spec can carry Conway deposits,
            // but cardano-ledger omitted them in protocol version 9 and keeps
            // that mainnet behavior forever for Conway-era contexts.
            if (_pv.bootstrap_phase())
                return constr(1, {});
            return constr(0, { data::bint(_alloc, coin) });
        }

        void guard_conway_features_for_v1_v2(const tx_base &tx)
        {
            if ((_typ == script_type::plutus_v1 || _typ == script_type::plutus_v2) && tx.block().era() >= 7) {
                const auto &ctx = dynamic_cast<const conway::tx_base &>(tx);
                if (!ctx.votes().empty())
                    throw error(fmt::format("voting procedures are not supported in a {} script context", _typ));
                if (!ctx.proposals().empty())
                    throw error(fmt::format("proposal procedures are not supported in a {} script context", _typ));
                if (ctx.current_treasury())
                    throw error(fmt::format("current treasury is not supported in a {} script context", _typ));
                if (ctx.donation() != 0)
                    throw error(fmt::format("treasury donation is not supported in a {} script context", _typ));
            }
        }

        void guard_reference_inputs_v1(const context &ctx)
        {
            if (_typ != script_type::plutus_v1)
                return;
            if (ctx.tx().block().era() < 7) {
                if (!ctx.ref_inputs().empty())
                    throw error("reference inputs are not supported in a plutus_v1 script context");
                return;
            }
            // Conway Haskell no longer exposes reference inputs to Plutus V1,
            // but still translates them to reject V1-incompatible outputs such
            // as Byron addresses and inline datums.
            for (const auto &in: ctx.ref_inputs())
                static_cast<void>(txo(in.data));
        }

        void guard_reference_inputs_not_spent(const context &ctx)
        {
            if (_typ != script_type::plutus_v3 || _pv.major < 11)
                return;
            // The formal Conway UTXO spec requires tx inputs and reference
            // inputs to be disjoint unconditionally. Haskell performs the
            // script-context check only from protocol version 11, so mirror the
            // node gate here.
            std::set<tx_out_ref> inputs {};
            for (const auto &in: ctx.inputs())
                inputs.emplace(in.id);
            for (const auto &in: ctx.ref_inputs()) {
                if (inputs.contains(in.id))
                    throw error(fmt::format("reference input is also spent: {}", in.id));
            }
        }

        data encode(const cpp_int &v)
        {
            return data::bint(_alloc, v);
        }

        data encode(buffer b)
        {
            return data::bstr(_alloc, b);
        }

        data encode(uint64_t u)
        {
            return data::bint(_alloc, u);
        }

        data encode(const tx_out_ref &ref)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return constr(0, {
                        constr(0, { encode(ref.hash) }),
                        encode(ref.idx)
                    });
                case script_type::plutus_v3:
                    return constr(0, {
                        encode(ref.hash),
                        encode(ref.idx)
                    });
                default: throw error(fmt::format("unsupported script_type: {}", static_cast<int>(_typ)));
            }
        }

        data encode(const pay_ident &pay_id)
        {
            switch (pay_id.type) {
                case pay_ident::ident_type::SHELLEY_KEY: return constr(0, { encode(pay_id.hash) });
                case pay_ident::ident_type::SHELLEY_SCRIPT: return constr(1, { encode(pay_id.hash) });
                default: throw error(fmt::format("an unsupported pay_id type within a script context: {}", static_cast<int>(pay_id.type)));
            }
        }

        data encode(const stake_ident &stake_id)
        {
            if (stake_id.script)
                return constr(1, { encode(stake_id.hash) });
            return constr(0, { encode(stake_id.hash) });
        }

        data stake_cred(const stake_ident &stake_id)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return constr(0, { encode(stake_id) });
                case script_type::plutus_v3:
                    return encode(stake_id);
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const drep_t &drep)
        {
            return std::visit<data>([&](const auto &cred) {
                using T = std::decay_t<decltype(cred)>;
                if constexpr (std::is_same_v<T, credential_t>)
                    return constr(0, { encode(cred) });
                if constexpr (std::is_same_v<T, drep_t::abstain_t>)
                    return constr(1, {});
                if constexpr (std::is_same_v<T, drep_t::no_confidence_t>)
                    return constr(2, {});
                throw error(fmt::format("unsupported drep type: {}", typeid(T).name()));
                return constr(2, {});
            }, drep.val);
        }

        data encode(const voter_t &voter)
        {
            switch (voter.type) {
                case voter_t::const_comm_key:
                    return constr(0, { stake_cred(stake_ident { voter.hash, false } )});
                case voter_t::const_comm_script:
                    return constr(0, { stake_cred(stake_ident { voter.hash, true } )});
                case voter_t::drep_key:
                    return constr(1, { stake_cred(stake_ident { voter.hash, false } )});
                case voter_t::drep_script:
                    return constr(1, { stake_cred(stake_ident { voter.hash, true } )});
                case voter_t::pool_key:
                    return constr(2, { encode(voter.hash) });
                default:
                    throw error(fmt::format("unsuported voter type: {}", static_cast<int>(voter.type)));
            }
        }

        data encode(const gov_action_id_t &ga_id)
        {
            return constr(0, { encode(ga_id.tx_id), data::bint(_alloc, ga_id.idx) });
        }

        data encode(const vote_t &vote)
        {
            switch (vote) {
                case vote_t::no: return constr(0, {});
                case vote_t::yes: return constr(1, {});
                case vote_t::abstain: return constr(2, {});
                default: throw error(fmt::format("unsupported vote: {}", static_cast<int>(vote)));
            }
        }

        data encode(const voting_procedure_t &vote)
        {
            return encode(vote.vote);
        }

        data encode(const redeemer_id &r, const context &ctx)
        {
            switch (r.tag) {
                case redeemer_tag::mint:
                    return constr(0, { encode(ctx.mint_at(r.ref_idx)) });
                case redeemer_tag::spend: {
                    const auto &in = ctx.input_at(r.ref_idx);
                    return constr(1, { encode(in.id) });
                }
                case redeemer_tag::reward:
                    return constr(2, { stake_cred(static_cast<stake_ident>(ctx.withdraw_at(r.ref_idx))) });
                case redeemer_tag::cert: {
                    const auto &cert = ctx.cert_at(r.ref_idx);
                    switch (_typ) {
                        case script_type::plutus_v1:
                        case script_type::plutus_v2:
                            return constr(3, { encode(cert) });
                        case script_type::plutus_v3:
                            return constr(3, { data::bint(_alloc, r.ref_idx), encode(cert) });
                        default: throw error(fmt::format("unsupported script type: {}", _typ));
                    }
                }
                case redeemer_tag::vote: {
                    return constr(4, { encode(ctx.voter_at(r.ref_idx)) });
                }
                case redeemer_tag::propose: {
                    return constr(5, { data::bint(_alloc, r.ref_idx), encode(ctx.proposal_at(r.ref_idx)) });
                }
                default: throw error(fmt::format("unsupported redeemer_tag: {}", static_cast<int>(r.tag)));
            }
        }

        data encode(const stake_ident_hybrid &stake_id)
        {
            return std::visit([&](const auto &id) {
                using T = std::decay_t<decltype(id)>;
                if constexpr (std::is_same_v<T, stake_ident>)
                    return constr(0, { encode(id) });
                if constexpr (std::is_same_v<T, stake_pointer>)
                    return constr(1, { encode(id.slot), encode(id.tx_idx), encode(id.cert_idx) });
                throw error(fmt::format("unsupported stake_id type: {}", typeid(T).name()));
                // A noop, to make Visual C++ happy
                return data::bint(_alloc, 0);
            }, stake_id);
        }

        data encode(const address &addr)
        {
            auto pay_cred = encode(addr.pay_id());
            auto stake_cred = addr.has_stake_id_hybrid()
                ? constr(0, { encode(addr.stake_id_hybrid()) })
                : constr(1, {});
            return constr(0, { std::move(pay_cred), std::move(stake_cred) });
        }

        data encode(const anchor_t &a)
        {
            return constr(0, { encode(a.url), encode(a.hash) });
        }

        template<typename T>
        data encode(const std::optional<T> &o)
        {
            if (o)
                return constr(0, { encode(*o) });
            return constr(1, {});
        }

        template<typename T>
        data encode(const nil_optional_t<T> &o)
        {
            if (o)
                return constr(0, { encode(*o) });
            return constr(1, {});
        }

        data encode(const protocol_version &pv)
        {
            return constr(0, { encode(pv.major), encode(pv.minor) });
        }

        data encode(const rational_u64 &r)
        {
            return constr(0, { encode(r.numerator), encode(r.denominator) });
        }

        data encode(const constitution_t &c)
        {
            return encode(c.policy_id);
        }

        data encode(const stake_reg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return constr(0, { stake_cred(c.stake_id) });
                case script_type::plutus_v3:
                    return constr(0, { stake_cred(c.stake_id), constr(1, {}) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const genesis_deleg_cert &c)
        {
            throw error(fmt::format("data serialization not implemented for: {}", typeid(c).name()));
        }

        data encode(const instant_reward_cert &c)
        {
            throw error(fmt::format("data serialization not implemented for: {}", typeid(c).name()));
        }

        data encode(const reg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return constr(0, { stake_cred(c.stake_id) });
                case script_type::plutus_v3:
                    return constr(0, { stake_cred(c.stake_id), conway_deposit(c.deposit) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const stake_dereg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return constr(1, { stake_cred(c.stake_id) });
                case script_type::plutus_v3:
                    return constr(1, { stake_cred(c.stake_id), constr(1, {}) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const unreg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return constr(1, { stake_cred(c.stake_id) });
                case script_type::plutus_v3:
                    return constr(1, { stake_cred(c.stake_id), conway_deposit(c.deposit) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const stake_deleg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return constr(2, { stake_cred(c.stake_id), encode(c.pool_id) });
                case script_type::plutus_v3:
                    return constr(2, { stake_cred(c.stake_id), constr(0, { encode(c.pool_id) }) });
                default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(_typ)));
            }
        }

        data encode(const vote_deleg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("vote_deleg_cert");
                case script_type::plutus_v3:
                    return constr(2, { encode(c.stake_id), constr(1, { encode(c.drep) }) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const stake_vote_deleg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("stake_vote_deleg_cert");
                case script_type::plutus_v3:
                    return constr(2, { encode(c.stake_id), constr(2, { encode(c.pool_id), encode(c.drep) }) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const stake_reg_deleg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("stake_reg_deleg_cert");
                case script_type::plutus_v3:
                    return constr(3, { encode(c.stake_id), constr(0, { encode(c.pool_id) }), data::bint(_alloc, c.deposit) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const vote_reg_deleg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("vote_reg_deleg_cert");
                case script_type::plutus_v3:
                    return constr(3, { encode(c.stake_id), constr(1, { encode(c.drep) }), data::bint(_alloc, c.deposit) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const stake_vote_reg_deleg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("stake_vote_reg_deleg_cert");
                case script_type::plutus_v3:
                    return constr(3, { encode(c.stake_id), constr(2, { encode(c.pool_id), encode(c.drep) }), data::bint(_alloc, c.deposit) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const reg_drep_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("reg_drep_cert");
                case script_type::plutus_v3:
                    return constr(4, { encode(c.drep_id), data::bint(_alloc, c.deposit) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const update_drep_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("update_drep_cert");
                case script_type::plutus_v3:
                    return constr(5, { encode(c.drep_id) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const unreg_drep_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("unreg_drep_cert");
                case script_type::plutus_v3:
                    return constr(6, { encode(c.drep_id), data::bint(_alloc, c.deposit) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const pool_reg_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return constr(3, { encode(c.pool_id), encode(c.params.vrf_vkey) });
                case script_type::plutus_v3:
                    return constr(7, { encode(c.pool_id), encode(c.params.vrf_vkey) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const pool_retire_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return constr(4, { encode(c.pool_id), encode(static_cast<uint64_t>(c.epoch)) });
                case script_type::plutus_v3:
                    return constr(8, { encode(c.pool_id), encode(static_cast<uint64_t>(c.epoch)) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const auth_committee_hot_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("auth_committee_hot_cert");
                case script_type::plutus_v3:
                    return constr(9, { encode(c.cold_id), encode(c.hot_id) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const resign_committee_cold_cert &c)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return unsupported_conway_v1_v2_cert("resign_committee_cold_cert");
                case script_type::plutus_v3:
                    return constr(10, { encode(c.cold_id) });
                default:
                    throw error(fmt::format("unsupported script type: {}", _typ));
            }
        }

        data encode(const cert_t &cert)
        {
            return std::visit<data>([&](const auto &c) {
                return encode(c);
            }, cert.val);
        }

        data encode(const plutus_cost_model &m)
        {
            data::list_type l { _alloc };
            for (const auto value: m.raw_values())
                l.emplace_back(data::bint(_alloc, cpp_int { value }));
            return data::list(_alloc, std::move(l));
        }

        data encode(const plutus_cost_models &mdls)
        {
            data::map_type m { _alloc };
            for (const auto &[id, model]: mdls.items)
                m.emplace_back(_alloc, data::bint(_alloc, id), encode(model));
            return data::map(_alloc, std::move(m));
        }

        data encode(const param_update_t &u)
        {
            // Haskell encodes ChangedParameters with ToPlutusData for the full
            // PParamsUpdate. Keep this local subset explicit until the whole
            // PParamsUpdate PlutusData encoding is mirrored here.
            data::map_type m { _alloc };
            if (u.plutus_cost_models)
                m.emplace_back(_alloc, data::bint(_alloc, 18), encode(*u.plutus_cost_models));
            return data::map(_alloc, std::move(m));
        }

        data encode(const gov_action_t::parameter_change_t &p)
        {
            return constr(0, {
                encode(p.prev_action_id),
                encode(p.update),
                encode(p.policy_id)
            });
        }

        data encode(const gov_action_t::hard_fork_init_t &h)
        {
            return constr(1, { encode(h.prev_action_id), encode(h.protocol_ver) });
        }

        data encode(const gov_action_t::treasury_withdrawals_t &t)
        {
            data::map_type m { _alloc };
            for (const auto &[reward_id, coin]: t.withdrawals)
                m.emplace_back(_alloc, encode(static_cast<stake_ident>(reward_id)), encode(coin));
            return constr(2, { data::map(_alloc, std::move(m)), encode(t.policy_id) });
        }

        data encode(const gov_action_t::no_confidence_t &n)
        {
            return constr(3, { encode(n.prev_action_id) });
        }

        data encode(const gov_action_t::update_committee_t &u)
        {
            data::list_type removed { _alloc };
            for (const auto &cred: u.members_to_remove)
                removed.emplace_back(encode(cred));
            data::map_type added { _alloc };
            for (const auto &[cred, epoch]: u.members_to_add)
                added.emplace_back(_alloc, encode(cred), encode(epoch));
            return constr(4, {
                encode(u.prev_action_id),
                data::list(_alloc, std::move(removed)),
                data::map(_alloc, std::move(added)),
                encode(u.new_threshold)
            });
        }

        data encode(const gov_action_t::new_constitution_t &n)
        {
            return constr(5, { encode(n.prev_action_id), encode(n.new_constitution) });
        }

        data encode(const gov_action_t::info_action_t &)
        {
            return constr(6, {});
        }

        data encode(const gov_action_t &ga)
        {
            return std::visit<data>([&](const auto &a) {
                return encode(a);
            }, ga.val);
        }

        data encode(const proposal_t &p)
        {
            return constr(0, {
                data::bint(_alloc, p.procedure.deposit),
                encode(p.procedure.return_addr),
                encode(p.procedure.action)
            });
        }

        data value(const uint64_t coin, const multi_asset_map &assets)
        {
            data::map_type m { _alloc };
            m.emplace_back(_alloc,
                encode(uint8_vector {}),
                data::map(_alloc, { data_pair { _alloc, encode(uint8_vector {}), encode(coin) } })
            );
            if (!assets.empty()) {
                for (const auto &[policy_id, p_assets] : assets) {
                    bstr_type::value_type policy_id_bytes { _alloc };
                    policy_id_bytes = policy_id;
                    data::map_type pm { _alloc };
                    for (const auto &[name, coin]: p_assets) {
                        bstr_type::value_type bytes { _alloc };
                        bytes = name.span();
                        pm.emplace_back(_alloc, encode(std::move(bytes)), data::bint(_alloc, cpp_int { coin }));
                    }
                    m.emplace_back(_alloc, encode(policy_id_bytes), data::map(_alloc, std::move(pm)));
                }
            }
            return data::map(_alloc, std::move(m));
        }

        data datum(const std::optional<datum_option_t> &datum)
        {
            if (datum) {
                return std::visit([&](const auto &d) {
                    using T = std::decay_t<decltype(d)>;
                    if constexpr (std::is_same_v<T, datum_hash>)
                        return constr(0, { encode(d) });
                    throw error(fmt::format("unsupported datum type: {}", typeid(T).name()));
                    // Make Visual C++ happy
                    return data::bint(_alloc, 0);
                }, datum->val);
            }
            return constr(1, {});
        }

        data datum_option(const std::optional<datum_option_t> &datum)
        {
            if (datum) {
                return std::visit([&](const auto &d) {
                    using T = std::decay_t<decltype(d)>;
                    if constexpr (std::is_same_v<T, datum_hash>) {
                        return constr(1, { encode(d) });
                    } else if constexpr (std::is_same_v<T, uint8_vector>) {
                        return constr(2, { data::from_cbor(_alloc, d) });
                    } else {
                        throw error(fmt::format("unsupported datum type: {}", typeid(T).name()));
                    }
                }, datum->val);
            }
            return constr(0, {});
        }

        data datum_value(const context &ctx, const std::optional<datum_option_t> &datum)
        {
            if (datum) {
                return std::visit([&](const auto &d) {
                    using T = std::decay_t<decltype(d)>;
                    if constexpr (std::is_same_v<T, datum_hash>) {
                        if (const auto it = ctx.datums().find(d); it != ctx.datums().end())
                            return constr(0, { it->second });
                        return constr(1, {});
                    } else if constexpr (std::is_same_v<T, uint8_vector>) {
                        return constr(0, { data::from_cbor(_alloc, d) });
                    } else {
                        throw error(fmt::format("unsupported datum type: {}", typeid(T).name()));
                    }
                }, datum->val);
            }
            return constr(1, {});
        }

        data script_ref(const std::optional<script_info> &script_ref)
        {
            if (script_ref)
                return constr(0, { encode(script_ref->hash()) });
            return constr(1, {});
        }

        data txo(const tx_out_data &txo)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                    return constr(0, {
                        encode(txo.addr()),
                        value(txo.coin, txo.assets),
                        datum(txo.datum)
                    });
                case script_type::plutus_v2:
                case script_type::plutus_v3:
                    return constr(0, {
                        encode(txo.addr()),
                        value(txo.coin, txo.assets),
                        datum_option(txo.datum),
                        script_ref(txo.script_ref)
                    });
                default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(_typ)));
            }
        }

        data datums(const context::datum_map &datums)
        {
            switch (_typ) {
                case script_type::plutus_v1: {
                    data::list_type l { _alloc };
                    for (const auto &[hash, d]: datums) {
                        l.emplace_back(constr(0, { encode(hash), d }));
                    }
                    return data::list(_alloc, std::move(l));
                }
                case script_type::plutus_v2:
                case script_type::plutus_v3: {
                    data::map_type m { _alloc };
                    for (const auto &[hash, d]: datums) {
                        m.emplace_back(data_pair { _alloc, encode(hash), d });
                    }
                    return data::map(_alloc, std::move(m));
                }
                default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(_typ)));
            }
        }

        data redeemers(const context &ctx)
        {
            data::map_type m { _alloc };
            for (const auto &[rid, rinfo]: ctx.redeemers()) {
                m.emplace_back(data_pair { _alloc, encode(rid, ctx), data::from_cbor(_alloc, rinfo.data) });
            }
            return data::map(_alloc, std::move(m));
        }

        data inputs(const stored_txo_list &inputs)
        {
            data::list_type l { _alloc };
            for (const auto &in: inputs) {
                auto d_ref = encode(in.id);
                auto d_data = txo(in.data);
                l.emplace_back(constr(0, { std::move(d_ref), std::move(d_data) }));
            }
            return data::list(_alloc, std::move(l));
        }

        data outputs(const tx_base &tx)
        {
            data::list_type l { _alloc };
            tx.foreach_output([&](const tx_output &txout) {
                l.emplace_back(txo(txout));
            });
            return data::list(_alloc, std::move(l));
        }

        data fee(const tx_base &tx)
        {
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                    return data::map(_alloc, {
                        data_pair {_alloc,
                            encode(uint8_vector {}),
                            data::map(_alloc, {
                                data_pair { _alloc, encode(uint8_vector {}), encode(tx.fee()) }
                            })
                        }
                    });
                case script_type::plutus_v3:
                    return encode(tx.fee());
                default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(_typ)));
            }
        }

        data mints(const tx_base &tx)
        {
            data::map_type m { _alloc };
            switch (_typ) {
                case script_type::plutus_v1:
                case script_type::plutus_v2:
                        m.emplace_back(_alloc,
                            encode(uint8_vector {}),
                            data::map(_alloc, { data_pair { _alloc, encode(uint8_vector {}), encode(0) } }));
                    break;
                case script_type::plutus_v3:
                    // do nothing
                    break;
                default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(_typ)));
            }
            tx.foreach_mint([&](const auto &policy_id, const auto &policy_assets) {
                data::map_type a_m { _alloc };
                for (const auto &[name, value]: policy_assets) {
                    a_m.emplace_back(_alloc, encode(name.span()), encode(cpp_int { value }));
                }
                std::sort(a_m.begin(), a_m.end(), [](const auto &a, const auto &b) {
                    return *std::get<data::bstr_type>(*a->first) < *std::get<data::bstr_type>(*b->first);
                });
                m.emplace_back(_alloc, encode(policy_id), data::map(_alloc, std::move(a_m)));
            });
            std::sort(m.begin(), m.end(), [](const auto &a, const auto &b) {
                return *std::get<data::bstr_type>(*a->first) < *std::get<data::bstr_type>(*b->first);
            });
            return data::map(_alloc, std::move(m));
        }

        data certs(const tx_base &tx)
        {
            data::list_type l { _alloc };
            tx.foreach_cert([&](const auto &cert) {
                std::visit([&](const auto &cv) {
                    l.emplace_back(encode(cv));
                }, cert.val);
            });
            return data::list(_alloc, std::move(l));
        }

        data withdrawals(const tx_base &tx)
        {
            switch (_typ) {
                case script_type::plutus_v1: {
                    data::list_type l { _alloc };
                    tx.foreach_withdrawal([&](const tx_withdrawal &w) {
                        l.emplace_back(constr(0, {
                            stake_cred(w.address.stake_id()),
                            encode(static_cast<uint64_t>(w.amount))
                        }));
                    });
                    return data::list(_alloc, std::move(l));
                }
                case script_type::plutus_v2: {
                    data::map_type m { _alloc };
                    tx.foreach_withdrawal([&](const tx_withdrawal &w) {
                        m.emplace_back(data_pair { _alloc,
                            encode(w.address.stake_id_hybrid()),
                            encode(static_cast<uint64_t>(w.amount))
                        });
                    });
                    return data::map(_alloc, std::move(m));
                }
                case script_type::plutus_v3: {
                    data::map_type m { _alloc };
                    tx.foreach_withdrawal([&](const tx_withdrawal &w) {
                        m.emplace_back(data_pair { _alloc,
                            stake_cred(w.address.stake_id()),
                            encode(static_cast<uint64_t>(w.amount))
                        });
                    });
                    return data::map(_alloc, std::move(m));
                }
                default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(_typ)));
            }
        }

        data slot(const context &ctx, const uint64_t slot)
        {
            return data::bint(_alloc, cardano::slot { slot, ctx.config() }.unixtime() * 1000);
        }

        data validity_start(const context &ctx, const std::optional<uint64_t> start)
        {
            if (start)
                return constr(0, { constr(1, { slot(ctx, *start) }), constr(1, {}) });
            return constr(0, { constr(0, {}), constr(1, {}) });;
        }

        data validity_end(const context &ctx, const std::optional<uint64_t> end, const std::optional<uint64_t> start)
        {
            if (end) {
                // Conway Haskell uses strictUpperBound even without a lower
                // bound. Older node translations used PV1.to in that case, so
                // preserve the historical inclusive bound before Conway.
                const auto strict_upper = start || ctx.tx().block().era() >= 7;
                return constr(0, { constr(1, { slot(ctx, *end) }), constr(strict_upper ? 0 : 1, {}) });
            }
            return constr(0, { constr(2, {}), constr(1, {}) });
        }

        data validity_range(const context &ctx)
        {
            const auto end = ctx.tx().validity_end();
            const auto start = ctx.tx().validity_start();
            return constr(0, {
                validity_start(ctx, start),
                validity_end(ctx, end, start)
            });
        }

        data signatories(const tx_base &tx)
        {
            // ensure the sorted order
            std::set<key_hash> vkeys {};
            tx.foreach_required_signer([&](const auto &vkey_hash) {
                vkeys.emplace(vkey_hash);
            });
            data::list_type l { _alloc };
            for (const auto &vkey : vkeys) {
                l.emplace_back(encode(vkey));
            }
            return data::list(_alloc, std::move(l));
        }

        data purpose(const context &ctx, const redeemer_id &r)
        {
            switch (r.tag) {
                case redeemer_tag::spend: {
                    const auto &in = ctx.input_at(r.ref_idx);
                    switch (_typ) {
                        case script_type::plutus_v1:
                        case script_type::plutus_v2:
                            return constr(1, { encode(in.id) });
                        case script_type::plutus_v3:
                            return constr(1, { encode(in.id), datum_value(ctx, in.data.datum) });
                        default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(_typ)));
                    }
                }
                case redeemer_tag::cert: {
                    const auto &cert = ctx.cert_at(r.ref_idx);
                    switch (_typ) {
                        case script_type::plutus_v1:
                        case script_type::plutus_v2:
                            return constr(3, { encode(cert) });
                        case script_type::plutus_v3:
                            return constr(3, { data::bint(_alloc, r.ref_idx), encode(cert) });
                        default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(_typ)));
                    }
                }
                default: return encode(r, ctx);
            }
        }

        data proposals(const context &ctx)
        {
            data::list_type l { _alloc };
            for (const auto &p: ctx.proposals()) {
                l.emplace_back(encode(p));
            }
            return data::list(_alloc, std::move(l));
        }

        data votes(const context &ctx)
        {
            data::map_type m { _alloc };
            const voter_t *prev_voter = nullptr;
            data::map_type votes_by_action { _alloc };
            const auto flush_voter = [&] {
                if (prev_voter)
                    m.emplace_back(_alloc, encode(*prev_voter), data::map(_alloc, std::move(votes_by_action)));
                votes_by_action = data::map_type { _alloc };
            };
            for (const auto &v: ctx.votes()) {
                if (!prev_voter || (v.voter <=> *prev_voter) != std::strong_ordering::equal) {
                    flush_voter();
                    prev_voter = &v.voter;
                }
                votes_by_action.emplace_back(_alloc, encode(v.action_id), encode(v.voting_procedure));
            }
            flush_voter();
            return data::map(_alloc, std::move(m));
        }

        data current_treasury(const tx_base &tx)
        {
            const auto &ctx = dynamic_cast<const conway::tx_base &>(tx);
            if (const auto treasury = ctx.current_treasury())
                return constr(0, { encode(*treasury) });
            return constr(1, {});
        }

        data donation(const tx_base &tx)
        {
            if (const auto coin = tx.donation(); coin != 0)
                return constr(0, { encode(coin) });
            return constr(1, {});
        }

        data redeemer(const tx_redeemer &r)
        {
            return data::from_cbor(_alloc, r.data);
        }

        data context_shared_v1(const context &ctx)
        {
            guard_conway_features_for_v1_v2(ctx.tx());
            guard_reference_inputs_v1(ctx);
            return constr(0, {
                inputs(ctx.inputs()),
                outputs(ctx.tx()),
                fee(ctx.tx()),
                mints(ctx.tx()),
                certs(ctx.tx()),
                withdrawals(ctx.tx()),
                validity_range(ctx),
                signatories(ctx.tx()),
                datums(ctx.datums()),
                constr(0, { encode(ctx.tx().hash()) })
            });
        }

        data context_shared_v2(const context &ctx)
        {
            guard_conway_features_for_v1_v2(ctx.tx());
            return constr(0, {
                inputs(ctx.inputs()),
                inputs(ctx.ref_inputs()),
                outputs(ctx.tx()),
                fee(ctx.tx()),
                mints(ctx.tx()),
                certs(ctx.tx()),
                withdrawals(ctx.tx()),
                validity_range(ctx),
                signatories(ctx.tx()),
                redeemers(ctx),
                datums(ctx.datums()),
                constr(0, { encode(ctx.tx().hash()) })
            });
        }

        data context_shared_v3(const context &ctx)
        {
            guard_reference_inputs_not_spent(ctx);
            return constr(0, {
                inputs(ctx.inputs()),
                inputs(ctx.ref_inputs()),
                outputs(ctx.tx()),
                fee(ctx.tx()),
                mints(ctx.tx()),
                certs(ctx.tx()),
                withdrawals(ctx.tx()),
                validity_range(ctx),
                signatories(ctx.tx()),
                redeemers(ctx),
                datums(ctx.datums()),
                encode(ctx.tx().hash()),
                votes(ctx),
                proposals(ctx),
                current_treasury(ctx.tx()),
                donation(ctx.tx())
            });
        }

        data context_v1(const context &ctx, const data &ctx_shared, const tx_redeemer &r)
        {
            return constr(0, { ctx_shared, purpose(ctx, r.id()) });
        }

        data context_v2(const context &ctx, const data &ctx_shared, const tx_redeemer &r)
        {
            return constr(0, { ctx_shared, purpose(ctx, r.id()) });
        }

        data context_v3(const context &ctx, const data &ctx_shared, const tx_redeemer &r)
        {
            return constr(0, { ctx_shared, redeemer(r), purpose(ctx, r.id()) });
        }

        data context_shared(const context &ctx)
        {
            switch (_typ) {
                case script_type::plutus_v1: return context_shared_v1(ctx);
                case script_type::plutus_v2: return context_shared_v2(ctx);
                case script_type::plutus_v3: return context_shared_v3(ctx);
                default: throw error(fmt::format("unsupported script_type: {}", static_cast<int>(_typ)));
            }
        }

        data context(const context &ctx, const data &ctx_shared, const tx_redeemer &r)
        {
            switch (_typ) {
                case script_type::plutus_v1: return context_v1(ctx, ctx_shared, r);
                case script_type::plutus_v2: return context_v2(ctx, ctx_shared, r);
                case script_type::plutus_v3: return context_v3(ctx, ctx_shared, r);
                default: throw error(fmt::format("unsupported script_type: {}", static_cast<int>(_typ)));
            }
        }
    private:
        allocator &_alloc;
        script_type _typ;
        protocol_version _pv;
    };

    static std::optional<protocol_version> protocol_ver_for_era(const uint64_t era)
    {
        switch (era) {
            case 0:
            case 1:
                return protocol_version { 1, 0 };
            case 2:
                return protocol_version { 2, 0 };
            case 3:
                return protocol_version { 3, 0 };
            case 4:
                return protocol_version { 4, 0 };
            case 5:
                return protocol_version { 6, 0 };
            case 6: return {};
            case 7: return {};
            default:
                throw error(fmt::format("unsupported era for Plutus context protocol version fallback: {}", era));
        }
    }

    const protocol_version &context::_require_protocol_ver() const
    {
        if (_protocol_ver)
            return *_protocol_ver;
        throw error(fmt::format(
            "the protocol version must be set explicitly for era {} because the era spans multiple protocol versions",
            _block_info.era));
    }

    context::context(uint8_vector &&tx_body_data, uint8_vector &&tx_wits_data, const storage::block_info &block, const cardano::config &cfg):
        _cfg { cfg },
        _tx_body_bytes { std::move(tx_body_data) },
        _tx_wits_bytes { std::move(tx_wits_data) },
        _block_info { block },
        _protocol_ver { protocol_ver_for_era(block.era) },
        _tx { block, block.offset + block.header_offset, cbor::zero2::parse(_tx_body_bytes).get(), cbor::zero2::parse(_tx_wits_bytes).get(), 0, _cfg }
    {
        _tx->foreach_redeemer([&](const auto &r) {
            const auto [it, created] = _redeemers.try_emplace(redeemer_id { r.tag, r.ref_idx }, r);
            // There are in fact transactions with duplicate redeemers, such as E8D403D9D2AD5F1CD4D3327F48E34E717B66AC4E4764765BA3A5843758B696E4
            if (!created)
                it->second = r;
        });
    }

    context::context(uint8_vector &&tx_body_data, uint8_vector &&tx_wits_data, const storage::block_info &block,
            stored_txo_list &&inputs_, stored_txo_list &&ref_inputs_, const cardano::config &c_cfg):
        context { std::move(tx_body_data), std::move(tx_wits_data), block, c_cfg }
    {
        set_inputs(std::move(inputs_), std::move(ref_inputs_));
    }

    context::context(stored_tx_context &&ctx, const cardano::config &c_cfg):
        context { std::move(ctx.body), std::move(ctx.wits), ctx.block,
                  std::move(ctx.inputs), std::move(ctx.ref_inputs), c_cfg }
    {
    }

    static stored_tx_context context_load(const std::string &path)
    {
        zpp_stream::read_stream s { path };
        return s.read<stored_tx_context>();
    }

    context::context(const std::string &path, const cardano::config &c_cfg):
        context { context_load(path), c_cfg }
    {
    }

    void context::set_inputs(stored_txo_list &&inputs_, stored_txo_list &&ref_inputs_)
    {
        _inputs = std::move(inputs_);
        _ref_inputs = std::move(ref_inputs_);
        _tx->foreach_script([&](auto &&s) {
            _scripts.try_emplace(s.hash(), std::move(s));
        }, this);
        _tx->foreach_datum([&](const auto &d) {
            _datums.try_emplace(d.hash, data::from_cbor(alloc(), d.data));
        });
    }

    const tx_base &context::tx() const
    {
        return *_tx;
    }

    prepared_script context::apply_script(allocator &&script_alloc, const script_info &script, const std::initializer_list<term> args, const std::optional<ex_units> &budget) const
    {
        const auto &pv = _require_protocol_ver();
        const flat::script s { script_alloc, script.script(), script.type(), pv.major };
        term t = s.program();
        for (auto it = args.begin(); it != args.end(); ++it) {
            // Uncomment to debug potential script context generation issues
            // file::write(install_path(fmt::format("tmp/script-{}-{}-args-my-{}.txt", script.hash(), script.type(), it - args.begin())), fmt::format("{}\n", *it));
            if (std::next(it) != args.end()) {
                if (script.type() == script_type::plutus_v1 || script.type() == script_type::plutus_v2)
                    t = term { script_alloc, apply { t, *it } };
            } else {
                t = term { script_alloc, apply { t, *it } };
            }
        }
        // Uncomment to debug potential script context generation issues
        // file::write(install_path("tmp/script-with-args.uplc"), fmt::format("(program {} {})", s_it->second.ver, t));
        // file::write(install_path("tmp/script-with-args.flat"), flat::encode_cbor(s_it->second.ver, t));
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
        return std::visit<script_hash>([](const auto &a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, gov_action_t::parameter_change_t>
                    || std::is_same_v<T, gov_action_t::treasury_withdrawals_t>) {
                if (!a.policy_id)
                    throw error(fmt::format("gov_action type {} has no policy script", typeid(T).name()));
                return *a.policy_id;
            }
            throw error(fmt::format("unsupported gov_action type: {}", typeid(T).name()));
            // Unreachable and needed only to Make Visual C++ happy
            return script_hash {};
        }, ga.val);
    }

    prepared_script context::prepare_script(const tx_redeemer &r) const
    {
        allocator script_alloc {};
        auto t_redeemer = term { script_alloc, constant { script_alloc, data::from_cbor(script_alloc, r.data) } };
        switch (r.tag) {
            case redeemer_tag::spend: {
                const auto &in = input_at(r.ref_idx);
                const auto addr = in.data.addr();
                if (const auto pay_id = addr.pay_id(); pay_id.type == pay_ident::ident_type::SHELLEY_SCRIPT) [[likely]] {
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
                    return apply_script(std::move(script_alloc), script, { t_redeemer, data(script_alloc, script.type(), r) }, r.budget);
                }
                throw error(fmt::format("tx {} (spend) input #{}: the output address is not a payment script: {}!", _tx->hash(), r.ref_idx, in));
                break;
            }
            case redeemer_tag::mint: {
                const auto policy_id = mint_at(r.ref_idx);
                const auto &script = scripts().at(policy_id);
                return apply_script(std::move(script_alloc), script, { t_redeemer, data(script_alloc, script.type(), r) }, r.budget);
            }
            case redeemer_tag::cert: {
                const auto &script = scripts().at(cert_cred_at(r.ref_idx).hash);
                return apply_script(std::move(script_alloc), script, { t_redeemer, data(script_alloc, script.type(), r) }, r.budget);
            }
            case redeemer_tag::reward: {
                const auto &script = scripts().at(withdraw_at(r.ref_idx).hash());
                return apply_script(std::move(script_alloc), script, { t_redeemer, data(script_alloc, script.type(), r) }, r.budget);
            }
            case redeemer_tag::vote: {
                const auto &script = scripts().at(voter_at(r.ref_idx).hash);
                return apply_script(std::move(script_alloc), script, { t_redeemer, data(script_alloc, script.type(), r) }, r.budget);
            }
            case redeemer_tag::propose: {
                const auto &p = proposal_at(r.ref_idx);
                const auto &script = scripts().at(proposal_script_hash(p.procedure.action));
                return apply_script(std::move(script_alloc), script, { t_redeemer, data(script_alloc, script.type(), r) }, r.budget);
            }
            default:
                throw error(fmt::format("tx: {} unsupported redeemer_tag: {}", _tx->hash(), static_cast<int>(r.tag)));
        }
    }

    ex_units context::validate_redeemer_budgets(const redeemer_map &redeemers, const ex_units &limit)
    {
        ex_units total {};
        for (const auto &[id, redeemer]: redeemers) {
            if (redeemer.budget.mem > limit.mem - total.mem
                    || redeemer.budget.steps > limit.steps - total.steps) [[unlikely]] {
                throw error(fmt::format(
                    "the total redeemer execution-unit budget exceeds maxTxExUnits {} at {}#{}: "
                    "accumulated {{ mem: {}, steps: {} }}, next {}",
                    limit, id.tag, id.ref_idx, total.mem, total.steps, redeemer.budget));
            }
            total.mem += redeemer.budget.mem;
            total.steps += redeemer.budget.steps;
        }
        return total;
    }

    void context::eval_script(prepared_script &ps) const
    {
        try {
            const auto &pv = _require_protocol_ver();
            machine m { ps.alloc, cost_models().for_script(ps.typ, builtins::semantics_variant(ps.typ, pv.major)), ps.typ, ps.budget, pv.major };
            m.evaluate_no_res(ps.expr);
        } catch (const std::exception &ex) {
            throw error(fmt::format("script {} {}: {}", ps.typ, ps.hash, ex.what()));
        }
    }

    script_hash context::redeemer_script(const redeemer_id &r) const
    {
        switch (r.tag) {
            case redeemer_tag::spend: {
                const auto &in = input_at(r.ref_idx);
                const auto addr = in.data.addr();
                if (const auto pay_id = addr.pay_id(); pay_id.type == pay_ident::ident_type::SHELLEY_SCRIPT) [[likely]]
                    return pay_id.hash;
                throw error(fmt::format("tx {} (spend) input #{}: the output address is not a payment script: {}!", _tx->hash(), r.ref_idx, in));
            }
            case redeemer_tag::mint: return mint_at(r.ref_idx);
            case redeemer_tag::cert: return cert_cred_at(r.ref_idx).hash;
            case redeemer_tag::reward: return withdraw_at(r.ref_idx).hash();
            case redeemer_tag::vote: return voter_at(r.ref_idx).hash;
            case redeemer_tag::propose: {
                const auto &p = proposal_at(r.ref_idx);
                return proposal_script_hash(p.procedure.action);
            }
            default:
                throw error(fmt::format("tx: {} unsupported redeemer_tag: {}", _tx->hash(), static_cast<int>(r.tag)));
        };
    }

    template<typename T>
    typename T::mapped_type nth_or_die(const T &t, size_t idx)
    {
        const auto it = t.nth(idx);
        if (!it)
            throw error(fmt::format("nth_or_die: invalid index: {}", idx));
        return it;
    }

    const reward_id_t &context::withdraw_at(const uint64_t r_idx) const
    {
        return dynamic_cast<const shelley::tx_base &>(*_tx).withdrawals().nth_or_die(r_idx).first;
    }

    const cert_t &context::cert_at(const uint64_t r_idx) const
    {
        return dynamic_cast<const shelley::tx_base &>(*_tx).certs().at(r_idx);
    }

    credential_t context::cert_cred_at(const uint64_t r_idx) const
    {
        return cert_at(r_idx).signing_cred().value();
    }

    buffer context::mint_at(const uint64_t r_idx) const
    {
        return dynamic_cast<const mary::tx_base &>(*_tx).mints().nth_or_die(r_idx).first;
    }

    const stored_txo &context::input_at(const uint64_t r_idx) const
    {
        return _inputs.at(r_idx);
    }

    const conway::proposal_set &context::proposals() const
    {
        return dynamic_cast<const conway::tx_base &>(*_tx).proposals();
    }

    const proposal_t &context::proposal_at(const uint64_t r_idx) const
    {
        return proposals().nth_or_die(r_idx);
    }

    const voter_t &context::voter_at(const uint64_t r_idx) const
    {
        return votes().nth_or_die(r_idx).voter;
    }

    const conway::vote_set &context::votes() const
    {
        return dynamic_cast<const conway::tx_base &>(*_tx).votes();
    }

    term context::data(allocator &script_alloc, const script_type typ, const tx_redeemer &r) const
    {
        // allocate the per-script data with the per-script allocator
        // but allocate the shared date with the per-context allocator
        const auto &pv = _require_protocol_ver();
        data_encoder enc { script_alloc, typ, pv };
        auto shared_it = _shared.find(typ);
        if (shared_it == _shared.end()) {
            data_encoder enc_shared { alloc(), typ, pv };
            auto [new_it, created] = _shared.try_emplace(typ, enc_shared.context_shared(*this));
            shared_it = new_it;
        }
        return { script_alloc, constant { script_alloc, enc.context(*this, shared_it->second, r) } };
    }
}
