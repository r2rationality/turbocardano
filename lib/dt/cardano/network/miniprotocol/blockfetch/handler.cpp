/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/chunk-registry.hpp>
#include "handler.hpp"
#include "messages.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::blockfetch {
    struct handler::impl {
        explicit impl(std::shared_ptr<chunk_registry> &&cr, config_t cfg):
            _cr { std::move(cr) }, _cfg { std::move(cfg) }
        {
            logger::info("created blockfetch handler with block_compression: {}", _cfg.block_compression);
        }

        void data(const buffer bytes, const protocol_send_func &send_func)
        {
            if (!_state.is<st_idle_t>()) [[unlikely]]
                throw error(fmt::format("no messages are expected in state {} but got one: {} bytes", _state.name(), bytes.size()));
            _process_st_idle_msg(bytes, send_func);
        }

        void failed(const std::string_view)
        {
            _state = st_done_t {};
        }

        void stopped()
        {
            _state = st_done_t {};
        }
    private:
        struct st_idle_t {};
        struct st_busy_t {};
        struct st_streaming_t {};
        struct st_done_t {};

        struct state_t {
            using val_type = std::variant<st_idle_t, st_busy_t, st_streaming_t, st_done_t>;

            state_t() =default;

            const char *name() const
            {
                return std::visit([](const auto &sv) -> const char * {
                    return typeid(std::decay_t<decltype(sv)>).name();
                }, _val);
            }

            template<typename T>
            bool is() const noexcept
            {
                return std::holds_alternative<T>(_val);
            }

            state_t &operator=(val_type &&new_val)
            {
                _val = std::move(new_val);
                _start = std::chrono::system_clock::now();
                return *this;
            }
        private:
            val_type _val { st_idle_t {} };
            std::chrono::system_clock::time_point _start = std::chrono::system_clock::now();
        };

        const std::shared_ptr<const chunk_registry> _cr;
        const config_t _cfg;
        state_t _state {};
        std::optional<point2> _isect {};

        void _send(const protocol_send_func &write_func, data_generator_t &&gen)
        {
            write_func(std::move(gen));
        }

        void _respond(const protocol_send_func &send_func, msg_no_blocks_t &&msg)
        {
            _send(send_func, message_generator(std::move(msg)));
            _state = st_idle_t {};
        }

        template<typename M>
        void _process_msg(const M &, const protocol_send_func &)
        {
            throw error(fmt::format("messages of type {} are not expected!", typeid(M).name()));
        }

        void _process_msg(const msg_client_done_t &, const protocol_send_func &)
        {
            _state = st_done_t {};
        }

        void _process_msg(const msg_request_range_t &msg, const protocol_send_func &send_func)
        {
            _state = st_busy_t {};
            logger::info("blockfetch from: {} to: {}", msg.from, msg.to);
            const auto from_it = _cr->find_block(msg.from);
            if (from_it == _cr->cend())
                return _respond(send_func, msg_no_blocks_t {});
            auto end_it = _cr->find_block(msg.to);
            if (end_it == _cr->cend())
                return _respond(send_func, msg_no_blocks_t {});
            _state = st_streaming_t {};
            ++end_it;
            _send(send_func, [](const bool block_compression, auto &state, auto first_it, auto last_it) -> data_generator_t {
                logger::info("blockfetch msg_start_batch");
                {
                    cbor::encoder enc {};
                    msg_start_batch_t {}.to_cbor(enc);
                    co_yield std::move(enc.cbor());
                }
                for (auto it = first_it; it != last_it; ) {
                    cbor::encoder enc {};
                    if (block_compression) {
                        logger::info("blockfetch msg_compressed_blocks");
                        auto [chunk_rem_data, next_it] = it.chunk_remaining_data(last_it);
                        msg_compressed_blocks_t { 1ULL, std::move(chunk_rem_data) }.to_cbor(enc);
                        it = next_it;
                    } else {
                        msg_block_t { it.block_data() }.to_cbor(enc);
                        ++it;
                    }
                    co_yield std::move(enc.cbor());
                }
                logger::info("blockfetch msg_batch_done");
                {
                    cbor::encoder enc {};
                    msg_batch_done_t {}.to_cbor(enc);
                    co_yield std::move(enc.cbor());
                }
                state = st_idle_t {};
            }(_cfg.block_compression, _state, from_it, end_it));
        }

        void _process_st_idle_msg(const buffer bytes, const protocol_send_func &send_func)
        {
            auto pv = cbor::zero2::parse(bytes);
            const auto msg = msg_t::from_cbor(pv.get());
            std::visit([&](const auto &mv) {
                _process_msg(mv, send_func);
            }, msg);
        }
    };

    handler::handler(std::shared_ptr<chunk_registry> cr, config_t cfg):
        _impl { std::make_unique<impl>(std::move(cr), std::move(cfg)) }
    {
    }

    handler::~handler() =default;

    void handler::data(const buffer bytes, const protocol_send_func &send_func)
    {
        _impl->data(bytes, send_func);
    }

    void handler::failed(const std::string_view err)
    {
        _impl->failed(err);
    }

    void handler::stopped()
    {
        _impl->stopped();
    }
}
