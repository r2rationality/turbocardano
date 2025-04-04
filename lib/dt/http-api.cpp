/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <future>
#ifdef _MSC_VER
#   include <SDKDDKVer.h>
#endif
#ifdef __clang__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#define BOOST_ASIO_HAS_STD_INVOKE_RESULT 1
#ifndef BOOST_ALLOW_DEPRECATED_HEADERS
#   define BOOST_ALLOW_DEPRECATED_HEADERS
#   define DT_CLEAR_BOOST_DEPRECATED_HEADERS
#endif
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#ifdef DT_CLEAR_BOOST_DEPRECATED_HEADERS
#   undef BOOST_ALLOW_DEPRECATED_HEADERS
#   undef DT_CLEAR_BOOST_DEPRECATED_HEADERS
#endif
#ifdef __clang__
#   pragma GCC diagnostic pop
#endif
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <boost/url.hpp>

#include <dt/asio.hpp>
#include <dt/cardano/ledger/state.hpp>
#include <dt/chunk-registry.hpp>
#include <dt/common/bytes.hpp>
#include <dt/common/format.hpp>
#include <dt/json.hpp>
#include <dt/history.hpp>
#include <dt/http/download-queue.hpp>
#include <dt/http-api.hpp>
#include <dt/logger.hpp>
#include <dt/mutex.hpp>
#include <dt/progress.hpp>
#include <dt/requirements.hpp>
#include <dt/sync/hybrid.hpp>
#include <dt/sync/p2p.hpp>
#include <dt/zpp.hpp>

namespace daedalus_turbo::http_api {
    enum class sync_type { none, turbo, p2p, hybrid };
}

namespace fmt {
    template<>
    struct formatter<boost::string_view>: formatter<std::string_view> {
        template<typename FormatContext>
        auto format(const boost::string_view &sv, FormatContext &ctx) const -> decltype(ctx.out()) {
            return formatter<std::string_view>::format(std::string_view { sv.data(), sv.size() }, ctx);
        }
    };

    template<>
    struct formatter<daedalus_turbo::http_api::sync_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::http_api::sync_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            switch (v) {
                case daedalus_turbo::http_api::sync_type::none: return fmt::format_to(ctx.out(), "none");
                case daedalus_turbo::http_api::sync_type::turbo: return fmt::format_to(ctx.out(), "turbo");
                case daedalus_turbo::http_api::sync_type::p2p: return fmt::format_to(ctx.out(), "p2p");
                case daedalus_turbo::http_api::sync_type::hybrid: return fmt::format_to(ctx.out(), "hybrid");
                default: throw daedalus_turbo::error(fmt::format("unsupported sync_type: {}", static_cast<int>(v)));
            }
        }
    };
}

namespace daedalus_turbo::http_api {
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    namespace dt = daedalus_turbo;
    namespace json = boost::json;
    using tcp = boost::asio::ip::tcp;

    struct server::impl {
        impl(const std::string &data_dir, const bool ignore_requirements, scheduler &sched)
            : _data_dir { data_dir }, _sched { sched }, _cache_dir { _data_dir / "history" },
                _ignore_requirements { ignore_requirements }
        {
        }

        void serve(const std::string &ip, const uint16_t port)
        {
            {
                mutex::scoped_lock results_lk { _results_mutex };
                _results.emplace("/sync/", std::optional<json::value> {});
            }
            {
                mutex::unique_lock queue_lk { _queue_mutex };
                _queue.emplace_back("/sync/");
                queue_lk.unlock();
                _queue_cv.notify_one();
            }
            auto &ioc = asio::worker::get().io_context();
            net::spawn(ioc, std::bind(&impl::_do_listen, std::ref(*this), std::ref(ioc),
                tcp::endpoint { net::ip::make_address(ip), port }, std::placeholders::_1));
            _worker_thread();
        }
    private:
        enum class sync_status { syncing, ready, failed };

