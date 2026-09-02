/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>

namespace turbo::cardano::allegra {
    transaction_body_t transaction_body_t::from_cbor(cbor::zero2::value &v)
    {
        transaction_body_t res {};
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto type = key.uint();
            auto &val = it.read_val(std::move(key));
            switch (type) {
                case 0: res.inputs = shelley::transaction_inputs_t::from_cbor(val); break;
                case 1: res.outputs = shelley::transaction_outputs_t::from_cbor(val); break;
                case 2: res.fee = val.uint(); break;
                case 3: res.validity_end.emplace(val.uint()); break;
                case 4: res.certs = shelley::certificates_t::from_cbor(val); break;
                case 5: res.withdrawals = shelley::withdrawals_t::from_cbor(val); break;
                case 6: res.updates = shelley::update_t::from_cbor(val); break;
                case 7: static_cast<void>(hash_32 { val.bytes() }); break;
                case 8: res.validity_start.emplace(val.uint()); break;
                [[unlikely]] default: throw error(fmt::format("unsupported allegra::transaction_body element type: {}", type));
            }
        }
        res.raw = v.data_raw();
        return res;
    }

}
