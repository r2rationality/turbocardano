/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxow.
// Included at class scope by lib/turbo/cardano/ledger/rules/utxow/requirements.hpp.

        static required_signer_t from_address(const redeemer_tag typ, const address &addr)
        {
            switch (const auto pay_id = addr.pay_id(); pay_id.type) {
                // Shelley onward requires the address root as a key hash;
                // the address encoding does not select the witness encoding.
                case pay_ident::ident_type::BYRON_KEY:
                case pay_ident::ident_type::SHELLEY_KEY:
                    return { vkey_signer_t { pay_id.hash } };
                case pay_ident::ident_type::SHELLEY_SCRIPT:
                    return { script_signer_t { pay_id.hash, typ } };
                [[unlikely]] default:
                    throw error(fmt::format("unsupported pay_ident type: {}", static_cast<int>(pay_id.type)));
            }
        }

        static required_signer_t from_cred(const credential_t &cred)
        {
            if (cred.script)
                return { script_signer_t { cred.hash, redeemer_tag::cert } };
            return { vkey_signer_t { cred.hash } };
        }
