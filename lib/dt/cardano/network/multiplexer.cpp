/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/network/multiplexer.hpp>
#include <dt/mutex.hpp>

namespace daedalus_turbo::cardano::network {
    inline bool atomic_set_true(std::atomic_bool &av, const std::function<bool()> &on_update)
    {
        for (;;) {
            if (av.load(std::memory_order_relaxed))
                return false;
            bool exp = false;
            if (!av.compare_exchange_strong(exp, true, std::memory_order_acquire, std::memory_order_relaxed)) [[unlikely]]
                return false;
            return on_update();
        }
    }

    struct multiplexer::impl {
        impl(connection_ptr &&conn, multiplexer_config_t &&mcfg):
            _conn { std::move(conn) },
            _config { std::move(mcfg) } {
            const auto it = _config.find(mini_protocol::handshake);
            if (it == _config.end()) [[unlikely]]
                throw error("mini_protocol::handshake must be set in any multiplexer config!");
            miniprotocol::handshake::result_t dummy_res {};
            const auto [p_it, created] = _protocols.try_emplace(it->first, it->second(dummy_res));
            dynamic_cast<miniprotocol::handshake::observer_t &>(*p_it->second.observer.get()).on_success([&](const auto &res) {
                logger::info("handshake succeeded with version: {}", res.version);
                for (const auto &[mp, factory]: _config) {
                    if (mp != mini_protocol::handshake) {
                        const auto [it, created] = _protocols.try_emplace(mp, factory(res));
                        if (!created) [[unlikely]]
                            throw error(fmt::format("mini protocol has already been registered with the connection: {}", mp));
                    }
                }
            });
        }

        bool try_send(const mini_protocol mp, data_generator_t &&generator)
        {
            _check_state();
            const auto p_it = _protocols.find(mp);
            if (p_it == _protocols.end()) [[unlikely]]
                return false;
            auto &p = p_it->second;
            if (!p.busy.load(std::memory_order_relaxed)) {
                bool exp = false;
                if (p.busy.compare_exchange_strong(exp, true, std::memory_order_acquire, std::memory_order_relaxed)) {
                    p.buffer.clear();
                    p.packet.clear();
                    p.generator = std::move(generator);
                    if (p.generator->resume()) {
                        p.buffer = p.generator->take();
                        _available_egress.fetch_add(1, std::memory_order_relaxed);
                        return true;
                    }
                    p.generator.reset();
                    p.busy.store(false, std::memory_order_release);
                }
            }
            return false;
        }

        bool available_egress() const
        {
            _check_state();
            return _available_egress.load(std::memory_order_relaxed) > 0;
        }

        bool available_ingress() const
        {
            _check_state();
            return _conn->available_ingress() > 0;
        }

        void process_egress(op_observer_ptr observer)
        {
            _check_state();
            if (available_egress()) {
                {
                    std::scoped_lock lk { _send_observer_mutex };
                    if (!_send_observer) [[likely]] {
                        _send_observer = std::move(observer);
                    } else {
                        logger::warn("multiplexer::process_egress called while already in progress");
                        observer->stopped();
                    }
                }
                _send_next();
            }
        }

        void process_ingress(op_observer_ptr observer)
        {
            _check_state();
            if (available_ingress()) {
                {
                    std::scoped_lock lk { _recv_observer_mutex };
                    if (!_recv_observer) [[likely]] {
                        _recv_observer = std::move(observer);
                    } else {
                        logger::warn("multiplexer::process_ingress called while already in progress");
                        observer->stopped();
                    }
                }
                _recv_next();
            }
        }

        state_t state() const
        {
            mutex::scoped_lock lk { _state_mutex };
            return _state;
        }
    private:
        struct protocol_data {
            protocol_observer_ptr observer;
            std::atomic_bool busy { false };
            std::optional<data_generator_t> generator {};
            // the last item returned from the generator or empty
            uint8_vector buffer {};
            // the last packet created from the most recent buffer
            uint8_vector packet {};

            protocol_data(const protocol_observer_ptr &o):
                observer { o }
            {
            }

            bool empty() const noexcept
            {
                return static_cast<int>(!generator.has_value()) & static_cast<int>(buffer.empty()) & static_cast<int>(packet.empty());
            }
        };

        struct my_observer_t: op_observer_t {
            my_observer_t(impl &parent):
                _parent { parent }
            {
            }

            void failed(const std::string_view err) override
            {
                _parent._on_failed(err);
            }

            void stopped() override
            {
                _parent._on_stopped();
            }
        protected:
            impl &_parent;
        };

        struct send_observer_t: my_observer_t {
            send_observer_t(impl &parent, const mini_protocol mp):
                my_observer_t { parent },
                _mp { mp },
                _data { _parent._protocols.at(_mp) }
            {
            }

