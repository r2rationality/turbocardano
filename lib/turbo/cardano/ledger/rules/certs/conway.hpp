#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/rules/rule-result.hpp>

#include <turbo/cardano/ledger/rules/govcert/conway.hpp>

namespace turbo::cardano::ledger::conway::rules::certs {
    // Read-only classification for coverage/conformance tooling. The first value
    // identifies CERT-deleg/pool/vdel; the second identifies the nested rule.
    rule_id transition_rule(const cert_t &);
    rule_id constructor_rule(const cert_t &);
}
