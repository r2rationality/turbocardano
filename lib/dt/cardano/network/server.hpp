#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/network/multiplexer.hpp>

namespace daedalus_turbo::cardano::network {
    struct server {
        static server make_default(const address &addr, const std::string &data_dir, const asio::worker_ptr &iow, const cardano::config &cfg);
        server(const address &addr, const multiplexer_config_t &&m, const asio::worker_ptr &iow, const cardano::config &cfg);
        ~server();
        void run();
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}