            void stopped() override
            {
                _parent._notify_send_observer(&op_observer_t::stopped);
            }

            void failed(const std::string_view err) override
            {
                _parent._notify_send_observer(&op_observer_t::failed, err);
            }

            void done() override
            {
                const auto packet_sz = _data.packet.size() - sizeof(segment_info);
                _data.buffer.erase(_data.buffer.begin(), _data.buffer.begin() + packet_sz);
                _data.packet.clear();
                if (_data.buffer.empty()) {
                    if (_data.generator->resume()) {
                        _data.buffer = _data.generator->take();
                        if (_data.buffer.empty()) [[unlikely]] {
                            _parent._notify_send_observer(&op_observer_t::failed, "the provided generator returned an empty buffer!"sv);
                            return;
                        }
                    } else {
                        _data.generator.reset();
                        _data.busy.store(false, std::memory_order_release);
                        _parent._available_egress.fetch_sub(1, std::memory_order_relaxed);
                    }
                }
                _parent._notify_send_observer(&op_observer_t::done);
            }
        private:
            const mini_protocol _mp;
            protocol_data &_data;
        };

        struct receive_header_observer_t: my_observer_t {
            receive_header_observer_t(impl &parent): my_observer_t { parent }
            {
            }

            void stopped() override
            {
                logger::error("multiplexer::receive_header stopped");
                std::scoped_lock lk { _parent._recv_observer_mutex };
                if (_parent._recv_observer) {
                    _parent._recv_observer->stopped();
                    _parent._recv_observer.reset();
                }
            }

            void failed(const std::string_view err) override
            {
                logger::error("multiplexer::receive_header failed: {}", err);
                std::scoped_lock lk { _parent._recv_observer_mutex };
                if (_parent._recv_observer) {
                    _parent._recv_observer->failed(err);
                    _parent._recv_observer.reset();
                }
            }

            void done() override
            {
                _parent._recv_payload.resize(_parent._recv_header.payload_size());
                _parent._conn->async_read(_parent._recv_payload, _parent._recv_payload_observer);
            }
        };

        struct receive_payload_observer_t: my_observer_t {
            receive_payload_observer_t(impl &parent): my_observer_t { parent }
            {
            }

            void stopped() override
            {
                logger::error("multiplexer::receive_payload stopped");
                if (_parent._recv_observer) {
                    _parent._recv_observer->stopped();
                    _parent._recv_observer.reset();
                }
            }

            void failed(const std::string_view err) override
            {
                logger::error("multiplexer::receive_payload failed: {}", err);
                std::scoped_lock lk { _parent._recv_observer_mutex };
                if (_parent._recv_observer) {
                    _parent._recv_observer->failed(err);
                    _parent._recv_observer.reset();
                }
            }

            void done() override
            {
                const auto mp_id = _parent._recv_header.mini_protocol_id();
                const auto p_it = _parent._protocols.find(mp_id);
                if (p_it == _parent._protocols.end()) [[unlikely]]
                    throw error(fmt::format("a client has requested an unsupported mini protocol: {}", mp_id));
                p_it->second.observer->data(_parent._recv_payload, [&, mp_id](data_generator_t &&gen) {
                    if (!_parent.try_send(mp_id, std::move(gen))) {
                        const auto msg = fmt::format("mini protocol {} can't schedule data submission while another one is in progress!", mp_id);
                        logger::warn(msg);
                        throw error(msg);
                    }
                });
                std::scoped_lock lk { _parent._recv_observer_mutex };
                if (_parent._recv_observer) {
                    _parent._recv_observer->done();
                    _parent._recv_observer.reset();
                }
            }
        };

        using protocol_map = std::map<mini_protocol, protocol_data>;

        connection_ptr _conn;
        multiplexer_config_t _config;
        protocol_map _protocols {};
        std::atomic_size_t _available_egress { 0 };

        mutable std::mutex _state_mutex alignas(mutex::alignment) {};
        state_t _state { ok_t {} };

        // sender state
        mutable std::mutex _send_observer_mutex alignas(mutex::alignment) {};
        op_observer_ptr _send_observer {};
        protocol_map::iterator _next_protocol = _protocols.end();

        // receiver state
        mutable std::mutex _recv_observer_mutex alignas(mutex::alignment) {};
        op_observer_ptr _recv_observer {};
        segment_info _recv_header {};
        uint8_vector _recv_payload {};
        op_observer_ptr _recv_hdr_observer = std::make_shared<receive_header_observer_t>(*this);
        op_observer_ptr _recv_payload_observer = std::make_shared<receive_payload_observer_t>(*this);

