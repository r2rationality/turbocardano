#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/asio.hpp>
#include <dt/cardano/common/config.hpp>
#include <dt/cardano/network/miniprotocol/blockfetch/messages.hpp>
#include <dt/scheduler-fwd.hpp>

namespace daedalus_turbo::cardano::network {
    typedef daedalus_turbo::error error;

    struct version_config_t {
        uint64_t min=14;
        uint64_t max=15;
    };

    enum class protocol_t: uint8_t {
        node_to_node = 0,
        node_to_client = 1
    };

    enum class mini_protocol: uint16_t {
        handshake = 0,
        chain_sync = 2,
        block_fetch = 3,
        tx_submission = 4,
        keep_alive = 8
    };

    enum class channel_mode: uint8_t {
        initiator = 0,
        responder = 1
    };

    struct segment_info {
        static constexpr size_t max_payload_size = 0xFFFF;

        explicit segment_info() =default;

        explicit segment_info(const uint32_t time, const channel_mode mode, const mini_protocol mp_id, const uint16_t size)
            : _time_us { host_to_net(time) }, _meta { host_to_net(_mode(mode) | _mini_protocol_id(mp_id) | static_cast<uint32_t>(size) ) }
        {
        }

        channel_mode mode() const
        {
            auto host_meta = net_to_host(_meta);
            return (host_meta >> 31) & 1 ? channel_mode::responder : channel_mode::initiator;
        }

        mini_protocol mini_protocol_id() const
        {
            auto host_meta = net_to_host(_meta);
            auto mp_id = static_cast<uint16_t>((host_meta >> 16) & 0x7FFF);
            return static_cast<mini_protocol>(mp_id);
        }

        uint16_t payload_size() const
        {
            auto host_meta = net_to_host(_meta);
            return static_cast<uint16_t>(host_meta & 0xFFFF);
        }

        buffer read_buf() const noexcept
        {
            return { reinterpret_cast<const uint8_t *>(this), sizeof(*this) };
        }

        write_buffer write_buf() noexcept
        {
            return { reinterpret_cast<uint8_t *>(this), sizeof(*this) };
        }
    private:
        static uint32_t _mode(const channel_mode m)
        {
            return m == channel_mode::responder ? 1U << 31U : 0U;
        }

        static uint32_t _mini_protocol_id(const mini_protocol mp_id)
        {
            return (static_cast<uint32_t>(mp_id) & 0x7FFFU) << 16U;
        }

        uint32_t _time_us = 0;
        uint32_t _meta = 0;
    };
    static_assert(sizeof(segment_info) == 8);

    struct address {
        std::string host {};
        std::string port {};

        bool operator==(const auto &o) const
        {
            return host == o.host && port == o.port;
        }

        bool operator<(const auto &o) const
        {
            if (host == o.host)
                return port < o.port;
            return host < o.host;
        }
    };

    using header_list = point2_list;

    struct client {
        using error_msg = std::string;

        struct find_response {
            address addr {};
            std::variant<intersection_info_t, error_msg> res { "No response or error has yet been assigned" };
        };
        using find_handler = std::function<void(find_response &&)>;

        struct one_block_response_t {
            parsed_block_ptr_t block;
            std::optional<std::string> err {};
        };
        using msg_block_t = miniprotocol::blockfetch::msg_block_t;
        using msg_compressed_blocks_t = miniprotocol::blockfetch::msg_compressed_blocks_t;
        using block_response_t = std::variant<msg_block_t, msg_compressed_blocks_t, error_msg>;
        using block_handler = std::function<bool(block_response_t)>;

        struct header_response {
            address addr {};
            std::optional<point2> intersect {};
            std::optional<point3> tip {};
            std::variant<header_list, error_msg> res {};
        };
        using header_handler = std::function<void(header_response &&)>;

        explicit client(const address &addr, const cardano::config &/*cfg*/=cardano::config::get())
            : _addr { addr }
        {
        }

        virtual ~client() = default;

        const address &addr() const
        {
            return _addr;
        }

        void find_tip(const find_handler &handler)
        {
            point2_list empty {};
            _find_intersection_impl(empty, handler);
        }

        void find_intersection(const point2_list &points, const find_handler &handler)
        {
            _find_intersection_impl(points, handler);
        }

        void fetch_headers(const point2_list &points, const size_t max_blocks, const header_handler &handler)
        {
            _fetch_headers_impl(points, max_blocks, handler);
        }

        point3 find_tip_sync()
        {
            find_response iresp {};
            _find_intersection_impl({}, [&](auto &&r) { iresp = std::move(r); });
            process();
            if (std::holds_alternative<error_msg>(iresp.res))
                throw error(fmt::format("find_tip error: {}", std::get<error_msg>(iresp.res)));
            return variant::get_nice<intersection_info_t>(iresp.res).tip;
        }

        intersection_info_t find_intersection_sync(const point2_list &points)
        {
            find_response iresp {};
            _find_intersection_impl(points, [&](auto &&r) { iresp = std::move(r); });
            process();
            if (std::holds_alternative<error_msg>(iresp.res))
                throw error(fmt::format("find_intersection error: {}", std::get<error_msg>(iresp.res)));
            return variant::get_nice<intersection_info_t>(iresp.res);
        }

