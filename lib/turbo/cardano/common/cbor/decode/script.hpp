#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano::detail {
    template<typename NATIVE_VALIDATOR>
    script_info script_info_from_cbor(
        const script_type type, cbor::zero2::value &script,
        NATIVE_VALIDATOR &&validate_native)
    {
        if (type == script_type::native) {
            validate_native(script);
            return { type, script.data_raw() };
        }
        if (!script.indefinite()) [[likely]]
            return { type, script.bytes() };
        uint8_vector bytes {};
        script.to_bytes(bytes);
        return { type, std::move(bytes) };
    }
}
