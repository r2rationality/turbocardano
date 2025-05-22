/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cbor/zero2.hpp>
#include "handler.hpp"
#include "messages.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::handshake {
    node_to_node_version_data_t node_to_node_version_data_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return {
            numeric_cast<uint32_t>(it.read().uint()),
            it.read().boolean(),
            numeric_cast<bool>(it.read().uint()),
            it.read().boolean()
        };
    }

    void node_to_node_version_data_t::to_cbor(cbor::encoder &enc) const
    {
        enc.array(4);
        enc.uint(network_magic);
        enc.boolean(initiator_only_diffusion_mode);
        enc.uint(peer_sharing);
        enc.boolean(query);
    }

    struct handler::impl {
        explicit impl(version_map &&versions, const uint64_t promoted_version):
            _versions { std::move(versions) },
            _promoted_version { promoted_version }
        {
            if (!_versions.contains(_promoted_version)) [[unlikely]]
                throw error("the promoted version is not in the know version list!");
        }

        void data(const buffer bytes, const protocol_send_func &send_func)
        {
            if (!_state.is<st_propose_t>()) [[unlikely]]
                throw error("handshake handler received data outside of st_start state!");
            _state = st_confirm_t {};
            cbor::encoder enc {};
            try {
                auto pv = cbor::zero2::parse(bytes);
                // the server must ignore unsupported parameters of newer versions!
                const auto msg = msg_t::from_cbor(pv.get());
                const auto &proposal = daedalus_turbo::variant::get_nice<msg_propose_versions_t>(msg);
                std::set<uint64_t> shared_versions {};
                for (const auto &[v, info]: proposal.versions) {
                    if (_versions.contains(v))
                        shared_versions.emplace(v);
                }
                if (shared_versions.empty()) {
                    return _respond(send_func, msg_refuse_t { msg_refuse_t::version_mismatch_t { _version_numbers() } });
                }
                const auto best_ver = *shared_versions.rbegin();
                const auto &req_info = proposal.versions.at(best_ver);
                const auto &have_info = _versions.at(best_ver);
                if (req_info.network_magic != have_info.network_magic) {
                    return _respond(
                        send_func,
                        msg_refuse_t {
                            msg_refuse_t::refused_t {
                                _promoted_version,
                                fmt::format("the proposed magic is not supported: req: {} have: {}", req_info.network_magic, have_info.network_magic)
                            }
                        }
                    );
                }
                if (req_info.initiator_only_diffusion_mode != true) {
                    return _respond(send_func, msg_refuse_t { msg_refuse_t::refused_t { _promoted_version, "a negative initiator_only_diffusion_mode is not supported" } });
                }
                if (req_info.peer_sharing != false) {
                    return _respond(send_func, msg_refuse_t { msg_refuse_t::refused_t { _promoted_version, "peer_sharing is not supported" } });
                }
                if (req_info.query) {
                    return _respond(send_func, msg_query_reply_t { _versions });
                }
                _result.emplace(
                    best_ver,
                    node_to_node_version_data_t { have_info.network_magic, req_info.initiator_only_diffusion_mode, req_info.peer_sharing, false }
                );
                if (_on_success)
                    (*_on_success)(*_result);
                return _respond(send_func, msg_accept_version_t { best_ver, _result->config });
            } catch (const error &ex) {
                return _respond(send_func, msg_refuse_t { msg_refuse_t::decode_error_t { _promoted_version, "invalid encoding" }});
            }
        }

        void on_success(const on_success_func &on_success)
        {
            _on_success = on_success;
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
        struct st_propose_t {};
        struct st_confirm_t {};
        struct st_done_t {};

        struct state_t: std::variant<st_propose_t, st_confirm_t, st_done_t> {
            using val_type = std::variant<st_propose_t, st_confirm_t, st_done_t>;

            state_t() =default;

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
            val_type _val { st_propose_t {} };
            std::chrono::system_clock::time_point _start = std::chrono::system_clock::now();
        };

        version_map _versions;
        uint64_t _promoted_version;
        std::optional<on_success_func> _on_success {};
        state_t _state {};
        std::optional<result_t> _result {};

        vector_t<uint64_t, cbor::encoder> _version_numbers() const
        {
            vector_t<uint64_t, cbor::encoder> res {};
            res.reserve(_versions.size());
            for (const auto &[v, info]: _versions)
                res.emplace_back(v);
            return res;
        }

        template<typename T>
        void _respond(const protocol_send_func &send_func, T &&msg)
        {
            cbor::encoder enc {};
            msg.to_cbor(enc);
            logger::info("handshake response: {}", enc.cbor());
            send_func(message_generator(std::move(msg)));
            _state = st_done_t {};
        }
    };

    handler::handler(version_map &&versions, const uint64_t promoted_ver):
        _impl { std::make_unique<impl>(std::move(versions), promoted_ver) }
    {
    }

    handler::~handler() =default;

    void handler::on_success(const on_success_func &on_success)
    {
        _impl->on_success(on_success);
    }

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
