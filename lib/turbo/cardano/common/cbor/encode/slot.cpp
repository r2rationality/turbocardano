/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/config.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/math/big-int.hpp>

namespace turbo::cardano {
    void slot::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        big_int_to_cbor(enc, cpp_int { unixtime() - _cfg.byron_start_time } * 1'000'000'000'000);
        enc.uint(_slot);
        enc.uint(epoch());
    }
}