        struct send_lambda {
            beast::tcp_stream& stream_;
            bool& close_;
            beast::error_code& ec_;
            net::yield_context yield_;

            send_lambda(beast::tcp_stream& stream, bool& close, beast::error_code& ec, net::yield_context yield)
                : stream_(stream), close_(close), ec_(ec), yield_(yield)
            {
            }

            template<bool isRequest, class Body, class Fields>
            void operator()(http::message<isRequest, Body, Fields>&& msg) const
            {
                close_ = msg.need_eof();
#if defined(__GNUC__) || defined(__clang__)
#               pragma GCC diagnostic push
#               pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
                http::serializer<isRequest, Body, Fields> sr{msg};
#if defined(__GNUC__) || defined(__clang__)
#               pragma GCC diagnostic pop
#endif
                http::async_write(stream_, sr, yield_[ec_]);
            }
        };

        const std::filesystem::path _data_dir;
        scheduler &_sched;
        std::filesystem::path _cache_dir;
        const bool _ignore_requirements;
        std::unique_ptr<chunk_registry> _cr {};
        std::unique_ptr<reconstructor> _reconst {};
        std::chrono::time_point<std::chrono::system_clock> _sync_start {};
        std::atomic<std::optional<uint64_t>> _sync_start_slot {};
        std::atomic<std::optional<uint64_t>> _sync_target_slot {};
        double _sync_duration {};
        double _sync_data_mb = 0;
        std::optional<chunk_registry::chunk_info> _sync_last_chunk {};
        std::shared_ptr<std::string> _sync_last_error {};

        sync_type _sync_type { sync_type::none };
        sync::validation_mode_t _validation_mode { sync::validation_mode_t::turbo };
        std::atomic<sync_status> _sync_status { sync_status::syncing };
        mutex::unique_lock::mutex_type _requirements_mutex alignas(mutex::alignment) {};
        requirements::check_status _requirements_status {};
        cardano::tail_relative_stake_map _tail_relative_stake {};
        json::array _j_tail_relative_stake {};

        // request queue
        mutex::unique_lock::mutex_type _queue_mutex alignas(mutex::alignment) {};
        std::condition_variable_any _queue_cv alignas(mutex::alignment);
        std::deque<std::string> _queue {};
        mutex::unique_lock::mutex_type _results_mutex alignas(mutex::alignment) {};
        std::map<std::string, std::optional<json::value>> _results {};

        static std::pair<std::string_view, std::vector<std::string_view>> _parse_target(const std::string_view &target)
        {
            std::optional<std::string_view> req_id {};
            std::vector<std::string_view> params {};
            if (target.at(0) != '/')
                throw error(fmt::format("target must begin with / but got: '{}'", target));
            size_t start = 1;
            while (start < target.size()) {
                size_t end = target.find('/', start);
                if (end == target.npos)
                    end = target.size();
                const std::string_view part = target.substr(start, end - start);
                if (!part.empty()) {
                    if (!req_id)
                        req_id.emplace(part);
                    else
                        params.emplace_back(part);
                }
                start = end + 1;
            }
            if (!req_id)
                throw error(fmt::format("target must have request id: '{}'", target));
            return std::make_pair(std::move(*req_id), std::move(params));
        }

        static json::value _error_response(const std::string &msg)
        {
            logger::error("error response: {}", msg);
            return json::value {
                { "error", msg }
            };
        }

