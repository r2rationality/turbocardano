#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/network/multiplexer.hpp>
#include "types.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::handshake
{
    using on_success_func = std::function<void(const result_t &)>;

    struct observer_t: protocol_observer_t {
        virtual void on_success(const on_success_func &on_success) =0;
    };

    struct handler: observer_t {
        handler(version_map &&versions, uint64_t promoted_version);
        ~handler() override;
        void on_success(const on_success_func &on_success) override;
        void data(buffer, const protocol_send_func &) override;
        void failed(std::string_view) override;
        void stopped() override;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}
