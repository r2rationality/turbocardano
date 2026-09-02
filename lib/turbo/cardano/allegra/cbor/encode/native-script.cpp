/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>

namespace turbo::cardano::allegra {
    void native_script_t::to_cbor(era_encoder &enc) const
    {
        const auto encode_scripts = [&] {
            enc.array_compact(scripts.size(), [&] {
                for (const auto &script: scripts)
                    script.to_cbor(enc);
            });
        };
        switch (type) {
            case type_t::signature:
                enc.array(2).uint(0).bytes(key);
                break;
            case type_t::all:
                enc.array(2).uint(1);
                encode_scripts();
                break;
            case type_t::any:
                enc.array(2).uint(2);
                encode_scripts();
                break;
            case type_t::at_least:
                enc.array(3).uint(3);
                if (required >= 0)
                    enc.uint(numeric_cast<uint64_t>(required));
                else
                    enc.nint(numeric_cast<uint64_t>(-(required + 1)));
                encode_scripts();
                break;
            case type_t::invalid_before:
                enc.array(2).uint(4).uint(slot);
                break;
            case type_t::invalid_hereafter:
                enc.array(2).uint(5).uint(slot);
                break;
        }
    }
}
