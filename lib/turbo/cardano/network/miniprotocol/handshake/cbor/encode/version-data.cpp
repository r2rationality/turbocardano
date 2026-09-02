/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/network/miniprotocol/handshake/handler.hpp>

namespace turbo::cardano::network::miniprotocol::handshake {
    void node_to_node_version_data_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(4);
        enc.uint(network_magic);
        enc.boolean(initiator_only_diffusion_mode);
        enc.uint(peer_sharing);
        enc.boolean(query);
    }
}

