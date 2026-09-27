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

#include <turbo/cardano/ledger/rules/certs/order.ipp>

    };

    struct chunk_indexer: chunk_indexer_one_epoch<item> {
        using chunk_indexer_one_epoch::chunk_indexer_one_epoch;
    protected:
#include <turbo/cardano/ledger/rules/ledger/index.ipp>

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
