#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <string_view>

namespace turbo::cardano::ledger::conway::rules {
    // Stable identifiers use the Agda transition-system and constructor names;
    // the one unsupported value marks signals outside the Conway system.
    enum class rule_id {
        deleg_delegate,
        deleg_dereg,
        deleg_reg,
        pool_regpool,
        pool_retirepool,
        govcert_regdrep,
        govcert_deregdrep,
        govcert_ccreghot,
        govcert_resign_cold,
        govcert_update_drep,
        cert_deleg,
        cert_pool,
        cert_vdel,
        gov_vote,
        gov_propose,
        ratify_accept,
        ratify_reject,
        ratify_continue,
        enact_no_conf,
        enact_upd_comm,
        enact_new_const,
        enact_hf,
        enact_pparams,
        enact_wdrl,
        enact_info,
        epoch,
        newepoch_new,
        newepoch_not_new,
        newepoch_no_reward_update,
        unsupported_certificate
    };

    constexpr std::string_view name(const rule_id id)
    {
        switch (id) {
            case rule_id::deleg_delegate: return "DELEG-delegate";
            case rule_id::deleg_dereg: return "DELEG-dereg";
            case rule_id::deleg_reg: return "DELEG-reg";
            case rule_id::pool_regpool: return "POOL-regpool";
            case rule_id::pool_retirepool: return "POOL-retirepool";
            case rule_id::govcert_regdrep: return "GOVCERT-regdrep";
            case rule_id::govcert_deregdrep: return "GOVCERT-deregdrep";
            case rule_id::govcert_ccreghot: return "GOVCERT-ccreghot";
            // Conway has separate wire signals for the two optional cases
            // represented by these Agda constructors.
            case rule_id::govcert_resign_cold: return "GOVCERT-ccreghot";
            case rule_id::govcert_update_drep: return "GOVCERT-regdrep";
            case rule_id::cert_deleg: return "CERT-deleg";
            case rule_id::cert_pool: return "CERT-pool";
            case rule_id::cert_vdel: return "CERT-vdel";
            case rule_id::gov_vote: return "GOV-Vote";
            case rule_id::gov_propose: return "GOV-Propose";
            case rule_id::ratify_accept: return "RATIFY-Accept";
            case rule_id::ratify_reject: return "RATIFY-Reject";
            case rule_id::ratify_continue: return "RATIFY-Continue";
            case rule_id::enact_no_conf: return "Enact-NoConf";
            case rule_id::enact_upd_comm: return "Enact-UpdComm";
            case rule_id::enact_new_const: return "Enact-NewConst";
            case rule_id::enact_hf: return "Enact-HF";
            case rule_id::enact_pparams: return "Enact-PParams";
            case rule_id::enact_wdrl: return "Enact-Wdrl";
            case rule_id::enact_info: return "Enact-Info";
            case rule_id::epoch: return "EPOCH";
            case rule_id::newepoch_new: return "NEWEPOCH-New";
            case rule_id::newepoch_not_new: return "NEWEPOCH-Not-New";
            case rule_id::newepoch_no_reward_update: return "NEWEPOCH-No-Reward-Update";
            case rule_id::unsupported_certificate: return "unsupported Conway certificate";
        }
        return "unknown Conway rule";
    }
}
