/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

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
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/use_awaitable.hpp>
#ifdef DT_CLEAR_BOOST_DEPRECATED_HEADERS
#   undef BOOST_ALLOW_DEPRECATED_HEADERS
#   undef DT_CLEAR_BOOST_DEPRECATED_HEADERS
#endif

#include <dt/chunk-registry.hpp>
#include "miniprotocol/blockfetch/handler.hpp"
#include "miniprotocol/chainsync/handler.hpp"
#include "miniprotocol/handshake/handler.hpp"
#include "server.hpp"

namespace daedalus_turbo::cardano::network {
    struct server::impl {
        impl(const address &addr, const multiplexer_config_t &&mcfg, const asio::worker_ptr &iow, const config &):
            _addr { std::move(addr) }, _config { std::move(mcfg) }, _iow { iow }
        {
            std::scoped_lock lk { _futures_mutex };
            _futures.emplace_back(boost::asio::co_spawn(_iow->io_context(), _listen(), boost::asio::use_future));
        }

        ~impl()
        {
            _destroy = true;
            for (const auto &f: _futures) {
                while (f.wait_for(std::chrono::milliseconds { 0 }) != std::future_status::ready) {
                    _iow->io_context().run_one();
                }
            }
        }

        void run()
        {
            while (!_iow->io_context().stopped() && !_destroy) {
                _iow->io_context().run_for(std::chrono::milliseconds { 100 });
            }
        }
    private:
        using tcp = boost::asio::ip::tcp;

        struct tcp_connection: connection {
            tcp_connection(tcp::socket &&conn):
                _conn { std::move(conn) }
            {
            }

            static void process_transfer_result(const std::string_view op_name, const std::error_code ec, const size_t transferred, const size_t expected, const op_observer_ptr observer)
            {
                if (transferred != expected) [[unlikely]]
                    observer->failed(fmt::format("asio::{}: completed only {} bytes while expected {}", op_name, transferred, expected));
                if (ec) [[unlikely]]
                    observer->failed(fmt::format("asio::{} error: {}", op_name, ec.message()));
                else
                    observer->done();
            }

            size_t available_ingress() const override
            {
                return _conn.available();
            }

            void async_read(const write_buffer buf, op_observer_ptr observer) override
            {
                _conn.async_read_some(boost::asio::mutable_buffer { buf.data(), buf.size() }, [observer, buf](const auto &ec, const size_t transferred) {
                    process_transfer_result("read", ec, transferred, buf.size(), observer);
                });
            }

            void async_write(const buffer buf, op_observer_ptr observer) override
            {
                _conn.async_write_some(boost::asio::const_buffer { buf.data(), buf.size() }, [buf, observer](const auto &ec, const size_t transferred) {
                    process_transfer_result("write", ec, transferred, buf.size(), observer);
                });
            }
        private:
            tcp::socket _conn;
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

        static boost::asio::awaitable<op_result_t> _async_process(boost::asio::io_context &ioc, std::shared_ptr<multiplexer> &m, void (multiplexer::*method)(op_observer_ptr))
        {
            using namespace boost::asio::experimental::awaitable_operators;
            const auto token = boost::asio::use_awaitable;
            auto executor = co_await boost::asio::this_coro::executor;
            op_result_t res = op_result_failed_t { "an async operation has taken too long!" };
            auto deadline = boost::asio::steady_timer { executor, std::chrono::seconds { 1 } };
            op_observer_ptr handler_ptr {};
            const auto wait_res = co_await (
                boost::asio::async_initiate<decltype(token), void(op_result_t)>(
                    [=, &ioc](auto &&handler) mutable {
                        handler_ptr = std::make_shared<my_op_handler_t<std::decay_t<decltype(handler)>>>(std::move(handler));
                        ioc.post(
                            [m, method, my_handler=handler_ptr]() mutable {
                                if (m->alive())
                                    ((*m).*method)(my_handler);
                                else
                                    my_handler->failed("multiplexer is not in a working state");
                            }
                        );
                    },
                    token
                )
                || deadline.async_wait(token)
            );
            std::visit([&](auto &&rv) {
                using T = std::decay_t<decltype(rv)>;
                if constexpr (std::is_same_v<T, op_result_ok_t> || std::is_same_v<T, op_result_failed_t> || std::is_same_v<T, op_result_stopped_t>) {
                    deadline.cancel();
                    res = std::move(rv);
                } else {
                    logger::error("an async operation has taken too long and has been cancelled");
                    if (handler_ptr) {
                        handler_ptr->stopped();
                    }
                }
            }, wait_res);
            co_return res;
        }

