/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <condition_variable>

#ifdef _MSC_VER
#   include <SDKDDKVer.h>
#endif
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <dt/cardano/network/miniprotocol/handshake/messages.hpp>
#include <dt/cardano/network/miniprotocol/blockfetch/messages.hpp>
#include <dt/cardano/network/common.hpp>
#include <dt/cardano.hpp>
#include <dt/cbor/encoder.hpp>
#include <dt/cbor/zero2.hpp>
#include <dt/logger.hpp>
#include <dt/mutex.hpp>
#include <dt/scheduler.hpp>

namespace daedalus_turbo::cardano::network {
    using boost::asio::ip::tcp;

    void client::fetch_blocks(const point2 &from, const point2 &to, const block_handler &handler)
    {
        _fetch_blocks_impl(from, to, handler);
    }

    struct client_connection::impl {
        impl(const address &addr, const version_config_t &versions, const config &cfg, const asio::worker_ptr &asio_worker):
            _cfg { cfg },
            _version_cfg { versions },
            _addr { addr },
            _protocol_magic { json::value_to<uint64_t>(cfg.byron_genesis.at("protocolConsts").at("protocolMagic"))  },
            _asio_worker { asio_worker }
        {
        }

        ~impl()
        {
            process_impl(nullptr, _asio_worker.get());
            if (_conn) {
                _conn->cancel();
                _conn->close();
            }
        }

        void find_intersection_impl(const point2_list &points, const find_handler &handler)
        {
            std::scoped_lock lk { _futures_mutex };
            _futures.emplace_back(boost::asio::co_spawn(_asio_worker->io_context(), _find_intersection(points, handler), boost::asio::use_future));
        }

        void fetch_headers_impl(const point2_list &points, const size_t max_blocks, const header_handler &handler)
        {
            std::scoped_lock lk { _futures_mutex };
            _futures.emplace_back(boost::asio::co_spawn(_asio_worker->io_context(), _fetch_headers(points, max_blocks, handler), boost::asio::use_future));
        }

        void fetch_blocks_impl(const point2 &from, const point2 &to, const block_handler &handler)
        {
            std::scoped_lock lk { _futures_mutex };
            _futures.emplace_back(boost::asio::co_spawn(_asio_worker->io_context(), _fetch_blocks(from, to, handler), boost::asio::use_future));
        }

        void process_impl(scheduler *sched, asio::worker *iow)
        {
            static constexpr std::chrono::milliseconds wait_period { 100 };
            std::scoped_lock lk { _futures_mutex };
            for (const auto &f: _futures) {
                while (f.wait_for(std::chrono::milliseconds { 0 }) != std::future_status::ready) {
                    if (sched)
                        sched->process_once();
                    if (iow)
                        iow->io_context().run_for(wait_period);
                }
            }
            _futures.clear();
        }

        void reset_impl()
        {
            std::scoped_lock lk { _futures_mutex };
            if (!_futures.empty()) [[unlikely]]
                throw error(fmt::format("a client instances can be reset only when there are no active requests but there are: {}", _futures.size()));
            _conn.reset();
        }
    private:
        struct perf_stats {
            std::atomic<std::chrono::system_clock::time_point> last_report_time = std::chrono::system_clock::now();
            std::atomic_size_t bytes = 0;

            void report(asio::worker &asio_w, const size_t bytes_downloaded)
            {
                const auto new_bytes = bytes.fetch_add(bytes_downloaded, std::memory_order_relaxed) + bytes_downloaded;
                for (;;) {
                    const auto now = std::chrono::system_clock::now();
                    auto prev_time = last_report_time.load(std::memory_order_relaxed);
                    if (prev_time + std::chrono::seconds { 5 } > now)
                        break;
                    if (last_report_time.compare_exchange_strong(prev_time, now, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        const double duration = std::chrono::duration_cast<std::chrono::duration<double>>(now - prev_time).count();
                        asio_w.internet_speed_report(static_cast<double>(new_bytes) * 8 / 1'000'000 / duration);
                        bytes.fetch_sub(new_bytes, std::memory_order_relaxed);
                        break;
                    }
                }
            }
        };

        const config &_cfg;
        const version_config_t _version_cfg;
        const address _addr;
        const uint64_t _protocol_magic;
        asio::worker_ptr _asio_worker;
        tcp::resolver _resolver { _asio_worker->io_context() };
        std::optional<tcp::socket> _conn {};
        perf_stats _stats {};
        std::mutex _futures_mutex alignas(mutex::alignment) {};
        std::vector<std::future<void>> _futures {};