        void _process_request(const std::string &target)
        {
            json::value resp {};
            try {
                timer t { fmt::format("handling request {}", target) };
                const auto [req_id, params] = _parse_target(target);
                logger::info("begin processing request {} with params {}", req_id, params);
                if (req_id == "export" && params.size() == 1) {
                    resp = _api_export(params.at(0));
                } else if (req_id == "config-sync" && params.size() == 2) {
                    resp = _api_config_sync(params.at(0), params.at(1));
                } else if (req_id == "tx" && params.size() == 1 && params[0].size() == 2 * 32) {
                    resp = _api_tx_info(uint8_vector::from_hex(params[0]));
                } else if (req_id == "stake" && params.size() == 1) {
                    const auto bytes = uint8_vector::from_hex(params[0]);
                    const cardano::address addr { bytes };
                    if (!addr.has_stake_id())
                        throw error(fmt::format("provided address does not have a stake-key component: {}", bytes));
                    resp = _api_stake_id_info(addr.stake_id());
                } else if (req_id == "stake-assets" && params.size() == 3) {
                    const auto bytes = uint8_vector::from_hex(params.at(0));
                    const cardano::address addr { bytes };
                    if (!addr.has_stake_id())
                        throw error(fmt::format("provided address does not have a stake-key component: {}", bytes));
                    const auto offset = std::stoull(static_cast<std::string>(params.at(1)));
                    const auto count = std::stoull(static_cast<std::string>(params.at(2)));
                    resp = _api_stake_assets(addr.stake_id(), offset, count);
                } else if (req_id == "stake-txs" && params.size() == 3) {
                    const auto bytes = uint8_vector::from_hex(params.at(0));
                    const cardano::address addr { bytes };
                    if (!addr.has_stake_id())
                        throw error(fmt::format("provided address does not have a stake-key component: {}", bytes));
                    const auto offset = std::stoull(static_cast<std::string>(params.at(1)));
                    const auto count = std::stoull(static_cast<std::string>(params.at(2)));
                    resp = _api_stake_txs(addr.stake_id(), offset, count);
                } else if (req_id == "pay" && params.size() == 1) {
                    const auto bytes = uint8_vector::from_hex(params[0]);
                    const cardano::address addr { bytes };
                    if (!addr.has_pay_id())
                        throw error(fmt::format("provided address does not have a payment-key component: {}", bytes));
                    resp = _api_pay_id_info(addr.pay_id());
                } else if (req_id == "pay-assets" && params.size() == 3) {
                    auto bytes = uint8_vector::from_hex(params.at(0));
                    const cardano::address addr { bytes };
                    if (!addr.has_pay_id())
                        throw error(fmt::format("provided address does not have a payment-key component: {}", bytes));
                    auto offset = std::stoull(static_cast<std::string>(params.at(1)));
                    auto count = std::stoull(static_cast<std::string>(params.at(2)));
                    resp = _api_pay_assets(addr.pay_id(), offset, count);
                } else if (req_id == "pay-txs" && params.size() == 3) {
                    auto bytes = uint8_vector::from_hex(params.at(0));
                    const cardano::address addr { bytes };
                    if (!addr.has_pay_id())
                        throw error(fmt::format("provided address does not have a pay-key component: {}", bytes));
                    const auto offset = std::stoull(static_cast<std::string>(params.at(1)));
                    const auto count = std::stoull(static_cast<std::string>(params.at(2)));
                    resp = _api_pay_txs(addr.pay_id(), offset, count);
                } else if (req_id == "sync") {
                    resp = _api_sync();
                } else {
                    throw error(fmt::format("unsupported endpoint '{}'", req_id));
                }
                logger::info("request {} succeeded in {:0.3f} secs", target, t.stop());
            } catch (const std::exception &ex) {
                resp = _error_response(fmt::format("request {} failed: {}", target, ex.what()));
            } catch (...) {
                resp = _error_response(fmt::format("request {} failed: unknown exception", target));
            }
            {
                mutex::scoped_lock lk { _results_mutex };
                _results[target] = std::move(resp);
            }
        }

