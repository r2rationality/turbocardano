#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano.hpp>
#include <turbo/common/error.hpp>

namespace turbo::cardano::network::miniprotocol::handshake
{
    typedef turbo::error error;

    struct node_to_node_version_data_t {
        uint32_t network_magic = 764824073;
        bool initiator_only_diffusion_mode = false;
        bool peer_sharing = false;
        bool query = false;

        static node_to_node_version_data_t from_cbor(cbor::zero2::value &v);
        void to_cbor(cbor::encoder &enc) const;
    };

    using version_map = map_t<uint64_t, node_to_node_version_data_t, cbor::encoder>;

    struct result_t {
        uint64_t version;
        node_to_node_version_data_t config;
    };
}
