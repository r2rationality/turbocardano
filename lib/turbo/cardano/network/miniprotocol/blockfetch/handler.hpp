#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/network/multiplexer.hpp>
#include <turbo/chunk-registry-fwd.hpp>
#include "types.hpp"

namespace turbo::cardano::network::miniprotocol::blockfetch
{
    struct config_t {
        bool block_compression = false;
    };

    struct handler: protocol_observer_t {
        handler(std::shared_ptr<chunk_registry>, config_t={});
        ~handler() override;
        void data(buffer, const protocol_send_func &) override;
        void failed(std::string_view) override;
        void stopped() override;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}