        const address _addr;
        const multiplexer_config_t _config;
        std::shared_ptr<asio::worker> _iow;
        std::atomic_bool _destroy { false };
        std::mutex _futures_mutex alignas(mutex::alignment);
        std::vector<std::future<void>> _futures {};

        boost::asio::awaitable<void> _handle_client(tcp::socket conn)
        {
            using namespace boost::asio::experimental::awaitable_operators;
            auto m = std::make_shared<multiplexer>(std::make_unique<tcp_connection>(std::move(conn)),
                multiplexer_config_t { _config });
            while (m->alive() && !_destroy.load(std::memory_order_relaxed)) {
                if (m->available_ingress()) {
                    co_await _async_process(_iow->io_context(),  m, &multiplexer::process_ingress);
                } else if (m->available_egress()) {
                    co_await _async_process(_iow->io_context(), m, &multiplexer::process_egress);
                } else {
                    boost::asio::steady_timer timer { co_await boost::asio::this_coro::executor };
                    timer.expires_after(50ms);
                    co_await timer.async_wait(boost::asio::use_awaitable);
                }
            }
        }

        boost::asio::awaitable<void> _listen()
        {
            using namespace boost::asio::experimental::awaitable_operators;
            auto ex = co_await boost::asio::this_coro::executor;
            tcp::resolver resolver { _iow->io_context() };
            const auto results = co_await resolver.async_resolve(_addr.host, _addr.port, boost::asio::use_awaitable);
            if (results.empty())
                throw error(fmt::format("DNS resolve for {}:{} returned no results!", _addr.host, _addr.port));
            tcp::acceptor acceptor { _iow->io_context(), *results.begin() };
            while (!_destroy.load(std::memory_order_relaxed)) {
                boost::asio::steady_timer timer(ex);
                timer.expires_after(std::chrono::milliseconds { 500ms });
                auto res = co_await (acceptor.async_accept(boost::asio::use_awaitable) || timer.async_wait(boost::asio::use_awaitable));
                std::visit([&](auto &&rv) {
                    using T = std::decay_t<decltype(rv)>;
                    if constexpr (std::is_same_v<T, tcp::socket>) {
                        timer.cancel();
                        std::scoped_lock lock { _futures_mutex };
                        _futures.emplace_back(co_spawn(ex, _handle_client(std::move(rv)), boost::asio::use_future));
                    } else {
                        acceptor.cancel();
                    }
                }, std::move(res));
            }
        }
    };

    server server::make_default(const address &addr, const std::string &data_dir, const asio::worker_ptr &iow, const cardano::config &ccfg)
    {
        const auto pm = ccfg.byron_protocol_magic;
        const auto cr = std::make_shared<chunk_registry>(data_dir, chunk_registry::mode::store, ccfg);
        const multiplexer_config_t cfg {
            { mini_protocol::handshake, [pm](const auto &) {
                return std::make_shared<miniprotocol::handshake::handler>(
                    miniprotocol::handshake::version_map {
                        { 14, miniprotocol::handshake::node_to_node_version_data_t { pm, false, false, false } },
                        { 15, miniprotocol::handshake::node_to_node_version_data_t { pm, false, false, false } }
                    },
                    15
                );
            } },
            { mini_protocol::chain_sync, [cr](const auto &) { return std::make_shared<miniprotocol::chainsync::handler>(cr); } },
            { mini_protocol::block_fetch, [cr](const auto &res) {
                return std::make_shared<miniprotocol::blockfetch::handler>(cr, miniprotocol::blockfetch::config_t { .block_compression=res.version>=15 });
            } }
        };
        return { addr, std::move(cfg), iow, ccfg };
    }

    server::server(const address &addr, const multiplexer_config_t &&mcfg, const asio::worker_ptr &iow, const cardano::config &cfg):
        _impl { std::make_unique<impl>(addr, std::move(mcfg), iow, cfg) }
    {
    }

    server::~server() =default;

    void server::run()
    {
        _impl->run();
    }
}
