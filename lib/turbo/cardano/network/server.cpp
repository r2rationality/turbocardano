/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#ifdef _MSC_VER
#   include <SDKDDKVer.h>
#endif
#define BOOST_ASIO_HAS_STD_INVOKE_RESULT 1
#ifndef BOOST_ALLOW_DEPRECATED_HEADERS
#   define BOOST_ALLOW_DEPRECATED_HEADERS
#   define DT_CLEAR_BOOST_DEPRECATED_HEADERS
#endif
#include <boost/asio/compose.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <set>
#ifdef DT_CLEAR_BOOST_DEPRECATED_HEADERS
#   undef BOOST_ALLOW_DEPRECATED_HEADERS
#   undef DT_CLEAR_BOOST_DEPRECATED_HEADERS
#endif

#include <turbo/chunk-registry.hpp>
#include <turbo/common/scope-exit.hpp>
#include "miniprotocol/blockfetch/handler.hpp"
#include "miniprotocol/chainsync/handler.hpp"
#include "miniprotocol/handshake/handler.hpp"
#include "server.hpp"

namespace turbo::cardano::network {
    struct server::impl {
        impl(const address &addr, const multiplexer_config_t &&mcfg, const asio::worker_ptr &iow, const config &,
                size_t max_connections):
            _addr { std::move(addr) }, _config { std::move(mcfg) }, _iow { iow }, _max_connections { max_connections }
        {
            if (!_max_connections) throw error("max-connections must be positive");
            std::scoped_lock lk { _futures_mutex };
            _futures.emplace_back(boost::asio::co_spawn(_strand, _listen(), boost::asio::use_future));
        }

        ~impl()
        {
            stop();
            while (!_shutdown->closed.load(std::memory_order_acquire)) {
                if (_iow->io_context().stopped()) _iow->io_context().restart();
                _iow->io_context().run_one();
            }
            for (const auto &f: _futures) {
                while (f.wait_for(std::chrono::milliseconds { 0 }) != std::future_status::ready) {
                    if (_iow->io_context().stopped()) _iow->io_context().restart();
                    _iow->io_context().run_one();
                }
            }
        }

        void run()
        {
            // Drain cancellation completions before releasing connection buffers.
            while (!_iow->io_context().stopped()) {
                _iow->io_context().run_for(std::chrono::milliseconds { 100 });
                std::scoped_lock lk { _futures_mutex };
                for (auto it = _futures.begin(); it != _futures.end();) {
                    if (it->wait_for(std::chrono::milliseconds { 0 }) != std::future_status::ready) { ++it; continue; }
                    logger::run_log_errors([&] { it->get(); });
                    it = _futures.erase(it);
                }
                if (_futures.empty()) { stop(); break; }
            }
        }
        void stop()
        {
            if (_stop_requested.exchange(true, std::memory_order_relaxed)) return;
            boost::asio::post(_strand, [state=_shutdown] { state->close(); });
        }
    private:
        using tcp = boost::asio::ip::tcp;

        struct connection_slot {
            explicit connection_slot(std::shared_ptr<std::atomic_size_t> count): _count { std::move(count) }
            {
                _count->fetch_add(1, std::memory_order_relaxed);
            }
            ~connection_slot() { _count->fetch_sub(1, std::memory_order_relaxed); }
        private:
            std::shared_ptr<std::atomic_size_t> _count;
        };

        struct tcp_connection: connection {
            tcp_connection(tcp::socket &&conn, std::unique_ptr<connection_slot> slot):
                _slot { std::move(slot) }, _conn { std::move(conn) }
            {
                _conn.non_blocking(true);
            }

            void owner(const std::shared_ptr<multiplexer> &m) { _owner = m; }

            void close()
            {
                boost::system::error_code ec;
                _conn.close(ec);
            }

            static void process_transfer_result(const std::string_view op_name, const std::error_code ec, const size_t transferred, const size_t expected, const op_observer_ptr observer)
            {
                if (ec) [[unlikely]]
                    observer->failed(fmt::format("asio::{} error: {}", op_name, ec.message()));
                else if (transferred != expected) [[unlikely]]
                    observer->failed(fmt::format("asio::{}: completed only {} bytes while expected {}", op_name, transferred, expected));
                else if (logger::run_log_errors([&] { observer->done(); }))
                    observer->failed("node-api response processing failed");
            }

            size_t available_ingress() const override
            {
                if (const auto size = _conn.available()) return size;
                uint8_t byte;
                boost::system::error_code ec;
                const auto size = _conn.receive(boost::asio::buffer(&byte, 1), tcp::socket::message_peek, ec);
                if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) return 0;
                if (ec) throw error(fmt::format("node-api socket read failed: {}", ec.message()));
                if (!size) throw error("node-api peer disconnected");
                return size;
            }

