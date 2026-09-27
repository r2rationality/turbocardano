/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Certs.
// Included at class scope by lib/turbo/cardano/ledger/conway.hpp.

        static uint64_t compute_expire_epoch(
            const protocol_params &pp,
            const uint64_t current_epoch,
            const uint64_t dormant_epochs=0)
        {
            return current_epoch + pp.drep_activity - dormant_epochs;
        }

        static uint64_t compute_reg_expire_epoch(
            const protocol_params &pp,
            const uint64_t current_epoch,
            const uint64_t dormant_epochs=0)
        {
            if (pp.protocol_ver.bootstrap_phase())
                return current_epoch + pp.drep_activity;
            return compute_expire_epoch(pp, current_epoch, dormant_epochs);
        }
