#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/network/multiplexer.hpp>
#include <dt/chunk-registry-fwd.hpp>
#include "types.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::blockfetch
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
