/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>

namespace turbo::cardano {
    invalid_tx_set invalid_tx_set::from_cbor(cbor::zero2::value &v)
    {
        invalid_tx_set res {};
        static_cast<base_type &>(res) = base_type::from_cbor(v);
        res.raw = v.data_raw();
        return res;
    }
}
