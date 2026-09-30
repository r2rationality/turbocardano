/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#ifdef _MSC_VER
#   include <SDKDDKVer.h>
#endif
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/stacktrace/stacktrace.hpp>
#include <thread>
#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>
#include <turbo/storage/test.hpp>
#include "miniprotocol/blockfetch/handler.hpp"
#include "miniprotocol/chainsync/handler.hpp"
#include "miniprotocol/handshake/handler.hpp"
#include "server.hpp"

namespace {
    using namespace turbo;
    using namespace turbo::cardano;
    using namespace turbo::cardano::network;
    using namespace turbo::cardano::network::miniprotocol;

    void pump_until(boost::asio::io_context &ioc, const auto &ready,
        const std::source_location &loc=std::source_location::current())
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds { 5 };
        while (!ready()) {
            expect(std::chrono::steady_clock::now() < deadline)
                << fmt::format("network operation did not complete; called from {}", loc) << fatal;
            ioc.restart();
            ioc.run_for(std::chrono::milliseconds { 10 });
        }
    }

    template<typename F>
    struct timer_task {
        using timer_ptr_t = std::shared_ptr<boost::asio::deadline_timer>;

        timer_task(const timer_ptr_t &timer, const F &func):
            _timer { timer },
            _func { func }
        {
        }

        void operator()(const boost::system::error_code &ec) const
        {
            if (!ec) [[likely]] {
                _func();
            } else {
                logger::debug("{}", boost::stacktrace::to_string(boost::stacktrace::stacktrace {}));
                logger::debug("timer cancelled or failed: {}", ec.message());
            }
        }
    private:
        timer_ptr_t _timer;
        F _func;
    };

    template<typename T, typename F>
    auto run_delayed(boost::asio::io_context &ioc, T duration_or_time, const F &f)
    {
        auto timer_ptr = std::make_shared<boost::asio::deadline_timer>(ioc, duration_or_time);
        auto &timer = *timer_ptr;
        timer.async_wait(timer_task { timer_ptr, f });
        return timer_ptr;
    }

    struct failure_test_state {
        bool fail = true;
        size_t active = 0;
    };

    struct failure_test_handler: handshake::observer_t {
        explicit failure_test_handler(std::shared_ptr<failure_test_state> state): _state { std::move(state) }
        {
            ++_state->active;
        }
        ~failure_test_handler() override { --_state->active; }
        void on_success(const handshake::on_success_func &) override {}
        void failed(std::string_view) override {}
        void stopped() override {}

        void data(buffer bytes, const protocol_send_func &send) override
        {
            if (bytes[0] == 3) {
                _poll = true;
                return;
            }
            if (bytes[0] == 1) check(*_state);
            send([](std::shared_ptr<failure_test_state> state, bool fail_after_send) -> data_generator_t {
                co_yield uint8_vector { 0 };
                if (fail_after_send) check(*state);
            }(_state, bytes[0] == 2));
        }

        void poll(const protocol_send_func &) override
        {
            if (_poll) { _poll = false; check(*_state); }
        }
    private:
        std::shared_ptr<failure_test_state> _state;
        bool _poll = false;

        static void check(const failure_test_state &state)
        {
            if (state.fail) throw error("injected response failure");
        }
    };

    struct fragmented_chain_handler: chainsync::handler {
        using chainsync::handler::handler;

        void data(buffer bytes, const protocol_send_func &send) override
        {
            auto parsed = cbor::zero2::parse(bytes);
            const bool request_next = parsed.get().array().read().uint() == 0;
            chainsync::handler::data(bytes, [&](data_generator_t &&gen) {
                if (request_next)
                    send(fragment(std::move(gen)));
                else
                    send(std::move(gen));
            });
        }
    private:
        static data_generator_t fragment(data_generator_t gen)
        {
            while (gen.resume()) {
                auto bytes = gen.result();
                // AwaitReply followed by only the opening array token of the reply.
                co_yield uint8_vector { 0x81, 0x01, bytes.front() };
                bytes.erase(bytes.begin());
                co_yield std::move(bytes);
            }
        }
    };
}

