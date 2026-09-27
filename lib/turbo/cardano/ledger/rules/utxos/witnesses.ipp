/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Audit: Ledger.Conway.Specification.Utxo.
// Included at namespace scope by lib/turbo/cardano/common/common.cpp.

    wit_cnt tx_base::witnesses_ok_plutus(const plutus::context &ctx) const
    {
        wit_cnt cnt {};
        for (const auto &[rid, rinfo]: ctx.redeemers()) {
            try {
                auto ps = ctx.prepare_script(rinfo);
                ctx.eval_script(ps);
                cnt += ps.typ;
            } catch (const std::exception &ex) {
                throw error(fmt::format("redeemer {}#{}: {}", rinfo.tag, rinfo.ref_idx, ex.what()));
            }
        }
        return cnt;
    }
