/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/native-script.hpp>
#include <turbo/common/format.hpp>

namespace turbo::cardano::native_script {
    namespace {
        struct validation_context {
            uint64_t slot;
            const signer_set &vkeys;
        };

        struct evaluation_result {
            optional_error_string parse_error {};
            optional_error_string validation_error {};
        };

        evaluation_result evaluate(cbor::zero2::value &script, const validation_context *ctx)
        {
            auto &it = script.array();
            const auto typ = it.read().uint();
            evaluation_result res {};
            switch (typ) {
                case 0: {
                    const key_hash req_vkey { it.read().bytes() };
                    if (ctx && !ctx->vkeys.contains(req_vkey)) [[unlikely]]
                        res.validation_error = fmt::format("required key {} didn't sign the transaction", req_vkey);
                    break;
                }
                case 1: {
                    auto &s_it = it.read().array();
                    while (!s_it.done()) {
                        auto sub_res = evaluate(s_it.read(), ctx);
                        if (sub_res.parse_error) [[unlikely]]
                            return sub_res;
                        if (!res.validation_error && sub_res.validation_error)
                            res.validation_error = std::move(sub_res.validation_error);
                    }
                    break;
                }
                case 2: {
                    bool any_ok = false;
                    auto &s_it = it.read().array();
                    while (!s_it.done()) {
                        auto sub_res = evaluate(s_it.read(), ctx);
                        if (sub_res.parse_error) [[unlikely]]
                            return sub_res;
                        if (!sub_res.validation_error)
                            any_ok = true;
                    }
                    if (ctx && !any_ok) [[unlikely]]
                        res.validation_error = "no child script has been successful!";
                    break;
                }
                case 3: {
                    const auto min_ok = it.read().int64();
                    int64_t num_ok = 0;
                    auto &s_it = it.read().array();
                    while (!s_it.done()) {
                        auto sub_res = evaluate(s_it.read(), ctx);
                        if (sub_res.parse_error) [[unlikely]]
                            return sub_res;
                        if (!sub_res.validation_error)
                            ++num_ok;
                    }
                    if (ctx && num_ok < min_ok) [[unlikely]]
                        res.validation_error = fmt::format("only {} child scripts succeed while {} are required!", num_ok, min_ok);
                    break;
                }
                case 4:
                    if (const auto invalid_before = it.read().uint(); ctx && ctx->slot < invalid_before)
                        res.validation_error = fmt::format("invalid before {} while the current slot is {}!", invalid_before, ctx->slot);
                    break;
                case 5:
                    if (const auto invalid_after = it.read().uint(); ctx && ctx->slot >= invalid_after)
                        res.validation_error = fmt::format("invalid after {} while the current slot is {}!", invalid_after, ctx->slot);
                    break;
                default:
                    res.parse_error = fmt::format("unsupported native script type {}", typ);
                    return res;
            }
            if (!it.done()) [[unlikely]]
                res.parse_error = fmt::format("native script type {} has unexpected trailing elements", typ);
            return res;
        }
    }

    optional_error_string validate(cbor::zero2::value &script, const uint64_t slot, const signer_set &vkeys)
    {
        const validation_context ctx { slot, vkeys };
        auto res = evaluate(script, &ctx);
        if (res.parse_error) [[unlikely]]
            return res.parse_error;
        return res.validation_error;
    }
}
