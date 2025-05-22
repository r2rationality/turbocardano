#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include "common.hpp"
#include "types.hpp"
#include "miniprotocol/handshake/types.hpp"

namespace daedalus_turbo::cardano::network {
    using protocol_send_func = std::function<void(data_generator_t &&)>;

    struct protocol_observer_t: base_observer_t {
        virtual void data(buffer, const protocol_send_func &) =0;
    };
    using protocol_observer_ptr = std::shared_ptr<protocol_observer_t>;
    using protocol_observer_factory_t = std::function<protocol_observer_ptr(const miniprotocol::handshake::result_t &)>;

    template<typename T>
    data_generator_t message_generator(T m)
    {
        auto gen = [](T msg) -> data_generator_t {
            cbor::encoder enc {};
            msg.to_cbor(enc);
            co_yield std::move(enc.cbor());
        }(std::move(m));
        return gen;
    }

    using multiplexer_config_t = std::map<mini_protocol, protocol_observer_factory_t>;

    struct multiplexer {
        struct ok_t {
            constexpr bool operator==(const ok_t &) const noexcept
            {
                return true;
            }
        };
        struct stopped_t {
            constexpr bool operator==(const stopped_t &) const noexcept
            {
                return true;
            }
        };
        struct failed_t {
            std::string err;

            constexpr bool operator==(const failed_t &o) const noexcept
            {
                return err == o.err;
            }
        };

        struct state_t: std::variant<ok_t, stopped_t, failed_t> {
            using base_type = std::variant<ok_t, stopped_t, failed_t>;
            using base_type::base_type;

            operator bool() const noexcept
            {
                return std::holds_alternative<ok_t>(*this);
            }
        };

        multiplexer(connection_ptr &&, multiplexer_config_t &&);
        ~multiplexer();
        bool try_send(mini_protocol mp, data_generator_t &&generator);
        bool try_send(mini_protocol mp, buffer data);
        bool alive() const;
        bool available_egress() const;
        bool available_ingress() const;
        state_t state() const;
        void process_egress(op_observer_ptr=std::make_shared<noop_observer_t>());
        void process_ingress(op_observer_ptr=std::make_shared<noop_observer_t>());
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::cardano::network::multiplexer::ok_t>: formatter<std::span<const uint8_t>> {
        template<typename FormatContext>
        auto format(const auto &, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "ok");
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::network::multiplexer::stopped_t>: formatter<std::span<const uint8_t>> {
        template<typename FormatContext>
        auto format(const auto &, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "stopped");
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::network::multiplexer::failed_t>: formatter<std::span<const uint8_t>> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "failed: {}", v.err);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::network::multiplexer::state_t>: formatter<std::span<const uint8_t>> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return std::visit([&](const auto &cv) -> decltype(ctx.out()) {
                return fmt::format_to(ctx.out(), "failed: {}", cv);
            }, v);
        }
    };
}