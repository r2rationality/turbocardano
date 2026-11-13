/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <exception>

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <turbo/common/mutex.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/scope-exit.hpp>
#include <turbo/cardano.hpp>
#include <turbo/cardano/network/common.hpp>
#include <turbo/cardano/network/message-buffer.hpp>
#include <turbo/cardano/network/miniprotocol/blockfetch/messages.hpp>
#include <turbo/cardano/network/miniprotocol/chainsync/messages.hpp>
#include <turbo/cardano/network/miniprotocol/handshake/messages.hpp>
#include <turbo/cbor/encoder.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/common/logger.hpp>

namespace turbo::cardano::network {
    using boost::asio::ip::tcp;
    static constexpr auto response_timeout = std::chrono::seconds { 10 };

    void client::fetch_blocks(const point2 &from, const point2 &to, const block_handler &handler)
    {
        _fetch_blocks_impl(from, to, handler);
    }

    struct client_connection::impl {
        impl(const address &addr, const version_config_t &versions, const config &cfg, const asio::worker_ptr &asio_worker):
            _cfg { cfg },
            _version_cfg { versions },
            _addr { addr },
            _protocol_magic { cfg.byron_protocol_magic },
            _asio_worker { asio_worker }
        {
        }

        auto acquire_request()
        {
            if (_request_active.exchange(true, std::memory_order_acquire)) [[unlikely]]
                throw error{"a client request is already in progress"};
            return scope_exit { [this] { _request_active.store(false, std::memory_order_release); } };
        }

        ~impl()
        {
            logger::run_log_errors([&] { process_impl(nullptr, _asio_worker.get()); });
        }

        void find_intersection_impl(const optional_point2_list &points, const find_handler &handler)
        {
            _start_request(_find_intersection(points, handler));
        }

        void fetch_headers_impl(const optional_point2_list &points, const size_t max_blocks, const header_handler &handler)
        {
            _start_request(_fetch_headers(points, max_blocks, handler));
        }

        void fetch_blocks_impl(const point2 &from, const point2 &to, const block_handler &handler)
        {
            logger::debug("fetch_blocks from: {} to: {}", from, to);
            _start_request(_fetch_blocks(from, to, handler));
        }

        chain_update next_header_sync(const std::stop_token stop, const std::function<void()> &idle)
        {
            auto active_request = acquire_request();
            std::stop_source request;
            std::stop_callback forward_stop { stop, [&] { request.request_stop(); } };
            auto f = boost::asio::co_spawn(_strand, _next_header(request.get_token()), boost::asio::use_future);
            std::exception_ptr idle_failure;
            while (f.wait_for(std::chrono::milliseconds { 100 }) != std::future_status::ready) {
                if (idle_failure) {
                    _asio_worker->io_context().poll();
                } else if (idle) {
                    try {
                        idle();
                    } catch (...) {
                        idle_failure = std::current_exception();
                        request.request_stop();
                    }
                }
            }
            if (idle_failure) {
                _conn.reset();
                std::rethrow_exception(idle_failure);
            }
            _check_stop(stop);
            _check_stop(_stop);
            try {
                return f.get();
            } catch (const boost::system::system_error &ex) {
                _rethrow_transport_error(ex);
            }
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
            std::exception_ptr failure;
            for (auto &f: _futures) {
                try {
                    f.get();
                } catch (...) {
                    if (!failure)
                        failure = std::current_exception();
                }
            }
            _futures.clear();
            if (failure)
                std::rethrow_exception(failure);
        }

        void reset_impl()
        {
            auto active_request = acquire_request();
            std::scoped_lock lk { _futures_mutex };
            if (!_futures.empty()) [[unlikely]]
                throw error{"a client instances can be reset only when there are no active requests but there are: {}", _futures.size()};
            _conn.reset();
        }

