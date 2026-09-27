#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/network/multiplexer.hpp>
#include <turbo/cardano/network/chain-source.hpp>

namespace turbo::cardano::network {
    struct server {
        static constexpr size_t default_max_connections = 1000;
        static server make_default(const address &addr, const std::string &data_dir, const asio::worker_ptr &iow,
            const cardano::config &cfg, size_t max_connections=default_max_connections,
            size_t cache_bytes=chunk_cache::default_max_bytes);
        static server make_default(const address &, std::shared_ptr<chain_source>, const asio::worker_ptr &,
            const cardano::config &, size_t max_connections=default_max_connections);
        server(const address &addr, const multiplexer_config_t &&m, const asio::worker_ptr &iow,
            const cardano::config &cfg, size_t max_connections=default_max_connections);
        ~server();
        void run();
        void stop();
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}
