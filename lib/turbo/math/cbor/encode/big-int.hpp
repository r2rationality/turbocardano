#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/math/big-int.hpp>

namespace turbo {
    inline void _raw_big_uint_to_cbor(cbor::encoder &enc, const cpp_int &val)
    {
        thread_local uint8_vector buf(0x1000);
        buf.clear();
        auto val_copy = val;
        while (val_copy) {
            buf.emplace_back(val_copy & 0xFF);
            val_copy >>= 8;
        }
        enc.bytes_reverse(buf);
    }

    inline void big_int_to_cbor(cbor::encoder &enc, const cpp_int &val)
    {
        if (val >= 0) [[likely]] {
            if (val <= std::numeric_limits<uint64_t>::max()) {
                enc.uint(static_cast<uint64_t>(val));
                return;
            }
            enc.tag(2);
            _raw_big_uint_to_cbor(enc, val);
        } else {
            const auto val_uint = -(val + 1);
            if (val_uint <= std::numeric_limits<uint64_t>::max()) {
                enc.nint(static_cast<uint64_t>(val_uint));
                return;
            }
            enc.tag(3);
            _raw_big_uint_to_cbor(enc, val_uint);
        }
    }
}
