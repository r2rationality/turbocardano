#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/native-script.hpp>
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano::ledger::conway::rules::utxow {
    // UTXOW-inductive: every needed native script must validate. The caller has
    // already found this script hash in the transaction's reference-script map.
    // An empty entry means this same script was successfully checked for another
    // purpose; it does not mean that an arbitrary script is available.
    template<typename Time, typename CollectSigners>
    void validate_reference_native(
        std::optional<script_info> &script,
        std::optional<signer_set> &vkeys,
        const Time &slot,
        const tx_hash &tx_id,
        CollectSigners &&collect_signers)
    {
        if (!script)
            return;
        if (!vkeys) {
            vkeys.emplace();
            collect_signers(*vkeys);
        }
        if (const auto err = native_script::validate(cbor::zero2::parse(script->script()).get(), slot, *vkeys); err) [[unlikely]]
            throw error(fmt::format("native script: {} failed to validate tx {}: {}", script->hash(), tx_id, err));
        // Cache success only. A failing script must remain pending.
        script.reset();
    }
}
