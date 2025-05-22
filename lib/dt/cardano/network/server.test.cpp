/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#ifdef _MSC_VER
#   include <SDKDDKVer.h>
#endif
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/stacktrace/stacktrace.hpp>
#include <dt/chunk-registry.hpp>
#include <dt/common/test.hpp>
#include "miniprotocol/blockfetch/handler.hpp"
#include "miniprotocol/chainsync/handler.hpp"
#include "server.hpp"

namespace {
    using namespace daedalus_turbo;
    using namespace daedalus_turbo::cardano;
    using namespace daedalus_turbo::cardano::network;
    using namespace daedalus_turbo::cardano::network::miniprotocol;

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
                logger::debug(boost::stacktrace::to_string(boost::stacktrace::stacktrace {}));
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
}

suite cardano_network_server_suite = [] {
    "cardano::network::server"_test = [] {
        static constexpr size_t timeout_sec = 300;
        const auto cr = std::make_shared<chunk_registry>(install_path("data/chunk-registry"), chunk_registry::mode::store);
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
            }
            expect(!timer_stop.load(std::memory_order_relaxed));
            if (tip_resp.has_value() && std::holds_alternative<intersection_info_t>(tip_resp->res)) {
                const auto &isect = std::get<intersection_info_t>(tip_resp->res);
                test_same(static_cast<point3>(*cr->tip()), isect.tip);
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
                    const point2_list start_points {};
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
                test_same(num_hdrs, num_blocks.load(std::memory_order_relaxed));
                test_same(0, num_errs.load(std::memory_order_relaxed));
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
                    const point2_list start_points { from };
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
                test_same(num_hdrs, num_blocks.load(std::memory_order_relaxed));
                test_same(0, num_errs.load(std::memory_order_relaxed));
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
                    client->fetch_blocks(from, to, [&](auto resp) {
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
                                auto blk = std::make_unique<parsed_block>(rv.bytes);
                                num_blocks.fetch_add(1, std::memory_order_relaxed);
                                if (blk->blk->point2() == to) {
                                    iow->io_context().post([&] {
                                        timer->cancel();
                                        iow->io_context().stop();
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
                test_same(10, num_blocks.load(std::memory_order_relaxed));
                test_same(0, num_errs.load(std::memory_order_relaxed));
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
                    client->fetch_blocks(from, to, [&](auto &&resp) {
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
                                        iow->io_context().stop();
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
                                            iow->io_context().stop();
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
                test_same(10, num_blocks.load(std::memory_order_relaxed));
                test_same(0, num_errs.load(std::memory_order_relaxed));
            }
        };
    };
};
