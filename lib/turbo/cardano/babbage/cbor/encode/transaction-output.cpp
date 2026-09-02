/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/babbage/cbor/encode/transaction-output.hpp>

namespace turbo::cardano {
    namespace {
        void assets_to_cbor(era_encoder &enc, const tx_out_data &data)
        {
            if (!data.assets.empty()) {
                enc.array(2);
                enc.uint(data.coin);
                data.assets.to_cbor(enc);
            } else {
                enc.uint(data.coin);
            }
        }

        template<typename DATUM_ENCODER, typename SCRIPT_ENCODER>
        void transaction_output_to_cbor(
            era_encoder &enc, const tx_out_data &data,
            DATUM_ENCODER &&encode_datum, SCRIPT_ENCODER &&encode_script,
            const bool force_map)
        {
            if (force_map || data.script_ref || (data.datum && data.datum->val.index() != 0)) {
                enc.map(2 + (data.datum ? 1 : 0) + (data.script_ref ? 1 : 0));
                enc.uint(0);
                data.addr().to_cbor(enc);
                enc.uint(1);
                assets_to_cbor(enc, data);
                if (data.datum) {
                    enc.uint(2);
                    encode_datum(*data.datum);
                }
                if (data.script_ref) {
                    enc.uint(3);
                    encode_script(*data.script_ref);
                }
            } else {
                enc.array(2 + (data.datum ? 1 : 0));
                data.addr().to_cbor(enc);
                assets_to_cbor(enc, data);
                if (data.datum)
                    enc.bytes(std::get<datum_hash>(data.datum->val));
            }
        }
    }

    void tx_out_data::to_cbor(era_encoder &enc) const
    {
        transaction_output_to_cbor(
            enc,
            *this,
            [&](const datum_option_t &datum) { datum.to_cbor(enc); },
            [&](const script_info &script) { script.to_cbor(enc); },
            false);
    }
}

namespace turbo::cardano::babbage::detail {
    void transaction_output_to_cbor_semantic(
        era_encoder &enc, const tx_out_data &data, plutus::allocator &alloc,
        const native_script_encoder_t encode_native)
    {
        transaction_output_to_cbor(
            enc,
            data,
            [&](const datum_option_t &datum) {
                datum_option_to_cbor_semantic(enc, datum, alloc);
            },
            [&](const script_info &script) {
                script_info_to_cbor_semantic(enc, script, encode_native);
            },
            true);
    }
}
