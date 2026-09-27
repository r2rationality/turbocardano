/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/asio.hpp>
#include <turbo/cardano/network/server.hpp>
#include <turbo/sync/p2p.hpp>
#include <turbo/common/scope-exit.hpp>
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include <cstdlib>
#include <charconv>
#include <limits>
#include <thread>
#include "common.hpp"

namespace turbo::cli::node_api {
    using namespace turbo::cardano::network;

    // Signals must remain responsive while the main thread waits for a checkpoint.
    struct shutdown_signals {
        shutdown_signals()
        {
            _wait();
            _thread = std::jthread { [this] { _ioc.run(); } };
        }

        ~shutdown_signals()
        {
            _ioc.stop();
            _thread.join();
        }

        std::stop_token token() const { return _stop.get_token(); }
        void request_stop() { _stop.request_stop(); }
    private:
        boost::asio::io_context _ioc;
        boost::asio::signal_set _signals { _ioc, SIGINT, SIGTERM };
        std::stop_source _stop;
        std::jthread _thread;
        bool _received = false;

        void _wait()
        {
            _signals.async_wait([this](const auto &ec, const int signal) {
                if (ec) return;
                if (_received) {
                    logger::warn("second shutdown signal received; forcing exit without waiting for the checkpoint");
                    std::_Exit(128 + signal);
                }
                _received = true;
                _wait();
                logger::info("{} received; shutting down and saving state. Press CTRL-C again to force exit",
                    signal == SIGINT ? "SIGINT (CTRL-C)" : "SIGTERM");
                request_stop();
            });
        }
    };

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "node-api";
            cmd.desc = "start a server providing Cardano Node networking protocol";
            cmd.args.expect({ "<data-dir>" });
            cmd.opts.try_emplace("ip", "an IP address at which to listen for incoming connections", "127.0.0.1");
            cmd.opts.try_emplace("port", "a TCP port at which to listen for incoming connections", "3001");
            cmd.opts.try_emplace("max-connections", "maximum open incoming connections", std::to_string(server::default_max_connections));
            cmd.opts.try_emplace("chunk-cache-mib", "maximum decompressed chunk cache size in MiB",
                std::to_string(chunk_cache::default_max_bytes >> 20));
            cmd.opts.emplace("no-sync", "serve the stored chain without connecting to an upstream peer");
            cmd.opts.emplace("peer-host", "follow this peer instead of a random topology peer");
            cmd.opts.try_emplace("peer-port", "upstream peer TCP port", "3001");
            cmd.opts.try_emplace("snapshot-interval", "minimum seconds between live checkpoints", "600");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &data_dir = args.at(0);
            const auto ip = opts.at("ip").value();
            const auto port = opts.at("port").value();
            const auto positive_size = [&](const std::string &name) {
                const auto &value = opts.at(name).value();
                size_t size = 0;
                const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), size);
                if (ec != std::errc {} || end != value.data() + value.size() || !size)
                    throw error(fmt::format("{} must be a positive integer", name));
                return size;
            };
            const auto max_connections = positive_size("max-connections");
            const auto cache_mib = positive_size("chunk-cache-mib");
            if (cache_mib > (std::numeric_limits<size_t>::max() >> 20))
                throw error("chunk-cache-mib is too large");
            const auto cache_bytes = cache_mib << 20;
            logger::info("NODE API listens at the address {}:{}", ip, port);
            shutdown_signals shutdown;
            const auto iow = std::make_shared<asio::worker_manual>();
            if (opts.contains("no-sync")) {
                auto srv = server::make_default(address { ip, port }, data_dir, iow, cardano::config::get(),
                    max_connections, cache_bytes);
                std::stop_callback stop_server { shutdown.token(), [&] { srv.stop(); } };
                srv.run();
                return;
            }
            const auto interval = std::chrono::seconds { std::stoll(opts.at("snapshot-interval").value()) };
            if (interval.count() <= 0)
                throw error("snapshot-interval must be positive");
            std::optional<address> peer;
            if (const auto it = opts.find("peer-host"); it != opts.end() && it->second)
                peer.emplace(*it->second, opts.at("peer-port").value());
            auto cr = std::make_shared<chunk_registry>(data_dir, chunk_registry::mode::validate,
                cardano::config::get(), scheduler::get(), file_remover::get(), true, true, true);
            if (shutdown.token().stop_requested()) return;
            auto source = std::make_shared<chain_source>(cr, cache_bytes);
            sync::p2p::syncer syncer { *cr };
            auto srv = server::make_default(address { ip, port }, source, iow, cr->config(), max_connections);
            std::stop_callback stop_server { shutdown.token(), [&] { srv.stop(); } };
            std::exception_ptr failure;
            std::jthread follower { [&] {
                try {
                    syncer.follow(shutdown.token(), [&](const auto &intersection) { source->publish(intersection); }, peer, {}, interval);
                } catch (...) {
                    failure = std::current_exception();
                    srv.stop();
                }
            } };
            {
                scope_exit stop_follower { [&] { shutdown.request_stop(); } };
                srv.run();
            }
            follower.join();
            if (failure) std::rethrow_exception(failure);
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
