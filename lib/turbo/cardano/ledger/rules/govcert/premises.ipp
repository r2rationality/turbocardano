/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Certs.
// Included at namespace scope by rules/certs/conway.cpp.

namespace turbo::cardano::ledger::conway::rules::govcert {
    expiry_result register_drep(
        const protocol_params &params,
        const uint64_t current_epoch,
        const uint64_t dormant_epochs,
        const bool already_registered,
        const uint64_t deposit)
    {
        if (already_registered)
            return expiry_result::fail(rule_id::govcert_regdrep, failure::drep_already_registered);
        if (deposit != params.drep_deposit)
            return expiry_result::fail(rule_id::govcert_regdrep, failure::deposit_mismatch);
        return expiry_result::success(
            rule_id::govcert_regdrep,
            { drep_info_t::compute_reg_expire_epoch(params, current_epoch, dormant_epochs) });
    }

    result deregister_drep(
        const bool registered,
        const uint64_t registered_deposit,
        const uint64_t requested_deposit,
        const uint64_t deposited_pot)
    {
        if (!registered)
            return result::fail(rule_id::govcert_deregdrep, failure::drep_unknown);
        if (registered_deposit != requested_deposit)
            return result::fail(rule_id::govcert_deregdrep, failure::deposit_mismatch);
        if (deposited_pot < requested_deposit)
            return result::fail(rule_id::govcert_deregdrep, failure::deposited_pot_insufficient);
        return result::success(rule_id::govcert_deregdrep);
    }

    expiry_result update_drep(
        const protocol_params &params,
        const uint64_t current_epoch,
        const uint64_t dormant_epochs,
        const bool registered)
    {
        if (!registered)
            return expiry_result::fail(rule_id::govcert_update_drep, failure::drep_unknown);
        return expiry_result::success(
            rule_id::govcert_update_drep,
            { drep_info_t::compute_expire_epoch(params, current_epoch, dormant_epochs) });
    }

    result authorize_hot(const bool known_cold_id, const committee_t::hot_key_t *current_key)
    {
        if (!known_cold_id)
            return result::fail(rule_id::govcert_ccreghot, failure::committee_member_unknown);
        if (current_key && std::holds_alternative<committee_t::resigned_t>(current_key->val))
            return result::fail(rule_id::govcert_ccreghot, failure::committee_member_resigned);
        return result::success(rule_id::govcert_ccreghot);
    }

    result resign_cold(const bool known_cold_id, const committee_t::hot_key_t *current_key)
    {
        const auto checked = authorize_hot(known_cold_id, current_key);
        if (!checked)
            return result::fail(rule_id::govcert_resign_cold, checked.failure);
        return result::success(rule_id::govcert_resign_cold);
    }
}