        template<typename ...Args>
        void _notify_send_observer(void (op_observer_t::*method)(Args...), Args... args)
        {
            {
                std::scoped_lock lk { _send_observer_mutex };
                if (_send_observer) {
                    ((*_send_observer).*method)(std::forward<Args>(args)...);
                    _send_observer.reset();
                }
            }
            if constexpr (sizeof...(args) > 0) {
                if (method == &op_observer_t::failed) [[unlikely]] {
                    _state = failed_t {};
                    _available_egress.fetch_sub(1, std::memory_order_relaxed);
                }
            } else {
                if (method == &op_observer_t::stopped) {
                    _state = stopped_t {};
                    _available_egress.fetch_sub(1, std::memory_order_relaxed);
                }
            }
        }

        void _recv_next()
        {
            if (std::holds_alternative<ok_t>(_state)) [[likely]] {
                _conn->async_read(write_buffer { reinterpret_cast<uint8_t *>(&_recv_header), sizeof(_recv_header) }, _recv_hdr_observer);
            }
        }

        void _send_next()
        {
            // the constructor ensures that _protocols is never empty!
            for (size_t i = 0; i < _protocols.size(); ++i) {
                if (_next_protocol != _protocols.end())
                    ++_next_protocol;
                if (_next_protocol == _protocols.end())
                    _next_protocol = _protocols.begin();
                const auto &p_id = _next_protocol->first;
                auto &p_data = _next_protocol->second;
                if (p_data.busy.load(std::memory_order_acquire)) {
                    // once the buffer is non-empty, the sender owns the resource
                    if (!p_data.buffer.empty() && p_data.packet.empty()) {
                        const auto sz = std::min(_next_protocol->second.buffer.size(), segment_info::max_payload_size);
                        const segment_info hdr { numeric_cast<uint32_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())), channel_mode::responder, _next_protocol->first, numeric_cast<uint16_t>(sz) };
                        _next_protocol->second.packet.clear();
                        _next_protocol->second.packet.reserve(sizeof(hdr) + sz);
                        _next_protocol->second.packet << buffer::from(hdr) << static_cast<buffer>(_next_protocol->second.buffer).subbuf(0, sz);
                        _conn->async_write(_next_protocol->second.packet, std::make_shared<send_observer_t>(*this, p_id));
                        return;
                    }
                }
            }
            _notify_send_observer(&op_observer_t::done);
        }

        void _check_state() const
        {
            mutex::scoped_lock lk { _state_mutex };
            std::visit([&](const auto &sv) {
                using T = std::decay_t<decltype(sv)>;
                if constexpr (std::is_same_v<T, failed_t>) {
                    throw error(fmt::format("the communication channel has failed: {}", sv.err));
                } else if constexpr (std::is_same_v<T, stopped_t>) {
                    throw error(fmt::format("the communication channel has been stopped"));
                } else {
                    // ok do nothing
                }
            }, _state);
        }

        void _on_failed(const std::string_view err)
        {
            mutex::scoped_lock lk { _state_mutex };
            // do not override the original failure, overrides stopped state
            if (!std::holds_alternative<failed_t>(_state)) [[likely]] {
                _state = failed_t { std::string { err } };
            } else {
                logger::warn("a failure when the connection has already failed: {}", err);
            }
        }

        void _on_stopped()
        {
            mutex::scoped_lock lk { _state_mutex };
            if (std::holds_alternative<ok_t>(_state)) [[likely]] {
                _state = stopped_t {};
            } else {
                logger::warn("a broken connection has been additionally cancelled!");
            }
        }
    };

    multiplexer::multiplexer(connection_ptr &&conn, multiplexer_config_t &&mcfg):
        _impl { std::make_unique<impl>(std::move(conn), std::move(mcfg)) }
    {
    }

    multiplexer::~multiplexer() =default;

    bool multiplexer::try_send(const mini_protocol mp, data_generator_t &&generator)
    {
        return _impl->try_send(mp, std::move(generator));
    }

    bool multiplexer::try_send(mini_protocol mp, const buffer data)
    {
        return _impl->try_send(mp, [](uint8_vector data_copy) -> data_generator_t {
            co_yield std::move(data_copy);
        }(data));
    }

    bool multiplexer::alive() const
    {
        return std::holds_alternative<ok_t>(_impl->state());
    }

    bool multiplexer::available_egress() const
    {
        return _impl->available_egress();
    }

    bool multiplexer::available_ingress() const
    {
        return _impl->available_ingress();
    }

    multiplexer::state_t multiplexer::state() const
    {
        return _impl->state();
    }

    void multiplexer::process_egress(op_observer_ptr observer)
    {
        _impl->process_egress(std::move(observer));
    }

    void multiplexer::process_ingress(op_observer_ptr observer)
    {
        _impl->process_ingress(std::move(observer));
    }
}