suite cardano_network_server_suite = [] {
    "cardano::network::server"_test = [] {
        static constexpr size_t timeout_sec = 300;
        const auto cr = std::make_shared<chunk_registry>(turbo::storage::sample_registry_path(), chunk_registry_settings_t { .mode=chunk_registry::mode::store });
        const network::address listen_addr { "127.0.0.1", "9876" };
        const auto chainsync_h = std::make_shared<chainsync::handler>(cr);
        const auto blockfetch_14_h = std::make_shared<blockfetch::handler>(cr);
        const auto blockfetch_15_h = std::make_shared<blockfetch::handler>(cr, blockfetch::config_t { .block_compression=true });
        const version_config_t v14 { 14, 14 };
        const version_config_t v14v15 { 14, 15 };

        const multiplexer_config_t cfg {
            { mini_protocol::handshake, [&](const auto &) {
                return std::make_shared<handshake::handler>(
                    handshake::version_map {
                        { 14, handshake::node_to_node_version_data_t { cr->config().byron_protocol_magic, false, false, false } },
                        { 15, handshake::node_to_node_version_data_t { cr->config().byron_protocol_magic, false, false, false } }
                    },
                    15
                );
            } },
            { mini_protocol::chain_sync, [&](const auto &) { return chainsync_h; } },
            { mini_protocol::block_fetch, [&](const auto &res) { return res.version == 15 ? blockfetch_15_h : blockfetch_14_h; } }
        };
        "connection limits, response failures, and shutdown release clients"_test = [&] {
            using tcp = boost::asio::ip::tcp;
            const auto iow = std::make_shared<asio::worker_manual>();
            auto &ioc = iow->io_context();
            const auto state = std::make_shared<failure_test_state>();
            server srv { listen_addr, {
                { mini_protocol::handshake, [state](const auto &) { return std::make_shared<failure_test_handler>(state); } }
            }, iow, cr->config(), 1 };
            const tcp::endpoint endpoint { boost::asio::ip::make_address(listen_addr.host),
                static_cast<uint16_t>(std::stoul(listen_addr.port)) };
            const auto connect = [&](const std::source_location &loc=std::source_location::current()) {
                tcp::socket socket { ioc };
                pump_until(ioc, [&] {
                    boost::system::error_code ec;
                    socket.close(ec);
                    socket.connect(endpoint, ec);
                    return !ec;
                }, loc);
                socket.non_blocking(true);
                return socket;
            };
            const auto await_close = [&](tcp::socket &socket, const std::source_location &loc=std::source_location::current()) {
                pump_until(ioc, [&] {
                    uint8_t bytes[256];
                    boost::system::error_code ec;
                    socket.read_some(boost::asio::buffer(bytes), ec);
                    return ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset;
                }, loc);
            };
            const auto send = [](tcp::socket &socket, uint8_t request) {
                const segment_info header { 1, channel_mode::initiator, mini_protocol::handshake, 1 };
                uint8_vector msg { buffer::from(header) };
                msg.push_back(request);
                boost::asio::write(socket, boost::asio::buffer(msg.data(), msg.size()));
            };
            auto first = connect();
            pump_until(ioc, [&] { return state->active == 1; });
            auto rejected = connect();
            await_close(rejected);
            expect_equal(state->active, 1);
            first.close();
            pump_until(ioc, [&] { return state->active == 0; });

            for (const uint8_t request: { 1, 2, 3 }) {
                auto socket = connect();
                pump_until(ioc, [&] { return state->active == 1; });
                send(socket, request);
                await_close(socket);
                pump_until(ioc, [&] { return state->active == 0; });
            }
            state->fail = false;
            auto healthy = connect();
            pump_until(ioc, [&] { return state->active == 1; });
            send(healthy, 1);
            pump_until(ioc, [&] { return healthy.available() > 0; });
            expect_equal(state->active, 1);
            healthy.close();
            pump_until(ioc, [&] { return state->active == 0; });

            // Leave a mux payload incomplete to exercise cancellation of a pending read.
            auto stalled = connect();
            pump_until(ioc, [&] { return state->active == 1; });
            const segment_info partial { 1, channel_mode::initiator, mini_protocol::handshake, 1000 };
            uint8_vector bytes { buffer::from(partial) };
            bytes.push_back(1);
            boost::asio::write(stalled, boost::asio::buffer(bytes.data(), bytes.size()));
            ioc.run_for(std::chrono::milliseconds { 20 });
            const auto started = std::chrono::steady_clock::now();
            srv.stop();
            srv.run();
            await_close(stalled);
            pump_until(ioc, [&] { return state->active == 0; });
            expect(std::chrono::steady_clock::now() - started < std::chrono::seconds { 2 });
            tcp::socket after_stop { ioc };
            boost::system::error_code ec;
            after_stop.connect(endpoint, ec);
            expect(ec == boost::asio::error::connection_refused);
        };
        "next_header decodes fragmented replies on one connection"_test = [&] {
            const auto iow = std::make_shared<asio::worker_manual>();
            auto &ioc = iow->io_context();
            auto fragmented_cfg = cfg;
            fragmented_cfg[mini_protocol::chain_sync] = [cr](const auto &) {
                return std::make_shared<fragmented_chain_handler>(cr);
            };
            server srv { listen_addr, std::move(fragmented_cfg), iow, cr->config() };
            ioc.run_for(std::chrono::milliseconds { 10 });
            const auto c = client_manager_async::get().connect(listen_addr, v14, cr->config(), iow);
            client::find_response found {};
            c->find_intersection(optional_point2_list { optional_point2 {} }, [&](auto &&resp) { found = std::move(resp); });
            c->process(nullptr, iow.get());
            expect(fatal(std::holds_alternative<intersection_info_t>(found.res)));
            expect(fatal(std::get<intersection_info_t>(found.res).found));
            std::stop_source stop;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds { 5 };
            const auto pump = [&] {
                if (std::chrono::steady_clock::now() >= deadline) stop.request_stop();
                ioc.run_for(std::chrono::milliseconds { 10 });
            };
            const auto rollback = c->next_header_sync(stop.get_token(), pump);
            expect(rollback.rollback);
            expect(!rollback.point);
            expect_equal(rollback.tip, optional_point3 { cr->tip() });
            const auto forward = c->next_header_sync(stop.get_token(), pump);
            expect(!forward.rollback);
            expect(fatal(forward.point.has_value()));
            expect_equal(*forward.point, point2 { cr->cbegin()->slot, cr->cbegin()->hash });
            expect_equal(forward.tip, rollback.tip);
            srv.stop();
        };
        "client reports a closed socket without waiting for its deadline"_test = [&] {
            struct failing_blockfetch: blockfetch::handler {
                using blockfetch::handler::handler;
                void data(buffer, const protocol_send_func &) override
                {
                    throw error("injected blockfetch connection failure");
                }
            };
            const auto iow = std::make_shared<asio::worker_manual>();
            auto failing_cfg = cfg;
            failing_cfg[mini_protocol::block_fetch] = [cr](const auto &) {
                return std::make_shared<failing_blockfetch>(cr);
            };
            server srv { listen_addr, std::move(failing_cfg), iow, cr->config() };
            iow->io_context().run_for(std::chrono::milliseconds { 10 });
            const auto c = client_manager_async::get().connect(listen_addr, v14, cr->config(), iow);
            std::string failure;
            const auto started = std::chrono::steady_clock::now();
            c->fetch_blocks(cr->cbegin()->point(), *cr->tip(), [&](auto response) {
                if (const auto err = std::get_if<client::error_msg>(&response)) failure = *err;
                return true;
            });
            c->process(nullptr, iow.get());
            expect(!failure.empty());
            expect(failure.find("timed out") == std::string::npos);
            expect(std::chrono::steady_clock::now() - started < std::chrono::seconds { 5 });
            srv.stop();
        };
        const auto check_blockfetch_completion = [&](const bool compressed, const bool interrupt, const std::source_location &loc=std::source_location::current()) {
            const auto context = fmt::format("check_blockfetch_completion called from {}", loc);
            struct test_blockfetch: blockfetch::handler {
                const bool compressed;
                const bool finish;

                test_blockfetch(std::shared_ptr<chunk_registry> registry, bool compressed_, bool finish_)
                    : blockfetch::handler { std::move(registry) }, compressed { compressed_ }, finish { finish_ } {}

                void data(buffer, const protocol_send_func &send) override
                {
                    send([](bool compressed, bool finish) -> data_generator_t {
                        cbor::encoder enc;
                        blockfetch::msg_start_batch_t {}.to_cbor(enc);
                        if (compressed)
                            blockfetch::msg_compressed_blocks_t {
                                blockfetch::msg_compressed_blocks_t::encoding_raw, uint8_vector { 0x80 }
                            }.to_cbor(enc);
                        else
                            blockfetch::msg_block_t { uint8_vector { 0x80 } }.to_cbor(enc);
                        // An interrupted fetch must finish even if BatchDone never arrives.
                        if (finish) blockfetch::msg_batch_done_t {}.to_cbor(enc);
                        co_yield std::move(enc.cbor());
                    }(compressed, finish));
                }
            };
            const auto iow = std::make_shared<asio::worker_manual>();
            auto &ioc = iow->io_context();
            size_t connections = 0;
            auto test_cfg = cfg;
            test_cfg[mini_protocol::block_fetch] = [&](const auto &) {
                return std::make_shared<test_blockfetch>(cr, compressed, ++connections > 1 || !interrupt);
            };
            server srv { listen_addr, std::move(test_cfg), iow, cr->config() };
            ioc.run_for(std::chrono::milliseconds { 10 });
            std::stop_source stop;
            const auto c = client_manager_async::get().connect(listen_addr, compressed ? v14v15 : v14, cr->config(), iow);
            c->set_stop_token(stop.get_token());
            boost::asio::steady_timer deadline { ioc };
            size_t messages = 0;
            size_t errors = 0;
            const auto fetch = [&](const bool stop_early, const std::source_location &fetch_loc=std::source_location::current()) {
                const auto fetch_context = fmt::format("fetch called from {}; {}", fetch_loc, context);
                deadline.expires_after(std::chrono::seconds { 5 });
                deadline.async_wait([&](const auto &ec) { if (!ec) stop.request_stop(); });
                c->fetch_blocks(point2 {}, point2 {}, [&](auto response) {
                    if (std::holds_alternative<client::error_msg>(response)) {
                        ++errors;
                    } else {
                        ++messages;
                        expect_equal(std::holds_alternative<client::msg_compressed_blocks_t>(response), compressed, fetch_context);
                    }
                    return !stop_early;
                });
                c->process(nullptr, iow.get());
                deadline.cancel();
                ioc.poll();
                expect(!stop.stop_requested())
                    << fmt::format("BlockFetch waited for the missing BatchDone; {}", fetch_context) << fatal;
                expect_equal(errors, 0, fetch_context);
            };
            fetch(interrupt);
            expect_equal(messages, 1, context);
            expect_equal(connections, 1, context);
            fetch(false);
            expect_equal(messages, 2, context);
            expect_equal(connections, interrupt ? 2 : 1, context);
            srv.stop();
        };
        "interrupted raw BlockFetch closes without BatchDone and reconnects"_test = [&] {
            check_blockfetch_completion(false, true);
        };
        "interrupted compressed BlockFetch closes without BatchDone and reconnects"_test = [&] {
            check_blockfetch_completion(true, true);
        };
        "completed raw BlockFetch reuses its connection"_test = [&] {
            check_blockfetch_completion(false, false);
        };
        "completed compressed BlockFetch reuses its connection"_test = [&] {
            check_blockfetch_completion(true, false);
        };
        "idle callback failure cancels and drains a pending ChainSync request"_test = [&] {
            const auto iow = std::make_shared<asio::worker_manual>();
            auto &ioc = iow->io_context();
            auto quiet_cfg = cfg;
            quiet_cfg[mini_protocol::chain_sync] = [cr](const auto &) {
                return std::make_shared<chainsync::handler>(cr);
            };
            server srv { listen_addr, std::move(quiet_cfg), iow, cr->config() };
            ioc.run_for(std::chrono::milliseconds { 10 });
            const auto c = client_manager_async::get().connect(listen_addr, v14, cr->config(), iow);
            const auto intersect = [&](const std::source_location &loc=std::source_location::current()) {
                const auto context = fmt::format("intersect called from {}", loc);
                client::find_response found;
                c->find_intersection(optional_point2_list { *cr->tip() }, [&](auto &&r) { found = std::move(r); });
                c->process(nullptr, iow.get());
                expect(std::holds_alternative<intersection_info_t>(found.res)) << context << fatal;
                expect(std::get<intersection_info_t>(found.res).found) << context << fatal;
            };
            intersect();
            const auto pump = [&] { ioc.run_for(std::chrono::milliseconds { 10 }); };
            expect(c->next_header_sync({}, pump).rollback);
            const auto started = std::chrono::steady_clock::now();
            std::stop_source stop;
            size_t idle_calls = 0;
            std::string failure;
            try {
                c->next_header_sync(stop.get_token(), [&] {
                    pump();
                    if (std::chrono::steady_clock::now() - started >= std::chrono::seconds { 5 }) stop.request_stop();
                    if (++idle_calls == 2) throw error("injected progress timeout");
                });
            } catch (const std::exception &ex) {
                failure = ex.what();
            }
            expect_equal(failure, "injected progress timeout");
            expect_equal(idle_calls, 2);
            expect(std::chrono::steady_clock::now() - started < std::chrono::seconds { 5 });
            intersect();
            expect(c->next_header_sync({}, pump).rollback);
            srv.stop();
        };
        "stop token interrupts an unfinished upstream handshake"_test = [&] {
            struct silent_handshake: handshake::observer_t {
                bool &requested;
                explicit silent_handshake(bool &seen): requested { seen } {}
                void on_success(const handshake::on_success_func &) override {}
                void data(buffer, const protocol_send_func &) override { requested = true; }
                void failed(std::string_view) override {}
                void stopped() override {}
            };
            const auto iow = std::make_shared<asio::worker_manual>();
            auto &ioc = iow->io_context();
            bool requested = false;
            server srv { listen_addr, {
                { mini_protocol::handshake, [&](const auto &) { return std::make_shared<silent_handshake>(requested); } }
            }, iow, cr->config() };
            ioc.run_for(std::chrono::milliseconds { 10 });
            std::stop_source stop;
            auto c = client_manager_async::get().connect(listen_addr, v14, cr->config(), iow);
            c->set_stop_token(stop.get_token());
            std::optional<client::find_response> response;
            c->find_tip([&](auto &&r) { response = std::move(r); });
            pump_until(ioc, [&] { return requested; });
            const auto started = std::chrono::steady_clock::now();
            stop.request_stop();
            c->process(nullptr, iow.get());
            expect(fatal(response.has_value()));
            expect(std::holds_alternative<client::error_msg>(response->res));
            expect(std::chrono::steady_clock::now() - started < std::chrono::seconds { 2 });
            srv.stop();
        };
        "stop token interrupts a BlockFetch that never sends BatchDone"_test = [&] {
            struct stalled_blockfetch: blockfetch::handler {
                bool &requested;
                stalled_blockfetch(std::shared_ptr<chunk_registry> registry, bool &seen)
                    : blockfetch::handler { std::move(registry) }, requested { seen } {}
                void data(buffer, const protocol_send_func &send) override
                {
                    requested = true;
                    send([]() -> data_generator_t {
                        cbor::encoder enc;
                        blockfetch::msg_start_batch_t {}.to_cbor(enc);
                        co_yield std::move(enc.cbor());
                    }());
                }
            };
            const auto iow = std::make_shared<asio::worker_manual>();
            auto &ioc = iow->io_context();
            bool requested = false;
            auto stalled_cfg = cfg;
            stalled_cfg[mini_protocol::block_fetch] = [&](const auto &) {
                return std::make_shared<stalled_blockfetch>(cr, requested);
            };
            server srv { listen_addr, std::move(stalled_cfg), iow, cr->config() };
            ioc.run_for(std::chrono::milliseconds { 10 });
            std::stop_source stop;
            auto c = client_manager_async::get().connect(listen_addr, v14, cr->config(), iow);
            c->set_stop_token(stop.get_token());
            size_t errors = 0;
            c->fetch_blocks(point2 {}, point2 {}, [&](auto response) {
                if (std::holds_alternative<client::error_msg>(response)) ++errors;
                return false;
            });
            pump_until(ioc, [&] { return requested; });
            ioc.run_for(std::chrono::milliseconds { 20 });
            const auto started = std::chrono::steady_clock::now();
            // Exercise the cross-thread handoff used by the signal handler.
            std::jthread stopper { [&] { stop.request_stop(); } };
            stopper.join();
            c->process(nullptr, iow.get());
            expect_equal(errors, 1);
            expect(std::chrono::steady_clock::now() - started < std::chrono::seconds { 2 });
            srv.stop();
        };
        "inquire the tip"_test = [&] {
            expect(fatal(cr->tip().has_value()));
            const auto iow = std::make_shared<asio::worker_manual>();
            const auto work_guard = boost::asio::make_work_guard(iow->io_context());
            std::atomic_bool timer_stop = false;
            const auto timer = run_delayed(iow->io_context(), boost::posix_time::seconds { timeout_sec }, [&] {
                timer_stop.store(true, std::memory_order_relaxed);
                iow->io_context().stop();
            });
            server s { listen_addr, multiplexer_config_t { cfg }, iow, cr->config() };
            std::optional<client::find_response> tip_resp {};
            {
                const auto client = client_manager_async::get().connect(listen_addr, v14, cr->config(), iow);
                client->find_tip([&](auto &&resp) {
                    iow->io_context().post([&] {
                        timer->cancel();
                    });
                    tip_resp.emplace(std::move(resp));
                    iow->io_context().stop();
                });
                iow->io_context().run();
                iow->io_context().restart();
            }
            expect(!timer_stop.load(std::memory_order_relaxed));
            if (tip_resp.has_value() && std::holds_alternative<intersection_info_t>(tip_resp->res)) {
                const auto &isect = std::get<intersection_info_t>(tip_resp->res);
                expect_equal(optional_point3 { cr->tip() }, isect.tip);
            } else {
                expect(false);
            }
        };
        "fetch byron headers"_test = [&] {
            expect(fatal(cr->tip().has_value())) << "the chain cannot be empty";
            const auto iow = std::make_shared<asio::worker_manual>();
            {
                std::atomic_bool timer_stop = false;
                std::atomic_size_t num_blocks { 0 };
                std::atomic_size_t num_errs { 0 };
                const auto timer = run_delayed(iow->io_context(), boost::posix_time::seconds { timeout_sec }, [&] {
                    timer_stop.store(true, std::memory_order_relaxed);
                    iow->io_context().stop();
                });
                static constexpr size_t num_hdrs = 5;
                {
                    auto work_guard = boost::asio::make_work_guard(iow->io_context());
                    server s { listen_addr, multiplexer_config_t { cfg }, iow, cr->config() };
                    auto client = client_manager_async::get().connect(listen_addr, v14, cr->config(), iow);
                    const optional_point2_list start_points {};
                    client->fetch_headers(start_points, num_hdrs, [&](auto &&resp) {
                        std::visit([&](const auto &rv) {
                            using RT = std::decay_t<decltype(rv)>;
                            if constexpr (std::is_same_v<RT, client::error_msg>) {
                                logger::warn("fetch_blocks err: {}", rv);
                                num_errs.fetch_add(1, std::memory_order_relaxed);
                                iow->io_context().post([&] {
                                    timer->cancel();
                                    iow->io_context().stop();
                                });
                            } else {
                                num_blocks.fetch_add(rv.size(), std::memory_order_relaxed);
                            }
                        }, resp.res);
                    });
                }
                expect(!timer_stop.load(std::memory_order_relaxed));
                expect_equal(num_hdrs, num_blocks.load(std::memory_order_relaxed));
                expect_equal(0, num_errs.load(std::memory_order_relaxed));
            }
        };
        "fetch shelley headers"_test = [&] {
            expect(fatal(cr->tip().has_value())) << "the chain cannot be empty";
            const auto iow = std::make_shared<asio::worker_manual>();
            {
                std::atomic_bool timer_stop = false;
                std::atomic_size_t num_blocks { 0 };
                std::atomic_size_t num_errs { 0 };
                const auto timer = run_delayed(iow->io_context(), boost::posix_time::seconds { timeout_sec }, [&] {
                    timer_stop.store(true, std::memory_order_relaxed);
                    iow->io_context().stop();
                });
                static constexpr size_t num_hdrs = 5;
                {
                    auto work_guard = boost::asio::make_work_guard(iow->io_context());
                    server s { listen_addr, multiplexer_config_t { cfg }, iow, cr->config() };
                    auto client = client_manager_async::get().connect(listen_addr, v14, cr->config(), iow);
                    const point2 from { 74044592, block_hash::from_hex("9903904F8A09D48FDAF19646D0907403536AFD6BE85C9BD7038A58BF0267A1AA") };
                    const optional_point2_list start_points { from };
                    client->fetch_headers(start_points, num_hdrs, [&](auto &&resp) {
                        std::visit([&](const auto &rv) {
                            using RT = std::decay_t<decltype(rv)>;
                            if constexpr (std::is_same_v<RT, client::error_msg>) {
                                logger::warn("fetch_blocks err: {}", rv);
                                num_errs.fetch_add(1, std::memory_order_relaxed);
                                iow->io_context().post([&] {
                                    timer->cancel();
                                    iow->io_context().stop();
                                });
                            } else {
                                num_blocks.fetch_add(rv.size(), std::memory_order_relaxed);
                            }
                        }, resp.res);
                    });
                }
                expect(!timer_stop.load(std::memory_order_relaxed));
                expect_equal(num_hdrs, num_blocks.load(std::memory_order_relaxed));
                expect_equal(0, num_errs.load(std::memory_order_relaxed));
            }
        };
        "fetch several blocks"_test = [&] {
            expect(fatal(cr->tip().has_value())) << "the chain cannot be empty";
            const auto iow = std::make_shared<asio::worker_manual>();
            {
                std::atomic_size_t num_blocks { 0 };
                std::atomic_size_t num_errs { 0 };
                std::atomic_bool timer_stop = false;
                const auto timer = run_delayed(iow->io_context(), boost::posix_time::seconds { timeout_sec }, [&] {
                    timer_stop.store(true, std::memory_order_relaxed);
                    iow->io_context().stop();
                });
                {
                    auto work_guard = boost::asio::make_work_guard(iow->io_context());
                    server s { listen_addr, multiplexer_config_t { cfg }, iow, cr->config() };
                    auto client = client_manager_async::get().connect(listen_addr, v14, cr->config(), iow);
                    const point2 from { 74044592, block_hash::from_hex("9903904F8A09D48FDAF19646D0907403536AFD6BE85C9BD7038A58BF0267A1AA") };
                    const point2 to { 74044785, block_hash::from_hex("43D6618AC1DC787EBCFEB99032109EBDA7A478723AA764A205773AE21C3EF743") };
                    client->fetch_blocks(from, to, [&, to](auto resp) {
                        return std::visit([&](auto &&rv) -> bool {
                            using T = std::decay_t<decltype(rv)>;
                            if constexpr (std::is_same_v<T, client::error_msg>) {
                                logger::warn("fetch_blocks err: {}", rv);
                                num_errs.fetch_add(1, std::memory_order_relaxed);
                                iow->io_context().post([&] {
                                    timer->cancel();
                                });
                                return false;
                            } else if constexpr (std::is_same_v<T, client::msg_block_t>) {
                                auto blk = std::make_unique<parsed_block>(rv.bytes);
                                num_blocks.fetch_add(1, std::memory_order_relaxed);
                                if (blk->blk->point2() == to) {
                                    iow->io_context().post([&] {
                                        timer->cancel();
                                    });
                                    return false;
                                }
                                return true;
                            } else {
                                logger::error("unsupported message: {}", typeid(T).name());
                                return false;
                            }
                        }, std::move(resp));
                    });
                }
                expect(!timer_stop.load(std::memory_order_relaxed));
                expect_equal(10, num_blocks.load(std::memory_order_relaxed));
                expect_equal(0, num_errs.load(std::memory_order_relaxed));
            }
        };

        "fetch compressed blocks"_test = [&] {
            expect(fatal(cr->tip().has_value())) << "the chain cannot be empty";
            const auto iow = std::make_shared<asio::worker_manual>();
            {
                std::atomic_size_t num_blocks { 0 };
                std::atomic_size_t num_errs { 0 };
                std::atomic_bool timer_stop = false;
                const auto timer = run_delayed(iow->io_context(), boost::posix_time::seconds { timeout_sec }, [&] {
                    timer_stop.store(true, std::memory_order_relaxed);
                    iow->io_context().stop();
                });
                {
                    auto work_guard = boost::asio::make_work_guard(iow->io_context());
                    server s { listen_addr, multiplexer_config_t { cfg }, iow, cr->config() };
                    auto client = client_manager_async::get().connect(listen_addr, v14v15, cr->config(), iow);
                    const point2 from { 74044592, block_hash::from_hex("9903904F8A09D48FDAF19646D0907403536AFD6BE85C9BD7038A58BF0267A1AA") };
                    const point2 to { 74044785, block_hash::from_hex("43D6618AC1DC787EBCFEB99032109EBDA7A478723AA764A205773AE21C3EF743") };
                    client->fetch_blocks(from, to, [&, to](auto &&resp) {
                        return std::visit([&](auto &&rv) -> bool {
                            using T = std::decay_t<decltype(rv)>;
                            if constexpr (std::is_same_v<T, client::error_msg>) {
                                logger::warn("fetch_blocks err: {}", rv);
                                num_errs.fetch_add(1, std::memory_order_relaxed);
                                iow->io_context().post([&] {
                                    timer->cancel();
                                    iow->io_context().stop();
                                });
                                return false;
                            } else if constexpr (std::is_same_v<T, client::msg_block_t>) {
                                num_blocks.fetch_add(1, std::memory_order_relaxed);
                                const auto blk = std::make_unique<parsed_block>(rv.bytes);
                                if (blk->blk->point2() == to) {
                                    iow->io_context().post([&] {
                                        timer->cancel();
                                    });
                                    return false;
                                }
                                return true;
                            } else if constexpr (std::is_same_v<T, client::msg_compressed_blocks_t>) {
                                const auto bytes = std::make_shared<uint8_vector>(rv.bytes());
                                cbor::zero2::decoder dec { *bytes };
                                while (!dec.done()) {
                                    num_blocks.fetch_add(1, std::memory_order_relaxed);
                                    const auto blk = std::make_unique<parsed_block>(bytes, dec.read());
                                    if (blk->blk->point2() == to) {
                                        iow->io_context().post([&] {
                                            timer->cancel();
                                        });
                                        return false;
                                    }
                                }
                                return true;
                            } else {
                                logger::error("unsupported message: {}", typeid(T).name());
                                return false;
                            }
                        }, std::move(resp));

                    });
                }
                expect(!timer_stop.load(std::memory_order_relaxed));
                expect_equal(10, num_blocks.load(std::memory_order_relaxed));
                expect_equal(0, num_errs.load(std::memory_order_relaxed));
            }
        };
    };
};
