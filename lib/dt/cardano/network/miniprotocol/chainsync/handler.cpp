/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/chunk-registry.hpp>
#include "handler.hpp"
#include "messages.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::chainsync {
    struct handler::impl {
        explicit impl(std::shared_ptr<chunk_registry> cr):
            _cr { std::move(cr) }
        {
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
        struct st_intersect_t {};
        struct st_can_await_t {};
        struct st_must_reply_t {};
        struct st_done_t {};

        struct state_t {
            using val_type = std::variant<st_idle_t, st_intersect_t, st_can_await_t, st_must_reply_t, st_done_t>;

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
        state_t _state {};
        std::optional<point2> _isect {};

        void _send(const protocol_send_func &send_func, msg_t &&m)
        {
            send_func([](msg_t msg) -> data_generator_t {
                cbor::encoder enc {};
                msg.to_cbor(enc);
                co_yield std::move(enc.cbor());
            }(std::move(m)));
        }

        void _respond(const protocol_send_func &send_func, msg_intersect_found_t &&msg)
        {
            _send(send_func, msg);
            _state = st_idle_t {};
        }

        void _respond(const protocol_send_func &send_func, msg_intersect_not_found_t &&msg)
        {
            _send(send_func, msg);
            _state = st_idle_t {};
        }

        void _respond(const protocol_send_func &send_func, msg_roll_forward_t &&msg)
        {
            _send(send_func, std::move(msg));
            _state = st_idle_t {};
        }

        void _respond(const protocol_send_func &send_func, msg_roll_backward_t &&msg)
        {
            _send(send_func, std::move(msg));
            _state = st_idle_t {};
        }

        void _respond(const protocol_send_func &send_func, msg_await_reply_t &&msg)
        {
            _send(send_func, msg);
            _state = st_must_reply_t {};
        }

        template<typename M>
        void _process_msg(const M &, const protocol_send_func &)
        {
            throw error(fmt::format("messages of type {} are not expected!", typeid(M).name()));
        }

        void _process_msg(const msg_find_intersect_t &msg, const protocol_send_func &send_func)
        {
            for (const auto &p: msg.points) {
                if (const auto block_info = _cr->find_block_by_slot_no_throw(p.slot, p.hash); block_info) {
                    _isect = block_info->point2();
                    return _respond(send_func, msg_intersect_found_t { block_info->point2(), _cr->tip().value() });
                }
            }
            if (const auto tip = _cr->tip(); tip)
                return _respond(send_func, msg_intersect_not_found_t { *tip });
            return _respond(send_func, msg_intersect_not_found_t { point { _cr->config().byron_genesis_hash, 0 } });
        }

        void _process_msg(const msg_request_next_t &, const protocol_send_func &send_func)
        {
            // By default, stream from the starting block
            auto it = _cr->cbegin();
            // If there is an intersection we stream from the first block after it
            if (_isect) {
                it = _cr->find_block(*_isect);
                if (it == _cr->cend()) [[unlikely]]
                    throw error(fmt::format("internal error: cannot find the intersection block!"));
                ++it;
            }
            if (it != _cr->cend()) [[likely]] {
                // simulate the Cardano Node behavior
                //if (it == _cr->cbegin()) [[unlikely]]
                //    _respond(send_func, msg_roll_backward_t { optional_point2 {}, *_cr->tip() });
                if (it != _cr->cend()) [[likely]] {
                    _isect = it->point2();
                    auto hdr = it.header();
                    return _respond(send_func, msg_roll_forward_t { std::move(hdr), _cr->tip().value() });
                }
            }
            _respond(send_func, msg_await_reply_t {});
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

    handler::handler(std::shared_ptr<chunk_registry> cr):
        _impl { std::make_unique<impl>(std::move(cr)) }
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
