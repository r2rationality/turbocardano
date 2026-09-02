/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/common/cbor/decode/script.hpp>
#include <turbo/cardano/shelley/cbor/decode/transaction-witness-set.hpp>

namespace turbo::cardano::allegra {
    namespace {
        void decode_scripts(shelley::transaction_witness_set_t &res, cbor::zero2::value &v)
        {
            if (!v.indefinite()) [[likely]]
                res.items.reserve(res.items.size() + v.special_uint());
            auto &it = v.array();
            while (!it.done()) {
                auto &script = it.read();
                res.items.emplace_back(::turbo::cardano::detail::script_info_from_cbor(
                    script_type::native, script, native_script_t::validate_cbor));
            }
        }
    }

    transaction_witness_set_t transaction_witness_set_t::from_cbor(cbor::zero2::value &v)
    {
        auto decoded = shelley::detail::transaction_witness_set_from_cbor(v, decode_scripts);
        transaction_witness_set_t res {};
        res.items = std::move(decoded.items);
        res.raw = decoded.raw;
        return res;
    }
}
