/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/conway/auxiliary-data.hpp>

namespace turbo::cardano::conway {
    namespace {
        void scripts_to_cbor(
            era_encoder &enc, const std::vector<script_info> &scripts, const bool native)
        {
            enc.array_compact(scripts.size(), [&] {
                for (const auto &script: scripts) {
                    if (native) {
                        const auto native_script = allegra::native_script_t::from_cbor(script.script());
                        native_script.to_cbor(enc);
                    } else {
                        enc.bytes(script.script());
                    }
                }
            });
        }
    }

    void auxiliary_data_map_t::to_cbor(era_encoder &enc) const
    {
        const size_t size = metadata.has_value()
            + !native_scripts.empty()
            + !plutus_v1_scripts.empty()
            + !plutus_v2_scripts.empty()
            + !plutus_v3_scripts.empty();
        enc.tag(259);
        enc.map_compact(size, [&] {
            if (metadata) {
                enc.uint(0);
                metadata->to_cbor(enc);
            }
            const auto encode = [&](const uint64_t key, const auto &scripts, const bool native=false) {
                if (!scripts.empty()) {
                    enc.uint(key);
                    scripts_to_cbor(enc, scripts, native);
                }
            };
            encode(1, native_scripts, true);
            encode(2, plutus_v1_scripts);
            encode(3, plutus_v2_scripts);
            encode(4, plutus_v3_scripts);
        });
    }

    void auxiliary_data_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, metadata_t>) {
                value.to_cbor(enc);
            } else if constexpr (std::is_same_v<T, auxiliary_data_array_t>) {
                enc.array(2);
                value.transaction_metadata.to_cbor(enc);
                scripts_to_cbor(enc, value.auxiliary_scripts, true);
            } else {
                value.to_cbor(enc);
            }
        }, value);
    }

    void auxiliary_data_dict_t::to_cbor(era_encoder &enc) const
    {
        enc.map_compact(size(), [&] {
            for (const auto &[index, auxiliary_data]: *this) {
                enc.uint(index);
                auxiliary_data.to_cbor(enc);
            }
        });
    }
}
