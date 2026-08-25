#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/block.hpp>
#include <turbo/index/common.hpp>

namespace turbo::index::timed_update {
    struct stake_withdraw {
        cardano::stake_ident stake_id {};
        uint64_t amount = 0;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.stake_id, self.amount);
        }
    };
    struct reward_withdraw {
        cardano::reward_id_t reward_id {};
        uint64_t amount = 0;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.reward_id, self.amount);
        }
    };
    struct conway_tx_prelude {
        bool has_proposals = false;
        std::set<cardano::credential_t> voting_dreps {};
        std::vector<reward_withdraw> withdrawals {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.has_proposals, self.voting_dreps, self.withdrawals);
        }
    };
    struct collected_collateral_input {
        cardano::tx_hash tx_hash {};
        cardano::tx_out_idx txo_idx {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.tx_hash, self.txo_idx);
        }
    };
    struct collected_collateral_refund {
        using serialize = ::zpp::bits::members<1>;
        cardano::amount refund {};
    };
    struct donation {
        uint64_t coin = 0;
    };
    using variant = std::variant<
        cardano::stake_reg_cert,
        cardano::reg_cert,
        cardano::stake_reg_deleg_cert,
        cardano::vote_reg_deleg_cert,
        cardano::stake_vote_reg_deleg_cert,
        cardano::reg_drep_cert,
        cardano::pool_reg_cert,
        cardano::genesis_deleg_cert,
        cardano::instant_reward_cert,
        cardano::stake_deleg_cert,
        cardano::vote_deleg_cert,
        cardano::stake_vote_deleg_cert,
        cardano::auth_committee_hot_cert,
        cardano::resign_committee_cold_cert,
        cardano::update_drep_cert,
        stake_withdraw,
        cardano::stake_dereg_cert,
        cardano::pool_retire_cert,
        cardano::unreg_cert,
        cardano::unreg_drep_cert,
        cardano::param_update_proposal,
        cardano::param_update_vote,
        collected_collateral_input,
        collected_collateral_refund,
        cardano::proposal_t,
        cardano::conway::vote_info_t,
        conway_tx_prelude
    >;
    struct item {
        cardano::cert_loc_t loc {};
        variant update;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.loc, self.update);
        }

        bool operator<(const auto &b) const
        {
            if (loc.slot == b.loc.slot && loc.tx_idx == b.loc.tx_idx) {
                const auto phase = _phase(update);
                const auto other_phase = _phase(b.update);
                if (phase != other_phase)
                    return phase < other_phase;
                if (loc.cert_idx != b.loc.cert_idx)
                    return loc.cert_idx < b.loc.cert_idx;
                return update.index() < b.update.index();
            }
            return loc < b.loc;
        }

        static size_t _phase(const variant &v)
        {
            return std::visit<size_t>([](const auto &u) {
                using T = std::decay_t<decltype(u)>;
                // PV11 transaction-level effects, including withdrawals, run
                // before certificates. The legacy withdrawal remains a
                // separate update and must also precede certificates; Conway
                // ignores it at PV11 after applying the prelude instead.
                if constexpr (std::is_same_v<T, conway_tx_prelude>)
                    return 0;
                if constexpr (std::is_same_v<T, stake_withdraw>)
                    return 1;
                if constexpr (std::is_same_v<T, cardano::proposal_t>)
                    return 3;
                if constexpr (std::is_same_v<T, cardano::conway::vote_info_t>)
                    return 4;
                return 2;
            }, v);
        }
    };

    struct chunk_indexer: chunk_indexer_one_epoch<item> {
        using chunk_indexer_one_epoch::chunk_indexer_one_epoch;
    protected:
        void index_tx(const cardano::tx_base &tx) override
        {
            const auto slot = tx.block().slot();
            if (const auto *c_tx = dynamic_cast<const cardano::conway::tx *>(&tx); c_tx) {
                conway_tx_prelude prelude { .has_proposals=!c_tx->proposals().empty() };
                for (const auto &v: c_tx->votes()) {
                    if (v.voter.type == cardano::voter_t::type_t::drep_key
                            || v.voter.type == cardano::voter_t::type_t::drep_script) {
                        prelude.voting_dreps.emplace(
                            v.voter.hash,
                            v.voter.type == cardano::voter_t::type_t::drep_script);
                    }
                }
                tx.foreach_withdrawal([&](const auto &with) {
                    prelude.withdrawals.push_back({ cardano::reward_id_t { with.address.bytes() }, with.amount });
                });
                if (prelude.has_proposals || !prelude.voting_dreps.empty() || !prelude.withdrawals.empty())
                    _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), 0 }, std::move(prelude));
                size_t cert_idx = 0;
                c_tx->foreach_cert([&](const auto &cert) {
                    std::visit([&](const auto &c) {
                        _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), cert_idx++ }, c);
                    }, cert.val);
                });
                {
                    size_t prop_idx = 0;
                    for (const auto &p: c_tx->proposals())
                        _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), prop_idx++ }, p);
                }
                {
                    size_t vote_idx = 0;
                    for (const auto &v: c_tx->votes())
                        _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), vote_idx++ }, v);
                }
            } else {
                size_t cert_idx = 0;
                tx.foreach_cert([&](const auto &cert) {
                    std::visit([&](const auto &c) {
                        _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), cert_idx++ }, c);
                    }, cert.val);
                });
            }
            tx.foreach_withdrawal([&](const auto &with) {
                _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), 0 }, stake_withdraw { with.address.stake_id(), with.amount });
            });
        }

        void index_invalid_tx(const cardano::tx_base &tx) override
        {
            const auto slot = tx.block().slot();
            if (const auto *babbage_tx = dynamic_cast<const cardano::babbage::tx_base *>(&tx); babbage_tx) {
                if (const auto c_ret = babbage_tx->collateral_return(); c_ret)
                    _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), 0 }, collected_collateral_refund { c_ret->coin });
            }
            tx.foreach_collateral([&](const auto &tx_in) {
                _data.emplace_back(cardano::cert_loc_t { slot, tx.index(), 0 }, collected_collateral_input { tx_in.hash, tx_in.idx });
            });
        }

        void _index_epoch(const cardano::block_container &blk, data_type &idx) override
        {
            blk->foreach_update_proposal([&](const auto &prop) {
                idx.emplace_back(cardano::cert_loc_t { blk->slot(), 0, 0 }, prop);
            });
            blk->foreach_update_vote([&](const auto &vote) {
                idx.emplace_back(cardano::cert_loc_t { blk->slot(), 0, 0 }, vote);
            });
        }
    };
    using indexer = indexer_one_epoch<chunk_indexer>;
}