        std::pair<header_list, point3> fetch_headers_sync(const point2_list &points, const size_t max_blocks, const bool allow_empty=false)
        {
            client::header_response iresp {};
            fetch_headers(points, max_blocks, [&](auto &&r) {
                iresp = r;
            });
            process();
            if (std::holds_alternative<client::error_msg>(iresp.res))
                throw error(fmt::format("fetch_headers error: {}", std::get<client::error_msg>(iresp.res)));
            if (!iresp.tip)
                throw error("no tip information received!");
            const auto &headers = std::get<header_list>(iresp.res);
            if (headers.empty() && !allow_empty)
                throw error("received an empty header list");
            return std::make_pair(std::move(headers), std::move(*iresp.tip));
        }

        std::pair<header_list, point3> fetch_headers_sync(const std::optional<point> &local_tip, const size_t max_blocks, const bool allow_empty=false)
        {
            point2_list points {};
            if (local_tip)
                points.emplace_back(*local_tip);
            return fetch_headers_sync(points, max_blocks, allow_empty);
        }

        void fetch_blocks(const point2 &from, const point2 &to, const block_handler &handler);

        void process(daedalus_turbo::scheduler *sched=nullptr, asio::worker *iow=nullptr)
        {
            _process_impl(sched, iow);
        }

        void reset()
        {
            _reset_impl();
        }
    protected:
        const address _addr;

    private:
        virtual void _find_intersection_impl(const point2_list &/*points*/, const find_handler &/*handler*/)
        {
            throw error("cardano::network::client::_find_intersection_impl not implemented!");
        }

        virtual void _fetch_headers_impl(const point2_list &/*points*/, const size_t /*max_blocks*/, const header_handler &/*handler*/)
        {
            throw error("cardano::network::client::_fetch_headers_impl not implemented!");
        }

        virtual void _fetch_blocks_impl(const point2 &/*from*/, const point2 &/*to*/, const block_handler &/*handler*/)
        {
            throw error("cardano::network::client::_fetch_blocks_impl not implemented!");
        }

        virtual void _process_impl(scheduler */*sched*/, asio::worker *iow)
        {
            throw error("cardano::network::client::_process_impl not implemented!");
        }

        virtual void _reset_impl()
        {
            throw error("cardano::network::client::_reset_impl not implemented!");
        }
    };

    struct client_manager {
        virtual ~client_manager() =default;

        std::unique_ptr<client> connect(const address &addr, const version_config_t &versions={}, const config &cfg=config::get(), const asio::worker_ptr &asio_worker=asio::worker::get())
        {
            return _connect_impl(addr, versions, cfg, asio_worker);
        }
    private:
        virtual std::unique_ptr<client> _connect_impl(const address &/*addr*/, const version_config_t &, const cardano::config &/*cfg*/, const asio::worker_ptr &/*asio_worker*/)
        {
            throw error("cardano::network::client_manager::_connect_impl not implemented!");
        }
    };

    struct client_connection: client {
        explicit client_connection(const address &addr, const version_config_t &, const cardano::config &cfg=cardano::config::get(), const asio::worker_ptr &asio_worker=asio::worker::get());
        ~client_connection() override;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;

        void _find_intersection_impl(const point2_list &points, const find_handler &handler) override;
        void _fetch_headers_impl(const point2_list &points, const size_t max_blocks, const header_handler &handler) override;
        void _fetch_blocks_impl(const point2 &from, const point2 &to, const block_handler &handler) override;
        void _process_impl(scheduler *sched, asio::worker *) override;
        void _reset_impl() override;
    };

    struct client_manager_async: client_manager {
        static client_manager_async &get();
    private:
        std::unique_ptr<client> _connect_impl(const address &addr, const version_config_t &, const cardano::config &cfg, const asio::worker_ptr &iow) override;
    };
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::cardano::network::address>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}:{}", v.host, v.port);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::network::mini_protocol>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using daedalus_turbo::cardano::network::mini_protocol;
            switch (v) {
                case mini_protocol::handshake: return fmt::format_to(ctx.out(), "handshake");
                case mini_protocol::block_fetch: return fmt::format_to(ctx.out(), "block_fetch");
                case mini_protocol::chain_sync: return fmt::format_to(ctx.out(), "chain_sync");
                case mini_protocol::keep_alive: return fmt::format_to(ctx.out(), "keep_alive");
                case mini_protocol::tx_submission: return fmt::format_to(ctx.out(), "tx_submission");
                [[unlikely]] default: throw daedalus_turbo::error(fmt::format("an unsupported value for mini_protocol: {}", static_cast<int>(v)));
            }
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::network::channel_mode>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using daedalus_turbo::cardano::network::channel_mode;
            switch (v) {
                case channel_mode::initiator: return fmt::format_to(ctx.out(), "initiator");
                case channel_mode::responder: return fmt::format_to(ctx.out(), "responder");
                [[unlikely]] default: throw daedalus_turbo::error(fmt::format("an unsupported value for channel_mode: {}", static_cast<int>(v)));
            }
        }
    };
}