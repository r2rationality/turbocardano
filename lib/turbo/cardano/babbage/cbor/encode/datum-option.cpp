/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/cardano/babbage/cbor/encode/transaction-output.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::cardano {
    namespace {
        template<typename INLINE_ENCODER>
        void datum_option_to_cbor(
            era_encoder &enc, const datum_option_t &datum,
            INLINE_ENCODER &&encode_inline)
        {
            enc.array(2);
            if (const auto *hash = std::get_if<datum_hash>(&datum.val)) {
                enc.uint(0).bytes(*hash);
            } else {
                enc.uint(1).tag(24);
                encode_inline(std::get<uint8_vector>(datum.val));
            }
        }
    }

    void datum_option_t::to_cbor(era_encoder &enc) const
    {
        datum_option_to_cbor(enc, *this, [&](const buffer raw) {
            enc.bytes(raw);
        });
    }
}

namespace turbo::cardano::babbage::detail {
    void datum_option_to_cbor_semantic(
        era_encoder &enc, const datum_option_t &datum, plutus::allocator &alloc)
    {
        ::turbo::cardano::datum_option_to_cbor(enc, datum, [&](const buffer raw) {
            era_encoder nested { enc };
            plutus::data::from_cbor(alloc, raw).to_cbor(nested);
            enc.bytes(nested.cbor());
        });
    }

    void datum_option_to_cbor_semantic(era_encoder &enc, const datum_option_t &datum)
    {
        plutus::allocator alloc {};
        datum_option_to_cbor_semantic(enc, datum, alloc);
    }
}