        void set_stop_token(const std::stop_token stop)
        {
            auto active_request = acquire_request();
            _stop = stop;
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
        boost::asio::strand<boost::asio::io_context::executor_type> _strand { boost::asio::make_strand(_asio_worker->io_context()) };
        std::shared_ptr<tcp::resolver> _resolver = std::make_shared<tcp::resolver>(_strand);
        std::shared_ptr<tcp::socket> _conn {};
        std::map<mini_protocol, message_buffer_t> _responses {};
        size_t _block_message_limit = 2'500'000;
        std::stop_token _stop {};
        std::atomic_bool _request_active { false };
        perf_stats _stats {};
        std::mutex _futures_mutex alignas(mutex::alignment) {};
        std::vector<std::future<void>> _futures {};

        void _start_request(boost::asio::awaitable<void> action)
        {
            auto guard = acquire_request();
            std::scoped_lock lk { _futures_mutex };
            if (_futures.size() == _futures.capacity())
                _futures.reserve(std::max(size_t { 1 }, _futures.capacity() * 2));
            const auto run = [](auto guard, boost::asio::awaitable<void> action) -> boost::asio::awaitable<void> {
                auto active_request = std::move(guard);
                co_await std::move(action);
            };
            _futures.emplace_back(boost::asio::co_spawn(_strand,
                run(std::move(guard), std::move(action)), boost::asio::use_future));
        }

        // Queued cancellation owns its targets even if the operation finishes first.
        static auto _cancel_on_stop(const std::stop_token stop, const std::shared_ptr<tcp::socket> &socket,
            std::shared_ptr<tcp::resolver> resolver={})
        {
            return std::stop_callback { stop, [executor=socket->get_executor(), socket, resolver] {
                boost::asio::post(executor, [socket, resolver] {
                    if (resolver) resolver->cancel();
                    boost::system::error_code ec;
                    socket->close(ec);
                });
            } };
        }

        static void _check_stop(const std::stop_token stop)
        {
            if (stop.stop_requested())
                throw error{"network operation stopped"};
        }

        std::exception_ptr _current_request_error() const
        {
            if (_stop.stop_requested())
                return std::make_exception_ptr(error{"network operation stopped"});
            return std::current_exception();
        }

        [[noreturn]] static void _rethrow_transport_error(const boost::system::system_error &ex)
        {
            using namespace boost::asio::error;
            const auto &code = ex.code();
            if (code == eof || code == connection_reset || code == connection_aborted
                    || code == connection_refused || code == broken_pipe || code == timed_out
                    || code == network_down || code == network_reset || code == network_unreachable
                    || code == host_unreachable || code == host_not_found_try_again || code == try_again
                    || code == would_block || code == interrupted || code == not_connected || code == shut_down)
                throw transient_error{ex.what()};
            throw;
        }

        message_buffer_t &_response_buffer(const mini_protocol mp_id)
        {
            const auto limit = mp_id == mini_protocol::block_fetch ? _block_message_limit
                : mp_id == mini_protocol::keep_alive ? size_t { 32 } : message_buffer_t::max_segment_size;
            return _responses.try_emplace(mp_id, limit).first->second;
        }

        boost::asio::awaitable<buffer> _read_response(tcp::socket &socket, const mini_protocol mp_id,
            const size_t limit=message_buffer_t::max_segment_size)
        {
            auto &messages = _response_buffer(mp_id);
            for (;;) {
                if (const auto message = messages.next(limit))
                    co_return *message;
                const auto deadline = std::chrono::steady_clock::now() + response_timeout;
                segment_info recv_info {};
                co_await _wait_with_deadline(boost::asio::async_read(socket, boost::asio::buffer(&recv_info, sizeof(recv_info)), boost::asio::use_awaitable),
                    deadline - std::chrono::steady_clock::now());
                if (recv_info.mode() != channel_mode::responder || recv_info.mini_protocol_id() != mp_id) [[unlikely]]
                    throw error{"unexpected message: mode: {} protocol_id: {}", static_cast<int>(recv_info.mode()), static_cast<uint16_t>(recv_info.mini_protocol_id())};
                const auto recv_payload = messages.prepare(recv_info.payload_size());
                co_await _wait_with_deadline(boost::asio::async_read(socket, boost::asio::buffer(recv_payload.data(), recv_payload.size()), boost::asio::use_awaitable),
                    deadline - std::chrono::steady_clock::now());
                if (mp_id == mini_protocol::handshake)
                    message_buffer_t::check_handshake(recv_payload);
                messages.commit(recv_payload.size());
            }
        }

        static boost::asio::awaitable<void> _write_request(tcp::socket &socket, const mini_protocol mp_id, const buffer &data)
        {
            if (data.size() >= (1 << 16)) [[unlikely]]
                throw error{"payload is larger than allowed: {}!", data.size()};
            uint8_vector segment {};
            auto epoch_time = std::chrono::system_clock::now().time_since_epoch();
            auto micros = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(epoch_time).count());
            segment_info send_info { micros, channel_mode::initiator, mp_id, static_cast<uint16_t>(data.size()) };
            segment << buffer::from(send_info);
            segment << data;
            co_await _wait_with_deadline(async_write(socket, boost::asio::const_buffer { segment.data(), segment.size() }, boost::asio::use_awaitable));
        }