            void async_read(const write_buffer buf, op_observer_ptr observer) override
            {
                // Cancellation must complete before the multiplexer's buffers are freed.
                boost::asio::async_read(_conn, boost::asio::mutable_buffer { buf.data(), buf.size() }, [owner=_owner.lock(), observer, buf](const auto &ec, const size_t transferred) {
                    process_transfer_result("read", ec, transferred, buf.size(), observer);
                });
            }

            void async_write(const buffer buf, op_observer_ptr observer) override
            {
                boost::asio::async_write(_conn, boost::asio::const_buffer { buf.data(), buf.size() }, [owner=_owner.lock(), buf, observer](const auto &ec, const size_t transferred) {
                    process_transfer_result("write", ec, transferred, buf.size(), observer);
                });
            }
        private:
            std::unique_ptr<connection_slot> _slot;
            mutable tcp::socket _conn;
            std::weak_ptr<multiplexer> _owner;
        };

        struct shutdown_state {
            explicit shutdown_state(const boost::asio::any_io_executor &executor): resolver { executor }, acceptor { executor } {}

            tcp::resolver resolver;
            tcp::acceptor acceptor;
            std::set<tcp_connection *> clients;
            std::atomic_bool closed { false };

            void close()
            {
                resolver.cancel();
                boost::system::error_code ec;
                acceptor.close(ec);
                for (auto *client: clients) client->close();
                closed.store(true, std::memory_order_release);
            }
        };

        template<typename H>
        struct my_op_handler_t final: op_observer_t {
            my_op_handler_t(H &&h):
                _handler { std::move(h) }
            {
            }

            void done() override
            {
                _handler(op_result_ok_t {});
            }

            void failed(const std::string_view err) override
            {
                _handler(op_result_failed_t { std::string { err } });
            }

            void stopped() override
            {
                _handler(op_result_stopped_t {});
            }
        private:
            H _handler;
        };

        static boost::asio::awaitable<op_result_t> _async_process(const std::shared_ptr<multiplexer> &m,
            tcp_connection &socket, void (multiplexer::*method)(op_observer_ptr))
        {
            const auto token = boost::asio::use_awaitable;
            auto executor = co_await boost::asio::this_coro::executor;
            boost::asio::steady_timer deadline { executor, std::chrono::seconds { 30 } };
            deadline.async_wait([m, socket=&socket](const auto &ec) {
                if (!ec) {
                    logger::warn("node-api transfer timed out after 30 seconds");
                    socket->close();
                }
            });
            scope_exit cancel_deadline { [&] { deadline.cancel(); } };
            co_return co_await boost::asio::async_initiate<decltype(token), void(op_result_t)>(
                [m, method, executor](auto &&handler) {
                    auto observer = std::make_shared<my_op_handler_t<std::decay_t<decltype(handler)>>>(std::move(handler));
                    boost::asio::post(executor, [m, method, observer] {
                        if (logger::run_log_errors([&] {
                            if (m->alive())
                                ((*m).*method)(observer);
                            else
                                observer->failed("multiplexer is not in a working state");
                        }))
                            observer->failed("node-api operation failed");
                    });
                }, token);
        }

        const address _addr;
        const multiplexer_config_t _config;
        std::shared_ptr<asio::worker> _iow;
        boost::asio::strand<boost::asio::io_context::executor_type> _strand { boost::asio::make_strand(_iow->io_context()) };
        const std::shared_ptr<shutdown_state> _shutdown = std::make_shared<shutdown_state>(_strand);
        const size_t _max_connections;
        const std::shared_ptr<std::atomic_size_t> _open_connections = std::make_shared<std::atomic_size_t>(0);
        std::atomic_bool _stop_requested { false };
        std::mutex _futures_mutex alignas(mutex::alignment);
        std::vector<std::future<void>> _futures {};

        boost::asio::awaitable<void> _handle_client(tcp::socket conn, std::unique_ptr<connection_slot> slot)
        {
            if (_stop_requested.load(std::memory_order_relaxed)) co_return;
            auto transport = std::make_unique<tcp_connection>(std::move(conn), std::move(slot));
            auto *socket = transport.get();
            auto m = std::make_shared<multiplexer>(std::move(transport), multiplexer_config_t { _config });
            socket->owner(m);
            _shutdown->clients.emplace(socket);
            scope_exit close { [&] { _shutdown->clients.erase(socket); socket->close(); } };
            while (m->alive() && !_stop_requested.load(std::memory_order_relaxed)) {
                m->poll();
                if (m->available_ingress()) {
                    const auto res = co_await _async_process(m, *socket, &multiplexer::process_ingress);
                    if (!std::holds_alternative<op_result_ok_t>(res)) co_return;
                } else if (m->available_egress()) {
                    const auto res = co_await _async_process(m, *socket, &multiplexer::process_egress);
                    if (!std::holds_alternative<op_result_ok_t>(res)) co_return;
                } else {
                    boost::asio::steady_timer timer { co_await boost::asio::this_coro::executor };
                    timer.expires_after(50ms);
                    co_await timer.async_wait(boost::asio::use_awaitable);
                }
            }
        }

