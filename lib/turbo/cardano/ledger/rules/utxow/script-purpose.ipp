/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxow.
// Included at namespace scope by lib/turbo/plutus/context.cpp.

    script_hash context::redeemer_script(const redeemer_id &r) const
    {
        if (_script_purposes)
            return _script_purposes->at(r);
        switch (r.tag) {
            case redeemer_tag::spend: {
                const auto &in = input_at(r.ref_idx);
                const auto addr = in.data.addr();
                if (const auto pay_id = addr.pay_id(); pay_id.type == pay_ident::ident_type::SHELLEY_SCRIPT) [[likely]]
                    return pay_id.hash;
                throw error(fmt::format("tx {} (spend) input #{}: the output address is not a payment script: {}!", _tx->hash(), r.ref_idx, in));
            }
            case redeemer_tag::mint: return mint_at(r.ref_idx);
            case redeemer_tag::cert: return cert_cred_at(r.ref_idx).hash;
            case redeemer_tag::reward: return withdraw_at(r.ref_idx).hash();
            case redeemer_tag::vote: return voter_at(r.ref_idx).hash;
            case redeemer_tag::propose: {
                const auto &p = proposal_at(r.ref_idx);
                return proposal_script_hash(p.procedure.action);
            }
            [[unlikely]] default:
                throw error(fmt::format("tx: {} unsupported redeemer_tag: {}", _tx->hash(), static_cast<int>(r.tag)));
        };
    }
