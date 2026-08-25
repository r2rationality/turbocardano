#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/conway/rule-result.hpp>

// Mirrors Ledger/Conway/Specification/Certs.lagda.md.
namespace turbo::cardano::ledger::conway::rules::govcert {
    enum class failure {
        none,
        drep_already_registered,
        drep_unknown,
        deposit_mismatch,
        deposited_pot_insufficient,
        committee_member_unknown,
        committee_member_resigned
    };

    struct expiry_effect {
        uint64_t expire_epoch = 0;
    };

    using expiry_result = rule_result<expiry_effect, failure>;
    using result = validation_result<failure>;

    expiry_result register_drep(const protocol_params &, uint64_t current_epoch, uint64_t dormant_epochs, bool already_registered, uint64_t deposit);
    result deregister_drep(bool registered, uint64_t registered_deposit, uint64_t requested_deposit, uint64_t deposited_pot);
    expiry_result update_drep(const protocol_params &, uint64_t current_epoch, uint64_t dormant_epochs, bool registered);
    result authorize_hot(bool known_cold_id, const committee_t::hot_key_t *current_key);
    result resign_cold(bool committee_exists, bool cold_id_known);
}

namespace turbo::cardano::ledger::conway::rules::certs {
    // Read-only classification for coverage/conformance tooling. The first value
    // identifies CERT-deleg/pool/vdel; the second identifies the nested rule.
    rule_id transition_rule(const cert_t &);
    rule_id constructor_rule(const cert_t &);
}