        boost::asio::awaitable<buffer> _send_request(tcp::socket &socket, const mini_protocol mp_id, const buffer &data)
        {
            co_await _write_request(socket, mp_id, data);
            co_return co_await _read_response(socket, mp_id);
        }

        boost::asio::awaitable<std::shared_ptr<tcp::socket>> _connect_and_handshake()
        {
            _check_stop(_stop);
            _responses.clear();
            auto socket = std::make_shared<tcp::socket>(_strand);
            auto cancel = _cancel_on_stop(_stop, socket, _resolver);
            auto results = co_await _wait_with_deadline(_resolver->async_resolve(_addr.host, _addr.port, boost::asio::use_awaitable));
            _check_stop(_stop);
            if (results.empty()) [[unlikely]]
                throw error{"DNS resolve for {}:{} returned no results!", _addr.host, _addr.port};
            co_await _wait_with_deadline(socket->async_connect(*results.begin(), boost::asio::use_awaitable));
            _check_stop(_stop);
            if (!socket->is_open()) [[unlikely]] {
                throw transient_error{"failed to connect to {} within the allotted timeframe", _addr};
            }

            cbor::encoder enc {};
            miniprotocol::handshake::version_map versions {};
            for (const uint64_t mv: { 14, 999 }) {
                if (mv < _version_cfg.min || mv > _version_cfg.max)
                    continue;
                versions.try_emplace(mv, miniprotocol::handshake::node_to_node_version_data_t {
                        numeric_cast<uint32_t>(_protocol_magic), true, false, false });
            }
            miniprotocol::handshake::msg_propose_versions_t {
                std::move(versions)
            }.to_cbor(enc);
            auto resp = co_await _send_request(*socket, mini_protocol::handshake, enc.cbor());
            auto resp_cbor = cbor::zero2::parse(resp);
            const auto msg = miniprotocol::handshake::msg_t::from_cbor(resp_cbor.get());
            std::visit([&](const auto &mv) {
                using T = std::decay_t<decltype(mv)>;
                if constexpr (std::is_same_v<T, miniprotocol::handshake::msg_accept_version_t>) {
                    if (mv.config.network_magic != _protocol_magic) [[unlikely]]
                        throw error{"peer at {}:{} accepted a different network magic", _addr.host, _addr.port};
                    if ((mv.version != 14 && mv.version != 999)
                            || mv.version < _version_cfg.min || mv.version > _version_cfg.max) [[unlikely]]
                        throw error{"peer at {}:{} ignored the requested protocol version range and returned {}!", _addr.host, _addr.port, mv.version};
                    logger::info("outbound peer {}: negotiated protocol version {}", _addr, mv.version);
                    _block_message_limit = mv.version == 999 ? _version_cfg.max_compressed_message_size : 2'500'000;
                } else {
                    throw error{"peer at {}:{} refused the requested protocol versions!", _addr.host, _addr.port};
                }
            }, msg);
            co_return socket;
        }

        boost::asio::awaitable<intersection_info_t>
        _find_intersection_do(optional_point2_list points)
        {
            _check_stop(_stop);
            if (!_conn)
                _conn = co_await _connect_and_handshake();
            auto cancel = _cancel_on_stop(_stop, _conn);
            intersection_info_t isect {};
            cbor::encoder enc {};
            miniprotocol::chainsync::msg_find_intersect_t { std::move(points) }.to_cbor(enc);
            const auto resp = co_await _send_request(*_conn, mini_protocol::chain_sync, enc.cbor());
            auto resp_cbor = cbor::zero2::parse(resp);
            auto &resp_arr = resp_cbor.get().array();
            switch (const auto typ = resp_arr.read().uint(); typ) {
                case 5: {
                    isect.found = true;
                    isect.isect = optional_point2::from_cbor(resp_arr.read());
                    isect.tip = optional_point3::from_cbor(resp_arr.read());
                    break;
                }
                case 6: {
                    isect.tip = optional_point3::from_cbor(resp_arr.read());
                    break;
                }
                [[unlikely]] default:
                    throw error{"unexpected chain_sync message: {}!", typ};
            }
            co_return isect;
        }

