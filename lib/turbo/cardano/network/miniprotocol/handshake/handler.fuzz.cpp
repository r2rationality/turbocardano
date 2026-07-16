/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "../handshake.hpp"

namespace {
    using namespace turbo;
    using namespace turbo::cardano::network::miniprotocol;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, const size_t size)
{
    try {
        handshake::handler h {
            handshake::version_map {
                { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
            },
            22
        };
        uint8_vector resp1 {};
        h.data(buffer { data, size }, [&](const auto bytes) { resp1 << bytes; } );
    } catch (const handshake::error &) {
        // ignore the library's exceptions
    }
    return 0;
}