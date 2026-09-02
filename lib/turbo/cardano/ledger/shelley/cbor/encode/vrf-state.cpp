/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */


#include <turbo/cardano/ledger/shelley.hpp>

namespace turbo::cardano::ledger::shelley {
    void vrf_state::to_cbor(cbor_encoder &ser) const
    {
        ser.add([&](auto enc) {
            enc.array(2)
                .uint(1)
                .array(2)
                    .array(2)
                        .uint(1)
                        .uint(_slot_last)
                    .array(3)
                        .array(3)
                            .custom([this] (auto &enc) {
                                enc.map(_kes_counters.size());
                                for (const auto &[pool_id, cnt]: _kes_counters) {
                                    enc.bytes(pool_id);
                                    enc.uint(cnt);
                                }
                            })
                            .array(2)
                                .uint(1)
                                .bytes(_nonce_evolving)
                            .array(2)
                                .uint(1)
                                .bytes(_nonce_candidate)
                            .array(2)
                                .array(2)
                                    .uint(1)
                                    .bytes(_nonce_epoch)
                            .custom([this](auto &enc) {
                                if (_prev_epoch_lab_prev_hash) {
                                    enc.array(2)
                                        .uint(1)
                                        .bytes(*_prev_epoch_lab_prev_hash);
                                } else {
                                    enc.array(1).uint(0);
                                }
                            })
                        .array(2)
                            .uint(1)
                            .bytes(_lab_prev_hash);
            return std::move(enc.cbor());
        });
    }
}