        void _worker_thread()
        {
            for (;;) {
                mutex::unique_lock lock { _queue_mutex };
                bool have_work = _queue_cv.wait_for(lock, std::chrono::seconds { 1 }, [&]{ return !_queue.empty(); });
                logger::trace("http-api worker thread waiting for tasks returned with {}", have_work);
                if (have_work) {
                    const auto target = _queue.front();
                    _queue.pop_front();
                    lock.unlock();
                    try {
                        _process_request(target);
                    } catch (const std::exception &ex) {
                        logger::error("worker process_request {}: std::exception {}", target, ex.what());
                    } catch (...) {
                        logger::error("worker process_request {}: unknown exception", target);
                    }
                }
            }
        }

        static http::response<http::string_body> _send_json_response(const http::request<http::string_body>& req, const json::value &json_resp)
        {
            auto resp_str = json::serialize(json_resp);
            resp_str += '\n';
            http::string_body::value_type body { std::move(resp_str) };
            const std::string &mime_type = "application/json";
            const auto size = body.size();

            http::response<http::string_body> res { std::piecewise_construct, std::make_tuple(std::move(body)), std::make_tuple(http::status::ok, req.version()) };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, mime_type);
            res.content_length(size);
            res.keep_alive(req.keep_alive());
            return res;
        }

        json::object _hardware_info() const
        {
            timer t { "collect hardware info" };
            const auto net_speed = asio::worker::get().internet_speed();
            return json::object {
                { "internet", fmt::format("{:0.1f}/{:0.1f} Mbps", net_speed.current, net_speed.max) },
                { "threads", fmt::format("{}/{}", _sched.active_workers(), _sched.num_workers()) },
                { "memory", fmt::format("{:0.1f}/{:0.1f} GiB", static_cast<double>(memory::max_usage_mb()) / (1 << 10), static_cast<double>(memory::physical_mb()) / (1 << 10)) },
                { "storage", fmt::format("{:0.1f}/{:0.1f} GB", static_cast<double>(file::disk_used(_data_dir.string())) / 1'000'000'000, static_cast<double>(file::disk_available(_data_dir.string())) / 1'000'000'000) }
            };
        }

        json::object _hardware_info_cached() const
        {
            static constexpr std::chrono::seconds update_delay { 5 };
            static mutex::unique_lock::mutex_type info_mutex alignas(mutex::alignment) {};
            static auto info = _hardware_info();
            static std::atomic<std::chrono::time_point<std::chrono::system_clock>> next_update { std::chrono::system_clock::now() + update_delay };
            static std::atomic_bool update_in_progress { false };

            auto old_next_update = next_update.load();
            const auto now = std::chrono::system_clock::now();
            const auto new_next_update = now + update_delay;
            if (now >= old_next_update && next_update.compare_exchange_strong(old_next_update, new_next_update)) {
                std::thread {[&] {
                    bool exp_false = false;
                    if (update_in_progress.compare_exchange_strong(exp_false, true)) {
                        logger::run_log_errors(
                            [&] {
                                auto new_info = _hardware_info();
                                mutex::scoped_lock lkw { info_mutex };
                                info = std::move(new_info);
                            },
                            [&] {
                                update_in_progress = false;
                            }
                        );
                    }
                } }.detach();
            }
            mutex::scoped_lock lkr { info_mutex };
            return info;
        }

