#pragma once
#ifndef DAEDALUS_TURBO_PEER_SELECTION_HPP
#define DAEDALUS_TURBO_PEER_SELECTION_HPP
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <chrono>
#include <random>
#include <boost/container/flat_set.hpp>
#include <dt/config.hpp>
#include <dt/cardano/network/common.hpp>
#include <dt/json.hpp>

namespace daedalus_turbo {
    // flat_set has random access iterator making random selection easy
    using turbo_peer_list = boost::container::flat_set<std::string>;
    using cardano_peer_list = boost::container::flat_set<cardano::network::address>;

    struct peer_selection {
        static constexpr size_t max_retries = 10;

        virtual ~peer_selection() =default;

        cardano::network::address next_cardano()
        {
            return _next_cardano_impl();
        }
    private:
        virtual cardano::network::address _next_cardano_impl() =0;
    };

    struct peer_selection_simple: peer_selection {
        static peer_selection_simple &get()
        {
            static peer_selection_simple ps {};
            return ps;
        }

        explicit peer_selection_simple() =default;
    private:
        turbo_peer_list _turbo_hosts {};
        cardano_peer_list _cardano_hosts {};
        std::default_random_engine _rnd { static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count()) };

        cardano::network::address _next_cardano_impl() override
        {
            if (_cardano_hosts.empty()) {
                const auto j_cardano_hosts = configs_dir::get().at("topology").at("bootstrapPeers").as_array();
                for (const auto &j_host: j_cardano_hosts) {
                    _cardano_hosts.emplace(
                        json::value_to<std::string>(j_host.at("address")),
                        std::to_string(json::value_to<uint64_t>(j_host.at("port")))
                    );
                }
                if (_cardano_hosts.empty())
                    throw error("The list of cardano hosts cannot be empty!");
            }
            std::uniform_int_distribution<size_t> dist { 0, _cardano_hosts.size() - 1 };
            const auto ri = dist(_rnd);
            return *(_cardano_hosts.begin() + ri);
        }
    };
}

#endif // !DAEDALUS_TURBO_PEER_SELECTION_HPP