        static boost::asio::awaitable<uint8_vector> _read_response(tcp::socket &socket, const mini_protocol mp_id)
        {
            segment_info recv_info {};
            co_await _wait_with_deadline(boost::asio::async_read(socket, boost::asio::buffer(&recv_info, sizeof(recv_info)), boost::asio::use_awaitable));
            uint8_vector recv_payload(recv_info.payload_size());
            co_await _wait_with_deadline(boost::asio::async_read(socket, boost::asio::buffer(recv_payload.data(), recv_payload.size()), boost::asio::use_awaitable));
            if (recv_info.mode() != channel_mode::responder || recv_info.mini_protocol_id() != mp_id) {
                logger::error("unexpected message: mode: {} mini_protocol_id: {} body size: {} body: {}",
                    static_cast<int>(recv_info.mode()), static_cast<uint16_t>(recv_info.mini_protocol_id()), recv_payload.size(),
                    cbor::zero2::parse(recv_payload).get().to_string());
                throw error(fmt::format("unexpected message: mode: {} protocol_id: {}", static_cast<int>(recv_info.mode()), static_cast<uint16_t>(recv_info.mini_protocol_id())));
            }
            co_return recv_payload;
        }

        static boost::asio::awaitable<uint8_vector> _send_request(tcp::socket &socket, const mini_protocol mp_id, const buffer &data)
        {
            if (data.size() >= (1 << 16))
                throw error(fmt::format("payload is larger than allowed: {}!", data.size()));
            uint8_vector segment {};
            auto epoch_time = std::chrono::system_clock::now().time_since_epoch();
            auto micros = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(epoch_time).count());
            segment_info send_info { micros, channel_mode::initiator, mp_id, static_cast<uint16_t>(data.size()) };
            segment << buffer::from(send_info);
            segment << data;
            co_await _wait_with_deadline(async_write(socket, boost::asio::const_buffer { segment.data(), segment.size() }, boost::asio::use_awaitable));
            co_return co_await _read_response(socket, mp_id);
        }

        boost::asio::awaitable<tcp::socket> _connect_and_handshake()
        {
            auto results = co_await _wait_with_deadline(_resolver.async_resolve(_addr.host, _addr.port, boost::asio::use_awaitable));
            if (results.empty())
                throw error(fmt::format("DNS resolve for {}:{} returned no results!", _addr.host, _addr.port));
            tcp::socket socket { _asio_worker->io_context() };
            co_await _wait_with_deadline(socket.async_connect(*results.begin(), boost::asio::use_awaitable));
            if (!socket.is_open()) [[likely]] {
                throw error(fmt::format("failed to connect to {} within the allotted timeframe", _addr));
            }

            cbor::encoder enc {};
            miniprotocol::handshake::version_map versions {};
            for (auto mv = _version_cfg.min; mv <= _version_cfg.max; ++mv) {
                versions.try_emplace(mv, miniprotocol::handshake::node_to_node_version_data_t {
                        numeric_cast<uint32_t>(_protocol_magic), true, false, false });
            }
            miniprotocol::handshake::msg_propose_versions_t {
                std::move(versions)
            }.to_cbor(enc);
            auto resp = co_await _send_request(socket, mini_protocol::handshake, enc.cbor());
            auto resp_cbor = cbor::zero2::parse(resp);
            const auto msg = miniprotocol::handshake::msg_t::from_cbor(resp_cbor.get());
            std::visit([&](const auto &mv) {
                using T = std::decay_t<decltype(mv)>;
                if constexpr (std::is_same_v<T, miniprotocol::handshake::msg_accept_version_t>) {
                    if (mv.version < _version_cfg.min || mv.version > _version_cfg.max)
                        throw error(fmt::format("peer at {}:{} ignored the requested protocol version range and returned {}!", _addr.host, _addr.port, mv.version));
                } else {
                    throw error(fmt::format("peer at {}:{} refused the requested protocol versions!", _addr.host, _addr.port));
                }
            }, msg);
            co_return socket;
        }

        boost::asio::awaitable<intersection_info_t>
        _find_intersection_do(const point2_list &points)
        {
            if (!_conn)
                _conn = co_await _connect_and_handshake();
            intersection_info_t isect {};
            cbor::encoder enc {};
            enc.array(2).uint(4).array(points.size());
            for (const auto &p: points) {
                enc.array(2).uint(p.slot).bytes(p.hash);
            }
            const auto resp = co_await _send_request(*_conn, mini_protocol::chain_sync, enc.cbor());
            auto resp_cbor = cbor::zero2::parse(resp);
            auto &resp_arr = resp_cbor.get().array();
            switch (const auto typ = resp_arr.read().uint(); typ) {
                case 5: {
                    isect.isect = point2::from_cbor(resp_arr.read());
                    isect.tip = point3::from_cbor(resp_arr.read());
                    break;
                }
                case 6: {
                    isect.tip = point3::from_cbor(resp_arr.read());
                    break;
                }
                default:
                    throw error(fmt::format("unexpected chain_sync message: {}!", typ));
            }
            co_return isect;
        }

