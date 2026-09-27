/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Ledger.
// Included at class scope by lib/turbo/txwit/validator.cpp.

            wit_cnt _validate_witnesses_and_invariants(batch_info &part)
            {
                _validate_max_stats(part);
                _validate_reference_scripts(part);
                return _validate_witnesses(part);
            }
