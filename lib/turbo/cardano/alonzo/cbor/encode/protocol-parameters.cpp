/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano {
    void ex_unit_prices::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        mem.to_cbor(enc);
        steps.to_cbor(enc);
    }

    void ex_units::to_cbor(era_encoder &enc) const
    {
        if (mem > std::numeric_limits<int64_t>::max()
                || steps > std::numeric_limits<int64_t>::max()) [[unlikely]]
            throw error("execution units exceed max_int64");
        enc.array(2);
        enc.uint(mem);
        enc.uint(steps);
    }
}
