/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "handler.hpp"
#include <turbo/chunk-registry.hpp>
#include "messages.hpp"

namespace turbo::cardano::network::miniprotocol::chainsync {
    struct handler::impl {
        explicit impl(std::shared_ptr<chain_source> source):
            _source { std::move(source) }
        {
        }

        void data(const buffer bytes, const protocol_send_func &send_func)
        {
            if (!_state.is<st_idle_t>()) [[unlikely]]
                throw error(fmt::format("no messages are expected in state {} but got one: {} bytes", _state.name(), bytes.size()));
            _process_st_idle_msg(bytes, send_func);
        }

        void poll(const protocol_send_func &send)
        {
            if (_state.is<st_must_reply_t>())
                _next(send, false);
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

        const std::shared_ptr<chain_source> _source;
        std::shared_ptr<const chain_source::view> _cursor_view;
        bool _initial_rollback = false;
        state_t _state {};
        optional_point2 _isect {};

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

        optional_point3 _tip(const chain_source::view &v) const
        {
            return v.tip;
        }

        void _process_msg(const msg_done_t &, const protocol_send_func &)
        {
            _state = st_done_t {};
        }

        void _process_msg(const msg_find_intersect_t &msg, const protocol_send_func &send_func)
        {
            const auto view = _source->current();
            _isect.reset();
            _initial_rollback = false;
            _cursor_view = view;
            for (const auto &p: msg.points) {
                if (!p || view->find(*p)) {
                    _isect = p;
                    _initial_rollback = true;
                    return _respond(send_func, msg_intersect_found_t { p, _tip(*view) });
                }
            }
            _respond(send_func, msg_intersect_not_found_t { _tip(*view) });
        }

        void _next(const protocol_send_func &send, const bool can_await)
        {
            const auto view = _source->current();
            if (_isect && !view->find(*_isect)) {
                auto old_pos = _cursor_view ? _cursor_view->find(*_isect) : std::nullopt;
                _isect.reset();
                while (old_pos) {
                    const auto p = _cursor_view->block(*old_pos).point2();
                    if (view->find(p)) { _isect = p; break; }
                    old_pos = _cursor_view->previous(*old_pos);
                }
                _initial_rollback = true;
            }
            _cursor_view = view;
            if (_initial_rollback) {
                _initial_rollback = false;
                return _respond(send, msg_roll_backward_t { _isect, _tip(*view) });
            }
            auto pos = _isect ? view->next(*view->find(*_isect)) : chain_source::view::position { 0, 0 };
            if (pos != view->end()) {
                _isect = view->block(pos).point2();
                return _respond(send, msg_roll_forward_t { _source->read_header(*view, pos), _tip(*view) });
            }
            if (can_await)
                _respond(send, msg_await_reply_t {});
        }

        void _process_msg(const msg_request_next_t &, const protocol_send_func &send_func)
        {
            _next(send_func, true);
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

    handler::handler(std::shared_ptr<chunk_registry> cr)
        : handler { std::make_shared<chain_source>(std::move(cr)) }
    {
    }

    handler::handler(std::shared_ptr<chain_source> source):
        _impl { std::make_unique<impl>(std::move(source)) }
    {
    }

    handler::~handler() =default;

    void handler::data(const buffer bytes, const protocol_send_func &send_func)
    {
        _impl->data(bytes, send_func);
    }

    void handler::poll(const protocol_send_func &send)
    {
        _impl->poll(send);
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
