#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway.hpp>

namespace turbo::cardano::ledger::dijkstra {
    struct state: conway::state {
        explicit state(conway::state &&);

        point from_cbor(cbor::zero2::value &) override;
        void to_cbor(cbor_encoder &, node_format = node_format::legacy) const override;
    private:
        bool _snapshots_after_epoch() const override { return true; }
        void _apply_genesis(protocol_params &) const;
    };
}
