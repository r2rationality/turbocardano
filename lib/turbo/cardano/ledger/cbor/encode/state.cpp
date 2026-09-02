/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/state.hpp>
#include <turbo/common/timer.hpp>

namespace turbo::cardano::ledger {
    void state::_serialize_node_state(cbor_encoder &ser, const point &tip) const
    {
        ser.add([&](auto enc) {
            enc.array(_eras.size());
            for (size_t era = 0; era < _eras.size(); ++era) {
                enc.array(2);
                slot { _eras[era], _cfg }.to_cbor(enc);
                if (era + 1 < _eras.size()) {
                    slot { _eras[era + 1], _cfg }.to_cbor(enc);
                } else {
                    enc.array(2)
                        .uint(2)
                        .array(3)
                            .array(1).array(3).uint(tip.slot).uint(tip.height).bytes(tip.hash);
                }
            }
            return std::move(enc.cbor());
        });
        _state->to_cbor(ser);
    }

    void state::_serialize_node_vrf_state(cbor_encoder &ser, const point &tip) const
    {
        ser.add([this, tip](auto enc) {
            enc.array(2);
            if (!_eras.empty()) {
                enc.array(1)
                    .array(2)
                        .uint(_eras.size() - 1)
                        .array(3)
                            .uint(tip.slot)
                            .bytes(tip.hash)
                            .uint(tip.height);
            } else {
                enc.array(0);
            }
            enc.array(_eras.size());
            for (size_t era = 0; era < _eras.size(); ++era) {
                enc.array(2);
                slot { _eras[era], _cfg }.to_cbor(enc);
                if (era + 1 < _eras.size()) {
                    slot { _eras[era + 1], _cfg }.to_cbor(enc);
                }
            }
            return std::move(enc.cbor());
        });
        _vrf_state->to_cbor(ser);
    }

    cbor_encoder state::to_cbor(const point &tip, const int prio) const
    {
        timer t { "serialize the state into the Cardano Node format", logger::level::info };
        cbor_encoder ser { [&] { return era_encoder { era_from_number(_eras.size()) }; } };
        ser.add([](auto enc) {
            enc.array(2); // versioned encoding tuple
            enc.uint(1); // version number
            enc.array(2);
            return std::move(enc.cbor());
        });
        _serialize_node_state(ser, tip);
        _serialize_node_vrf_state(ser, tip);
        ser.run(_sched, "ledger-export", prio, true);
        return ser;
    }
}

