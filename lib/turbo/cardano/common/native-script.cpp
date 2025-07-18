/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/native-script.hpp>
#include <turbo/common/format.hpp>

namespace turbo::cardano::native_script {
    optional_error_string validate(cbor::zero2::value &script, const uint64_t slot, const std::set<key_hash> &vkeys)
    {
        auto &it = script.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0:
                if (const auto &req_vkey = it.read().bytes(); !vkeys.contains(req_vkey)) [[unlikely]]
                    return fmt::format("required key {} didn't sign the transaction", req_vkey);
                break;
            case 1: {
                auto &s_v = it.read();
                auto &s_it = s_v.array();
                while (!s_it.done()) {
                    auto &sub_script = s_it.read();
                    if (const auto err = validate(sub_script, slot, vkeys); err)
                        return err;
                }
                break;
            }
            case 2: {
                bool any_ok = false;
                auto &s_v = it.read();
                auto &s_it = s_v.array();
                while (!s_it.done()) {
                    auto &sub_script = s_it.read();
                    if (!validate(sub_script, slot, vkeys))
                        any_ok = true;
                }
                if (!any_ok) [[unlikely]]
                    return fmt::format("no child script has been successful!");
                break;
            }
            case 3: {
                const auto min_ok = it.read().uint();
                uint64_t num_ok = 0;
                auto &s_v = it.read();
                auto &s_it = s_v.array();
                while (!s_it.done()) {
                    auto &sub_script = s_it.read();
                    if (!validate(sub_script, slot, vkeys))
                        ++num_ok;
                }
                if (num_ok < min_ok) [[unlikely]]
                    return fmt::format("only {} child scripts succeed while {} are required!", num_ok, min_ok);
                break;
            }
            case 4:
                if (const auto invalid_before = it.read().uint(); slot < invalid_before)
                    return fmt::format("invalid before {} while the current slot is {}!", invalid_before, slot);
                break;
            case 5:
                if (const auto invalid_after = it.read().uint(); slot >= invalid_after)
                    return fmt::format("invalid after {} while the current slot is {}!", invalid_after, slot);
                break;
            default:
                return fmt::format("unsupported native script type {}", typ);
        }
        return {};
    }
}