        boost::asio::awaitable<void> _listen()
        {
            auto ex = co_await boost::asio::this_coro::executor;
            if (_stop_requested.load(std::memory_order_relaxed)) co_return;
            boost::system::error_code ec;
            const auto results = co_await _shutdown->resolver.async_resolve(_addr.host, _addr.port,
                boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            if (_stop_requested.load(std::memory_order_relaxed)) co_return;
            if (ec) throw boost::system::system_error { ec };
            if (results.empty()) [[unlikely]]
                throw error(fmt::format("DNS resolve for {}:{} returned no results!", _addr.host, _addr.port));
            auto &acceptor = _shutdown->acceptor;
            const tcp::endpoint endpoint = *results.begin();
            acceptor.open(endpoint.protocol());
            acceptor.set_option(tcp::acceptor::reuse_address(true));
            acceptor.bind(endpoint);
            acceptor.listen();
            while (!_stop_requested.load(std::memory_order_relaxed)) {
                auto socket = co_await acceptor.async_accept(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
                if (_stop_requested.load(std::memory_order_relaxed)) co_return;
                if (ec) throw boost::system::system_error { ec };
                if (_open_connections->load(std::memory_order_relaxed) >= _max_connections) {
                    socket.close(ec);
                    continue;
                }
                auto slot = std::make_unique<connection_slot>(_open_connections);
                std::scoped_lock lock { _futures_mutex };
                _futures.emplace_back(co_spawn(ex, _handle_client(std::move(socket), std::move(slot)), boost::asio::use_future));
            }
        }
    };

    server server::make_default(const address &addr, const std::string &data_dir, const asio::worker_ptr &iow,
            const cardano::config &ccfg, size_t max_connections, size_t cache_bytes)
    {
        auto cr = std::make_shared<chunk_registry>(data_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store, .ccfg=ccfg });
        return make_default(addr, std::make_shared<chain_source>(std::move(cr), cache_bytes), iow, ccfg, max_connections);
    }

    server server::make_default(const address &addr, std::shared_ptr<chain_source> cr, const asio::worker_ptr &iow,
            const cardano::config &ccfg, size_t max_connections)
    {
        const auto pm = ccfg.byron_protocol_magic;
        struct keepalive_handler: protocol_observer_t {
            void data(buffer bytes, const protocol_send_func &send) override
            {
                auto parsed = cbor::zero2::parse(bytes);
                auto &items = parsed.get().array();
                const auto tag = items.read().uint();
                if (tag == 2) return;
                if (tag != 0) throw error("invalid KeepAlive request");
                const auto cookie = items.read().uint();
                send([](uint64_t cookie) -> data_generator_t {
                    cbor::encoder enc;
                    enc.array(2).uint(1).uint(cookie);
                    co_yield std::move(enc.cbor());
                }(cookie));
            }
            void failed(std::string_view) override {}
            void stopped() override {}
        };
        const multiplexer_config_t cfg {
            { mini_protocol::handshake, [pm](const auto &) {
                return std::make_shared<miniprotocol::handshake::handler>(
                    miniprotocol::handshake::version_map {
                        { 14, miniprotocol::handshake::node_to_node_version_data_t { pm, false, false, false } },
                        { 999, miniprotocol::handshake::node_to_node_version_data_t { pm, false, false, false } }
                    },
                    999
                );
            } },
            { mini_protocol::keep_alive, [](const auto &) { return std::make_shared<keepalive_handler>(); } },
            { mini_protocol::chain_sync, [cr](const auto &) { return std::make_shared<miniprotocol::chainsync::handler>(cr); } },
            { mini_protocol::block_fetch, [cr](const auto &res) {
                return std::make_shared<miniprotocol::blockfetch::handler>(cr, miniprotocol::blockfetch::config_t { .block_compression=res.version==999 });
            } }
        };
        return { addr, std::move(cfg), iow, ccfg, max_connections };
    }

    server::server(const address &addr, const multiplexer_config_t &&mcfg, const asio::worker_ptr &iow,
            const cardano::config &cfg, size_t max_connections):
        _impl { std::make_unique<impl>(addr, std::move(mcfg), iow, cfg, max_connections) }
    {
    }

    server::~server() =default;

    void server::run()
    {
        _impl->run();
    }

    void server::stop()
    {
        _impl->stop();
    }
}