        http::response<http::string_body> _api_status(const http::request<http::string_body>& req)
        {
            requirements::check_status req_status;
            {
                mutex::scoped_lock req_lk { _requirements_mutex };
                req_status = _requirements_status;
            }
            const auto status = _sync_status.load();
            json::object resp {};
            resp.emplace("syncType", fmt::format("{}", _sync_type));
            if (_sync_last_error)
                resp.emplace("syncError", *_sync_last_error);
            resp.emplace("validationMode", fmt::format("{}", _validation_mode));
            resp.emplace("ready", status == sync_status::ready);
            resp.emplace("requirements", req_status.to_json());
            resp.emplace("hardware", _hardware_info_cached());
            auto progress_copy = progress::get().copy();
            if (!progress_copy.empty()) {
                json::object task_progress {};
                for (const auto &[name, value]: progress_copy)
                    task_progress.emplace(name, fmt::format("{:0.3f}%", value * 100));
                resp.emplace("progress", std::move(task_progress));
            }
            {
                json::object requests {};
                mutex::scoped_lock lk { _results_mutex };
                for (const auto &[req_id, resp]: _results)
                    requests.emplace(req_id, static_cast<bool>(resp));
                resp.emplace("requests", std::move(requests));
            }
            switch (status) {
                case sync_status::ready:
                    resp.emplace("syncDuration", fmt::format("{:0.1f}", _sync_duration / 60));
                    resp.emplace("syncDataMB", fmt::format("{:0.1f}", _sync_data_mb));
                    if (_sync_last_chunk) {
                        const auto last_slot = _cr->make_slot(_sync_last_chunk->last_slot);
                        resp.emplace("lastBlock", json::object {
                            { "hash", fmt::format("{}", _sync_last_chunk->last_block_hash) },
                            { "slot", static_cast<uint64_t>(_sync_last_chunk->last_slot) },
                            { "epoch", last_slot.epoch() },
                            { "epochSlot", last_slot.epoch_slot() },
                            { "timestamp", fmt::format("{} UTC", last_slot.timestamp()) }
                        });
                    }
                    if (_cr->can_export())
                        resp.emplace("exportable", true);
                    break;
                case sync_status::syncing: {
                    const double in_progress = std::chrono::duration<double>(std::chrono::system_clock::now() - _sync_start).count();
                    resp.emplace("syncDuration", fmt::format("{:0.1f}", in_progress / 60));
                    if (const auto start_slot = _sync_start_slot.load(std::memory_order_relaxed); start_slot) {
                        const auto start_slot_obj = _cr->make_slot(*start_slot);
                        resp.emplace("syncStartSlot", fmt::format("from slot {} in epoch {}", start_slot_obj.epoch_slot(), start_slot_obj.epoch()));
                    }
                    if (const auto target_slot = _sync_target_slot.load(std::memory_order_relaxed); target_slot) {
                        const auto target_slot_obj = _cr->make_slot(*target_slot);
                        resp.emplace("syncTargetSlot", fmt::format("to slot {} in epoch {}", target_slot_obj.epoch_slot(), target_slot_obj.epoch()));
                    }
                    break;
                }
                case sync_status::failed:
                    resp.emplace("syncError", *_sync_last_error);
                    break;
                default:
                    throw error(fmt::format("internal error: unsupported value of the internal status: {}", static_cast<int>(status)));
            }
            return _send_json_response(req, std::move(resp));
        }

        json::value _api_config_sync(const std::string_view &network_source, const std::string_view &validation_mode)
        {
            if (network_source == "turbo")
                _sync_type = sync_type::turbo;
            else if (network_source == "p2p")
                _sync_type = sync_type::p2p;
            else
                logger::warn("unsupported network source: {}", network_source);
            if (validation_mode == "turbo")
                _validation_mode = sync::validation_mode_t::turbo;
            else if (validation_mode == "full")
                _validation_mode = sync::validation_mode_t::full;
            else
                logger::warn("unsupported network source: {}", network_source);
            return json::value { "ok" };
        }

