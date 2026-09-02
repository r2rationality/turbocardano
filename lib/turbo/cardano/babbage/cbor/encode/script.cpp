/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/cardano/babbage/cbor/encode/transaction-output.hpp>

namespace turbo::cardano {
    namespace {
        template<typename NATIVE_ENCODER>
        void script_info_to_cbor(
            era_encoder &enc, const script_info &script,
            NATIVE_ENCODER &&encode_native)
        {
            enc.tag(24);
            era_encoder nested { enc };
            nested.array(2).uint(static_cast<uint8_t>(script.type()));
            if (script.type() == script_type::native)
                encode_native(nested, script.script());
            else
                nested.bytes(script.script());
            enc.bytes(nested.cbor());
        }
    }

    void script_info::to_cbor(era_encoder &enc) const
    {
        script_info_to_cbor(enc, *this, [](era_encoder &nested, const buffer raw) {
            nested.raw_cbor(raw);
        });
    }
}

namespace turbo::cardano::babbage::detail {
    void script_info_to_cbor_semantic(
        era_encoder &enc, const script_info &script, const native_script_encoder_t encode_native)
    {
        ::turbo::cardano::script_info_to_cbor(enc, script, encode_native);
    }
}