        boost::asio::awaitable<void> _find_intersection(const point2_list points, const find_handler handler)
        {
            try {
                auto isect = co_await _find_intersection_do(points);
                handler(find_response { _addr, std::move(isect) });
            } catch (const std::exception &ex) {
                handler(find_response { _addr, fmt::format("query_tip error: {}", ex.what()) });
                _conn.reset();
            } catch (...) {
                handler(find_response { _addr, "query_tip unknown error!" });
                _conn.reset();
            }
        }

        struct timer_stopped_t {};
        static boost::asio::awaitable<timer_stopped_t> _wait_for_timer(const std::chrono::seconds deadline)
        {
            auto executor = co_await boost::asio::this_coro::executor;
            auto timer = boost::asio::steady_timer { executor, deadline };
            co_await timer.async_wait(boost::asio::use_awaitable);
            co_return timer_stopped_t {};
        }

        template<typename T>
        static boost::asio::awaitable<T> _wait_with_deadline(boost::asio::awaitable<T> action, const std::chrono::seconds deadline=std::chrono::seconds { 5 })
        {
            using namespace boost::asio::experimental::awaitable_operators;
            auto res = co_await (std::move(action) || _wait_for_timer(deadline));
            if (std::holds_alternative<timer_stopped_t>(res)) [[unlikely]]
                throw error("blockfetch: failed to receive the next block within the allotted timeframe from the peer");
            if constexpr (!std::is_same_v<T, void>)
                co_return std::move(std::get<T>(res));
        }

        static boost::asio::awaitable<void> _receive_blocks(tcp::socket &socket, uint8_vector parse_buf, const block_handler &handler)
        {
            for (;;) {
                while (!parse_buf.empty()) {
                    try {
                        auto resp_cbor = cbor::zero2::parse(parse_buf);
                        auto msg = miniprotocol::blockfetch::msg_t::from_cbor(resp_cbor.get());
                        const auto go_on = std::visit([&](auto &&mv) -> bool {
                            using T = std::decay_t<decltype(mv)>;
                            if constexpr (std::is_same_v<T, miniprotocol::blockfetch::msg_block_t>) {
                                if (!handler(block_response_t { std::move(mv) }))
                                    return false;
                            } else if constexpr (std::is_same_v<T, miniprotocol::blockfetch::msg_compressed_blocks_t>) {
                                if (!handler(block_response_t { std::move(mv) }))
                                    return false;
                            } else if constexpr (std::is_same_v<T, miniprotocol::blockfetch::msg_batch_done_t>) {
                                return false;
                            } else {
                                throw error(fmt::format("unexpected blockfetch message: {}!", msg.index()));
                            }
                            return true;
                        }, std::move(msg));
                        if (!go_on)
                            co_return;
                        parse_buf.erase(parse_buf.begin(), parse_buf.begin() + resp_cbor.get().data_raw().size());
                    } catch (const cbor::zero2::incomplete_error &) {
                        // exit the while loop and wait for more data
                        break;
                    }
                }
                parse_buf << co_await _wait_with_deadline(_read_response(socket, mini_protocol::block_fetch), std::chrono::seconds { 5 });
            }
        }

        // block_handler must be a copy so that the handler is owned by the coroutine!
        boost::asio::awaitable<void> _fetch_blocks(const point2 from, const point2 to, const block_handler handler)
        {
            try {
                if (!_conn)
                    _conn = co_await _connect_and_handshake();
                cbor::encoder enc {};
                enc.array(3).uint(0);
                from.to_cbor(enc);
                to.to_cbor(enc);
                auto resp = co_await _send_request(*_conn, mini_protocol::block_fetch, enc.cbor());
                auto resp_cbor = cbor::zero2::parse(resp);
                auto &resp_items = resp_cbor.get().array();
                switch (const auto typ = resp_items.read().uint(); typ) {
                    case 2: {
                        resp.erase(resp.begin(), resp.begin() + resp_cbor.get().data_raw().size());
                        co_await _receive_blocks(*_conn, std::move(resp), [&](block_response_t blk) {
                            std::visit([&](const auto &rv) {
                                using T = std::decay_t<decltype(rv)>;
                                if constexpr (std::is_same_v<T, miniprotocol::blockfetch::msg_block_t>) {
                                    _stats.report(*_asio_worker, rv.bytes.size());
                                } else if constexpr (std::is_same_v<T, miniprotocol::blockfetch::msg_compressed_blocks_t>) {
                                    _stats.report(*_asio_worker, rv.payload.size());
                                }
                            }, blk);
                            return handler(std::move(blk));
                        });
                        break;
                    }
                    case 3: {
                        handler(block_response_t { "fetch_blocks do not have all requested blocks!" });
                        break;
                    }
                    default:
                        throw error(fmt::format("unexpected chain_sync message: {}!", typ));
                }
            } catch (const std::exception &ex) {
                handler(block_response_t { fmt::format("fetch_blocks error: {}", ex.what()) });
                _conn.reset();
            } catch (...) {
                handler(block_response_t { "fetch_blocks unknown error!" });
                _conn.reset();
            }
        }