        boost::asio::awaitable<void> _find_intersection(optional_point2_list points, const find_handler handler)
        {
            try {
                auto isect = co_await _find_intersection_do(std::move(points));
                handler(find_response { _addr, std::move(isect) });
            } catch (...) {
                _conn.reset();
                handler(find_response { _addr, _current_request_error() });
            }
        }

        struct timer_stopped_t {};
        static boost::asio::awaitable<timer_stopped_t> _wait_for_timer(const std::chrono::steady_clock::duration deadline)
        {
            auto executor = co_await boost::asio::this_coro::executor;
            auto timer = boost::asio::steady_timer { executor, deadline };
            co_await timer.async_wait(boost::asio::use_awaitable);
            co_return timer_stopped_t {};
        }

        template<typename T>
        static boost::asio::awaitable<T> _wait_with_deadline(boost::asio::awaitable<T> action, const std::chrono::steady_clock::duration deadline=response_timeout)
        {
            using namespace boost::asio::experimental::awaitable_operators;
            if (deadline <= std::chrono::steady_clock::duration::zero())
                throw transient_error{"network response deadline expired"};
            std::exception_ptr failure;
            std::optional<std::conditional_t<std::is_void_v<T>, std::monostate, T>> result;
            const auto complete = [&]() -> boost::asio::awaitable<void> {
                try {
                    if constexpr (std::is_void_v<T>) {
                        co_await std::move(action);
                        result.emplace();
                    } else {
                        result.emplace(co_await std::move(action));
                    }
                } catch (...) {
                    failure = std::current_exception();
                }
            };
            auto res = co_await (complete() || _wait_for_timer(deadline));
            if (std::holds_alternative<timer_stopped_t>(res)) [[unlikely]]
                throw transient_error{"network operation timed out after {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(deadline).count()};
            if (failure) {
                try {
                    std::rethrow_exception(failure);
                } catch (const boost::system::system_error &ex) {
                    _rethrow_transport_error(ex);
                }
            }
            if constexpr (!std::is_same_v<T, void>)
                co_return std::move(*result);
        }

        // True means BatchDone was received; an interrupted stream cannot be reused.
        boost::asio::awaitable<bool> _receive_blocks(tcp::socket &socket, const block_handler &handler)
        {
            bool batch_done = false;
            for (;;) {
                _check_stop(_stop);
                const auto bytes = co_await _read_response(socket, mini_protocol::block_fetch, _block_message_limit);
                auto resp_cbor = cbor::zero2::parse(bytes);
                auto msg = miniprotocol::blockfetch::msg_t::from_cbor(resp_cbor.get());
                const auto go_on = std::visit([&](auto &&mv) -> bool {
                    using T = std::decay_t<decltype(mv)>;
                    if constexpr (std::is_same_v<T, miniprotocol::blockfetch::msg_block_t>
                            || std::is_same_v<T, miniprotocol::blockfetch::msg_compressed_blocks_t>) {
                        return handler(block_response_t { std::move(mv) });
                    } else if constexpr (std::is_same_v<T, miniprotocol::blockfetch::msg_batch_done_t>) {
                        batch_done = true;
                        return false;
                    } else {
                        throw error{"unexpected blockfetch message: {}!", msg.index()};
                    }
                }, std::move(msg));
                if (!go_on)
                    co_return batch_done;
            }
        }

        // block_handler must be a copy so that the handler is owned by the coroutine!
        boost::asio::awaitable<void> _fetch_blocks(const point2 from, const point2 to, const block_handler handler)
        {
            try {
                _check_stop(_stop);
                if (!_conn)
                    _conn = co_await _connect_and_handshake();
                auto cancel = _cancel_on_stop(_stop, _conn);
                cbor::encoder enc {};
                miniprotocol::blockfetch::msg_request_range_t { from, to }.to_cbor(enc);
                auto resp = co_await _send_request(*_conn, mini_protocol::block_fetch, enc.cbor());
                switch (const auto typ = cbor::zero2::parse(resp).get().array().read().uint(); typ) {
                    case 2: {
                        const auto completed = co_await _receive_blocks(*_conn, [&](block_response_t blk) {
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
                        if (!completed) {
                            logger::debug("block fetch interrupted; closing connection before BatchDone");
                            _conn.reset();
                        }
                        break;
                    }
                    case 3: {
                        throw transient_error{"fetch_blocks do not have all requested blocks!"};
                    }
                    [[unlikely]] default:
                        throw error{"unexpected chain_sync message: {}!", typ};
                }
            } catch (...) {
                _conn.reset();
                handler(block_response_t { _current_request_error() });
            }
        }

        // One operation owns the socket. KeepAlive may interleave with the
        // delayed ChainSync response, but no second coroutine reads the stream.
        boost::asio::awaitable<chain_update> _next_header(const std::stop_token stop)
        {
            _check_stop(stop);
            if (!_conn)
                throw error{"ChainSync requires an established intersection"};
            auto cancel = _cancel_on_stop(stop, _conn);
            cbor::encoder req {};
            miniprotocol::chainsync::msg_request_next_t {}.to_cbor(req);
            co_await _write_request(*_conn, mini_protocol::chain_sync, req.cbor());
            auto &pending = _response_buffer(mini_protocol::chain_sync);
            auto &keepalive = _response_buffer(mini_protocol::keep_alive);
            bool awaiting = false;
            std::optional<chain_update> result;
            bool keepalive_pending = false;
            auto last_keepalive = std::chrono::steady_clock::now();
            const auto request_started = last_keepalive;
            for (;;) {
                if (stop.stop_requested())
                    throw error{"ChainSync stopped"};
                while (const auto message = pending.next()) {
                    if (result) [[unlikely]]
                        throw error{"unsolicited ChainSync response"};
                    auto parsed = cbor::zero2::parse(*message);
                    auto &items = parsed.get().array();
                    switch (items.read().uint()) {
                        case 1:
                            if (awaiting) [[unlikely]]
                                throw error{"duplicate AwaitReply"};
                            awaiting = true;
                            break;
                        case 2: {
                            const auto header = parsed_header::from_cbor(items.read(), _cfg);
                            auto tip = optional_point3::from_cbor(items.read());
                            if (!tip) [[unlikely]]
                                throw error{"RollForward with a tip at origin"};
                            result = chain_update { false, point2 { header->slot(), header->hash() }, std::move(tip) };
                            break;
                        }
                        case 3: {
                            const auto target = optional_point2::from_cbor(items.read());
                            result = chain_update { true, target, optional_point3::from_cbor(items.read()) };
                            break;
                        }
                        default:
                            throw error{"unexpected persistent ChainSync response"};
                    }
                }
                while (const auto message = keepalive.next()) {
                    auto response = cbor::zero2::parse(*message);
                    auto &items = response.get().array();
                    if (!keepalive_pending || items.read().uint() != 1 || items.read().uint() != 0) [[unlikely]]
                        throw error{"unexpected KeepAlive response"};
                    keepalive_pending = false;
                }
                if (result && !keepalive_pending)
                    co_return *result;
                const auto now = std::chrono::steady_clock::now();
                if (!awaiting && now - request_started >= response_timeout)
                    throw transient_error{"ChainSync response timed out"};
                if (keepalive_pending && now - last_keepalive >= response_timeout)
                    throw transient_error{"KeepAlive response timed out"};
                if (!result && awaiting && !keepalive_pending && now - last_keepalive >= response_timeout) {
                    cbor::encoder enc {};
                    enc.array(2).uint(0).uint(0);
                    co_await _write_request(*_conn, mini_protocol::keep_alive, enc.cbor());
                    keepalive_pending = true;
                    last_keepalive = std::chrono::steady_clock::now();
                }
                if (!_conn->available()) {
                    boost::asio::steady_timer timer { co_await boost::asio::this_coro::executor };
                    timer.expires_after(std::chrono::milliseconds { 100 });
                    using namespace boost::asio::experimental::awaitable_operators;
                    const auto ready = co_await (_conn->async_wait(tcp::socket::wait_read, boost::asio::use_awaitable)
                        || timer.async_wait(boost::asio::use_awaitable));
                    if (ready.index() != 0)
                        continue;
                }
                auto deadline = std::chrono::steady_clock::now() + response_timeout;
                if (!awaiting)
                    deadline = std::min(deadline, request_started + response_timeout);
                if (keepalive_pending)
                    deadline = std::min(deadline, last_keepalive + response_timeout);
                segment_info hdr {};
                co_await _wait_with_deadline(boost::asio::async_read(*_conn, boost::asio::buffer(&hdr, sizeof(hdr)), boost::asio::use_awaitable),
                    deadline - std::chrono::steady_clock::now());
                if (hdr.mode() != channel_mode::responder)
                    throw error{"unexpected mux direction"};
                const auto mp_id = hdr.mini_protocol_id();
                if (mp_id != mini_protocol::chain_sync && mp_id != mini_protocol::keep_alive)
                    throw error{"unexpected mini-protocol while following"};
                auto &messages = mp_id == mini_protocol::chain_sync ? pending : keepalive;
                const auto bytes = messages.prepare(hdr.payload_size());
                co_await _wait_with_deadline(boost::asio::async_read(*_conn, boost::asio::buffer(bytes.data(), bytes.size()), boost::asio::use_awaitable),
                    deadline - std::chrono::steady_clock::now());
                messages.commit(bytes.size());
            }
        }

        boost::asio::awaitable<void> _fetch_headers(optional_point2_list points, const size_t max_blocks, const header_handler handler)
        {
            try {
                header_list headers {};
                auto isect = co_await _find_intersection_do(std::move(points));
                auto cancel = _cancel_on_stop(_stop, _conn);
                cbor::encoder msg_req_next {};
                miniprotocol::chainsync::msg_request_next_t {}.to_cbor(msg_req_next);
                while (headers.size() < max_blocks) {
                    _check_stop(_stop);
                    auto parse_buf = co_await _send_request(*_conn, mini_protocol::chain_sync, msg_req_next.cbor());
                    auto resp_cbor = cbor::zero2::parse(parse_buf);
                    auto &resp_it = resp_cbor.get().array();
                    const auto typ = resp_it.read().uint();
                    // MsgAwaitReply
                    if (typ == 1)
                        break;
                    if (typ == 3) {
                        auto intersect = optional_point2::from_cbor(resp_it.read());
                        isect.tip = optional_point3::from_cbor(resp_it.read());
                        if (isect.isect == intersect)
                            continue;
                        break;
                    }
                    if (typ != 2) [[unlikely]] // !MsgRollForward
                        throw error{"unexpected chain_sync message: {}!", typ};
                    {
                        const auto hdr = parsed_header::from_cbor(resp_it.read(), _cfg);
                        headers.emplace_back(hdr->slot(), hdr->hash());
                    }
                    isect.tip = optional_point3::from_cbor(resp_it.read());
                    if (!isect.tip) throw error("RollForward with a tip at origin");
                    if (headers.back().hash == isect.tip->hash)
                        break;
                }
                handler(header_response { _addr, isect.isect, isect.tip, std::move(headers) });
            } catch (...) {
                _conn.reset();
                handler(header_response { .addr=_addr, .res=_current_request_error() });
            }
        }
    };

    client_connection::client_connection(const address &addr, const version_config_t &versions, const cardano::config &cfg, const asio::worker_ptr &asio_worker)
        : client { addr }, _impl { std::make_unique<impl>(addr, versions, cfg, asio_worker) }
    {
    }

    client_connection::~client_connection() =default;

    void client_connection::set_stop_token(const std::stop_token stop)
    {
        _impl->set_stop_token(stop);
    }

    void client_connection::_find_intersection_impl(const optional_point2_list &points, const find_handler &handler)
    {
        _impl->find_intersection_impl(points, handler);
    }

    client::chain_update client_connection::next_header_sync(const std::stop_token stop, const std::function<void()> &idle)
    {
        return _impl->next_header_sync(stop, idle);
    }

    void client_connection::_fetch_headers_impl(const optional_point2_list &points, const size_t max_blocks, const header_handler &handler)
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