        json::value _api_sync()
        {
            timer t { "api_sync" };
            logger::info("sync start");
            _sync_last_chunk.reset();
            _sync_last_error.reset();
            _sync_start = std::chrono::system_clock::now();
            _sync_status = sync_status::syncing;
            try {
                if (!_ignore_requirements) {
                    const auto req_status = requirements::check(_data_dir.string());
                    {
                        mutex::scoped_lock req_lk { _requirements_mutex };
                        _requirements_status = req_status;
                    }
                    if (!req_status)
                        throw error("requirements check failed - cannot begin the sync!");
                }
                // destroy the reconstructor instance, so that the index files can be removed and updated
                if (_reconst)
                    _reconst.reset();
                if (!_cr)
                    _cr = std::make_unique<chunk_registry>(_data_dir.string());
                {
                    std::unique_ptr<sync::syncer> syncr {};
                    std::shared_ptr<sync::peer_info> peer {};
                    switch (_sync_type) {
                        case sync_type::none: {
                            // keep the pointers empty!
                            break;
                        }
                        case sync_type::turbo: {
                            auto t_syncr = std::make_unique<sync::turbo::syncer>(*_cr);
                            peer = t_syncr->find_peer();
                            syncr = std::move(t_syncr);
                            break;
                        }
                        case sync_type::p2p: {
                            auto t_syncr = std::make_unique<sync::p2p::syncer>(*_cr);
                            peer = t_syncr->find_peer();
                            syncr = std::move(t_syncr);
                            break;
                        }
                        case sync_type::hybrid: {
                            auto t_syncr = std::make_unique<sync::hybrid::syncer>(*_cr);
                            peer = t_syncr->find_peer();
                            syncr = std::move(t_syncr);
                            break;
                        }
                        default:
                            throw error(fmt::format("unsupported sync type: {}", static_cast<int>(_sync_type)));
                    }
                    const uint64_t start_offset = _cr->valid_end_offset();
                    _sync_start_slot = _cr->tip() ? _cr->tip()->slot : 0;
                    if (syncr && peer) {
                        _sync_target_slot = peer->tip() ? peer->tip()->slot : 0;
                        logger::reset_last_error();
                        syncr->sync(std::move(peer), {}, _validation_mode);
                        _sync_last_error = logger::last_error();
                    } else {
                        _sync_target_slot.store(_sync_start_slot.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    }
                    _sync_data_mb = static_cast<double>(_cr->valid_end_offset() - start_offset) / 1000000;
                    // prepare JSON array with tail_relative_stake data
                    _tail_relative_stake = _cr->tail_relative_stake();
                    _j_tail_relative_stake.clear();
                    for (const auto &[point, rel_stake]: _tail_relative_stake) {
                        _j_tail_relative_stake.emplace_back(json::object {
                            { "slot", point.slot },
                            { "relativeStake", rel_stake }
                        });
                    }
                }
                _reconst = std::make_unique<reconstructor>(*_cr);
                _sync_last_chunk = _cr->last_chunk();
                _sync_duration = std::chrono::duration<double>(std::chrono::system_clock::now() - _sync_start).count();
                _sync_status = sync_status::ready;
                logger::info("synchronization complete, all API endpoints are available now");
            } catch (std::exception &ex) {
                logger::error("sync failed: {}", ex.what());
                if (!_sync_last_error)
                    _sync_last_error = std::make_shared<std::string>(ex.what());
                _sync_status = sync_status::failed;
            }
            return json::value { "synchronization complete" };
        }

        json::value _api_export(const boost::urls::pct_string_view export_dir_enc) const
        {
            std::string export_dir {};
            export_dir.resize(export_dir_enc.decoded_size());
            export_dir_enc.decode({}, boost::urls::string_token::assign_to(export_dir));
            _cr->node_export(export_dir, _cr->immutable_tip().value());
            return json::object {
                { "dataSizeGB", static_cast<double>(_cr->valid_end_offset()) / (1ULL << 30) },
                { "numChunks", _cr->num_chunks() }
            };
        }

        json::value _api_tx_info(const buffer &tx_hash)
        {
            auto tx_info = _reconst->find_tx(tx_hash);
            if (!tx_info) {
                return json::object {
                    { "hash", fmt::format("{}", tx_hash) },
                    { "error", "transaction data have not been found!" }
                };
            }
            return (*tx_info)->to_json(_tail_relative_stake);
        }

        template<typename T>
        history<T> _find_history(const T &id, const std::string &suffix)
        {
            struct cache_meta {
                T id {};
                cardano::block_hash last_block_hash {};
            };
            if (_cr->num_chunks() == 0)
                return history<T> { _cr->config() };
            const std::string cache_meta_path = fmt::format("{}/meta-{}.bin", _cache_dir.string(), suffix);
            const std::string cache_data_path = fmt::format("{}/data-{}.bin", _cache_dir.string(), suffix);
            if (std::filesystem::exists(cache_meta_path) && std::filesystem::exists(cache_data_path)) {
                cache_meta meta {};
                zpp::load(meta, cache_meta_path);
                if (meta.id == id && meta.last_block_hash == _cr->last_chunk()->last_block_hash) {
                    timer t { fmt::format("load {} cached history for {}", suffix, id), logger::level::info };
                    history<T> hist { _cr->config() };
                    zpp::load(hist, cache_data_path);
                    if (hist.id == id) {
                        return hist;
                    }
                }
            }
            timer t { fmt::format("find {} history for {}", suffix, id), logger::level::info };
            auto hist = _reconst->find_history(id);
            zpp::save(cache_data_path, hist);
            zpp::save(cache_meta_path, cache_meta { id, _cr->last_chunk()->last_block_hash });
            return hist;
        }

        history<cardano::stake_ident> _find_stake_history(const cardano::stake_ident &id)
        {
            return _find_history(id, "stake");
        }

        history<cardano::pay_ident> _find_pay_history(const cardano::pay_ident &id)
        {
            return _find_history(id, "pay");
        }

        json::value _api_stake_id_info(const cardano::stake_ident &id)
        {
            auto hist = _find_stake_history(id);
            if (hist.transactions.size() == 0) {
                return json::object {
                    { "id", hist.id.to_json() },
                    { "error", "could't find any transactions referencing this stake key!" }
                };
            }
            return hist.to_json(_tail_relative_stake, _cr->config());
        }

        json::value _api_stake_txs(const cardano::stake_ident &id, const size_t offset, const size_t max_items)
        {
            auto hist = _find_stake_history(id);
            return json::object {
                { "id", hist.id.to_json() },
                { "txCount", hist.transactions.size() },
                { "txOffset", offset },
                { "transactions", hist.transactions.to_json(_tail_relative_stake, _cr->config(), offset, max_items) }
            };
        }

        json::object _api_stake_assets(const cardano::stake_ident &id, const size_t offset, const size_t max_items)
        {
            auto hist = _find_stake_history(id);
            return json::object {
                { "id", hist.id.to_json() },
                { "assetCount", hist.balance_assets.size() },
                { "assetOffset", offset },
                { "assets", hist.balance_assets.to_json(offset, max_items) }
            };
        }

        json::value _api_pay_id_info(const cardano::pay_ident &pay_id)
        {
            auto hist = _find_pay_history(pay_id);
            if (hist.transactions.size() == 0) {
                return json::object {
                    { "id", hist.id.to_json() },
                    { "error", "couldn't find any transactions referencing this payment key!" }
                };
            }
            return hist.to_json(_tail_relative_stake, _cr->config());
        }

        json::value _api_pay_txs(const cardano::pay_ident &id, const size_t offset, const size_t max_items)
        {
            auto hist = _find_pay_history(id);
            return json::object {
                { "id", hist.id.to_json() },
                { "txCount", hist.transactions.size() },
                { "txOffset", offset },
                { "transactions", hist.transactions.to_json(_tail_relative_stake, _cr->config(), offset, max_items) }
            };
        }

        json::object _api_pay_assets(const cardano::pay_ident &id, const size_t offset, const size_t max_items)
        {
            auto hist = _find_pay_history(id);
            return json::object {
                { "id", hist.id.to_json() },
                { "assetCount", hist.balance_assets.size() },
                { "assetOffset", offset },
                { "assets", hist.balance_assets.to_json(offset, max_items) }
            };
        }

        void _handle_request(http::request<http::string_body> &&req, send_lambda &&send)
        {
            if(req.method() != http::verb::get)
                throw error(fmt::format("Unsupported HTTP method {}", static_cast<std::string_view>(req.method_string())));
            const auto target = static_cast<std::string>(req.target());
            try {
                timer t { target, logger::level::trace };
                if (target.starts_with("/status/")) {
                    send(_api_status(req));
                } else if (_sync_status != sync_status::ready) {
                    json::value resp {};
                    if (_sync_status == sync_status::syncing)
                        resp = _error_response("Sync in progress, the API is not yet ready!");
                    else if (_sync_status == sync_status::failed)
                        resp = _error_response(*_sync_last_error);
                    else
                        resp = _error_response("The synchronization state is unknown");
                    send(_send_json_response(req, resp));
                } else {
                    std::optional<json::value> resp {};
                    {
                        mutex::scoped_lock results_lk { _results_mutex };
                        if (auto r_it = _results.find(target); r_it != _results.end()) {
                            if (r_it->second) {
                                resp.emplace();
                                std::swap(*resp, *r_it->second);
                                _results.erase(r_it);
                            } else {
                                resp.emplace(json::object { { "delayed", true } });
                            }
                        } else {
                            _results.emplace(target, std::optional<json::value> {});
                        }
                    }
                    if (!resp) {
                        mutex::unique_lock queue_lk { _queue_mutex };
                        _queue.emplace_back(target);
                        queue_lk.unlock();
                        _queue_cv.notify_one();
                        resp = json::object { { "delayed", true } };
                    }
                    send(_send_json_response(req, std::move(*resp)));
                }
            } catch (const std::exception &ex) {
                logger::error("request {}: {}", target, ex.what());
                send(_send_json_response(req, _error_response("Illegal request")));
            }
        }

        static void _fail(beast::error_code ec, char const* what)
        {
            logger::error("{}: {}", what, ec.message());
        }

        void _do_session(beast::tcp_stream& stream, net::yield_context yield)
        {
            bool close = false;
            beast::error_code ec;
            beast::flat_buffer buffer;
            send_lambda lambda { stream, close, ec, yield };

            for(;;) {
                stream.expires_after(std::chrono::seconds(30));
                http::request<http::string_body> req;
                http::async_read(stream, buffer, req, yield[ec]);
                if(ec == http::error::end_of_stream) break;
                if(ec) return _fail(ec, "read");
                _handle_request(std::move(req), std::move(lambda));
                if(ec) return _fail(ec, "write");
                if(close) break;
            }
            stream.socket().shutdown(tcp::socket::shutdown_send, ec);
        }

        void _do_listen(net::io_context& ioc, tcp::endpoint endpoint, net::yield_context yield)
        {
            beast::error_code ec;
            tcp::acceptor acceptor(ioc);
            acceptor.open(endpoint.protocol(), ec);
            if(ec) return _fail(ec, "open");
            acceptor.set_option(net::socket_base::reuse_address(true), ec);
            if(ec) return _fail(ec, "set_option");
            acceptor.bind(endpoint, ec);
            if(ec) return _fail(ec, "bind");
            acceptor.listen(net::socket_base::max_listen_connections, ec);
            if(ec) return _fail(ec, "listen");
            logger::info("http-api server is ready to serve requests");
            for(;;) {
                tcp::socket socket(ioc);
                acceptor.async_accept(socket, yield[ec]);
                if(ec) _fail(ec, "accept");
                else boost::asio::spawn(acceptor.get_executor(), std::bind(&impl::_do_session, std::ref(*this), beast::tcp_stream(std::move(socket)), std::placeholders::_1));
            }
        }
    };

    server::server(const std::string &data_dir, const bool ignore_requirements, scheduler &sched)
        : _impl { std::make_unique<impl>(data_dir, ignore_requirements, sched) }
    {
    }

    server::~server() =default;

    void server::serve(const std::string &ip, uint16_t port)
    {
        _impl->serve(ip, port);
    }
}