        static point _decode_point_2(cbor::zero2::array_reader &it)
        {
            const auto pnt_slot = it.read().uint();
            return { it.read().bytes(), pnt_slot };
        }

        static point _decode_point_2(cbor::zero2::value &v)
        {
            return _decode_point_2(v.array());
        }

        static point _decode_point_3(cbor::zero2::value &v)
        {
            auto &it = v.array();
            auto p = _decode_point_2(it.read());
            p.height = it.read().uint();
            return p;
        }

        static std::optional<point> _decode_intersect(cbor::zero2::value &v)
        {
            if (v.indefinite() || v.special_uint() > 0)
                return _decode_point_2(v);
            return {};
        }

        boost::asio::awaitable<void> _fetch_headers(const point2_list points, const size_t max_blocks, const header_handler handler)
        {
            try {
                header_list headers {};
                auto isect = co_await _find_intersection_do(points);
                cbor::encoder msg_req_next {};
                msg_req_next.array(1).uint(0);
                while (headers.size() < max_blocks) {
                    auto parse_buf = co_await _send_request(*_conn, mini_protocol::chain_sync, msg_req_next.cbor());
                    auto resp_cbor = cbor::zero2::parse(parse_buf);
                    auto &resp_it = resp_cbor.get().array();
                    const auto typ = resp_it.read().uint();
                    // MsgAwaitReply
                    if (typ == 1)
                        break;
                    if (typ == 3) {
                        auto intersect = _decode_intersect(resp_it.read());
                        isect.tip = _decode_point_3(resp_it.read());
                        if (isect.isect == intersect)
                            continue;
                        break;
                    }
                    if (typ != 2) // !MsgRollForward
                        throw error(fmt::format("unexpected chain_sync message: {}!", typ));
                    {
                        const auto hdr = parsed_header::from_cbor(resp_it.read(), _cfg);
                        headers.emplace_back(hdr->slot(), hdr->hash());
                    }
                    isect.tip = _decode_point_3(resp_it.read());
                    if (headers.back().hash == isect.tip.hash)
                        break;
                }
                handler(header_response { _addr, isect.isect, isect.tip, std::move(headers) });
            } catch (const std::exception &ex) {
                handler(header_response { .addr=_addr, .res=fmt::format("fetch_headers error: {}", ex.what()) });
                _conn.reset();
            } catch (...) {
                handler(header_response { .addr=_addr, .res="fetch_headers unknown error!" });
                _conn.reset();
            }
        }
    };

    client_connection::client_connection(const address &addr, const version_config_t &versions, const cardano::config &cfg, const asio::worker_ptr &asio_worker)
        : client { addr }, _impl { std::make_unique<impl>(addr, versions, cfg, asio_worker) }
    {
    }

    client_connection::~client_connection() =default;

    void client_connection::_find_intersection_impl(const point2_list &points, const find_handler &handler)
    {
        _impl->find_intersection_impl(points, handler);
    }

    void client_connection::_fetch_headers_impl(const point2_list &points, const size_t max_blocks, const header_handler &handler)
    {
        _impl->fetch_headers_impl(points, max_blocks, handler);
    }

    void client_connection::_fetch_blocks_impl(const point2 &from, const point2 &to, const block_handler &handler)
    {
        _impl->fetch_blocks_impl(from, to, handler);
    }

    void client_connection::_process_impl(scheduler *sched, asio::worker *iow)
    {
        _impl->process_impl(sched, iow);
    }

    void client_connection::_reset_impl()
    {
        _impl->reset_impl();
    }

    client_manager_async &client_manager_async::get()
    {
        static client_manager_async m {};
        return m;
    }

    std::unique_ptr<client> client_manager_async::_connect_impl(const address &addr, const version_config_t &versions, const cardano::config &cfg, const asio::worker_ptr &asio_worker)
    {
        return std::make_unique<client_connection>(addr, versions, cfg, asio_worker);
    }
}