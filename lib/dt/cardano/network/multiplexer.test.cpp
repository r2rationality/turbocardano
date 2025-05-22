/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/network/multiplexer.hpp>
#include <dt/common/test.hpp>

namespace {
    using namespace daedalus_turbo;
    using namespace daedalus_turbo::cardano;
    using namespace daedalus_turbo::cardano::network;

    struct mock_socket: connection {
        using payload_list = std::vector<uint8_vector>;

        mock_socket(const payload_list &inputs, payload_list &outputs):
            _reads { inputs }, _writes { outputs }
        {
        }

        size_t available_ingress() const override
        {
            size_t num_bytes = 0;
            for (size_t i = _num_reads; i < _reads.size(); ++i) {
                num_bytes += _reads[i].size();
            }
            return num_bytes;
        }

        void async_read(const write_buffer out, const op_observer_ptr observer) override
        {
            if (_num_reads >= _reads.size()) [[unlikely]] {
                return observer->failed("no more data!");
            }
            const auto &in = _reads[_num_reads++];;
            if (out.size() != in.size()) {
                return observer->failed(fmt::format("async_read requested {} bytes but the next data set has {}!", out.size(), in.size()));
            }
            memcpy(out.data(), in.data(), out.size());
            observer->done();
        }

        void async_write(const buffer in, const op_observer_ptr observer) override
        {
            _writes.emplace_back(in);
            observer->done();
        }
    private:
        const payload_list &_reads;
        payload_list &_writes;
        size_t _num_reads = 0;
    };

    struct fail_socket: connection {
        size_t available_ingress() const override
        {
            return 0;
        }

        void async_read(const write_buffer, const op_observer_ptr observer) override
        {
            observer->failed("read failed");
        }

        void async_write(const buffer, const op_observer_ptr observer) override
        {
            observer->failed("write failed");
        }
    };

    struct stop_socket: fail_socket {
        void async_read(const write_buffer, const op_observer_ptr observer) override
        {
            observer->stopped();
        }

        void async_write(const buffer, const op_observer_ptr observer) override
        {
            observer->stopped();
        }
    };

    struct mock_observer: miniprotocol::handshake::observer_t {
        std::vector<uint8_vector> msgs {};
        std::vector<std::string> errs {};
        size_t num_stops = 0;
        std::optional<miniprotocol::handshake::on_success_func> handshake_success_f {};

        void handshake_success()
        {
            if (handshake_success_f) {
                (*handshake_success_f)({ 14, {} });
                handshake_success_f.reset();
            }
        }

        void on_success(const miniprotocol::handshake::on_success_func &f) override
        {
            handshake_success_f = f;
        }

        void data(const buffer msg, const protocol_send_func &) override
        {
            msgs.emplace_back(msg);
        }

        void failed(const std::string_view err) override
        {
            errs.emplace_back(err);
        }

        void stopped() override
        {
            ++num_stops;
        }
    };
}

suite cardano_network_multiplexer_suite = [] {
    "cardano::network::multiplexer"_test = [] {
        "constructor"_test = [] {
            mock_socket::payload_list outputs {};
            mock_socket::payload_list inputs {};
            const auto obs_factory = [](const auto &) -> protocol_observer_ptr { return std::make_shared<mock_observer>(); };
            // multiple definitions are merged into a single std::map entry
            const multiplexer_config_t config {
                { mini_protocol::handshake, obs_factory },
                { mini_protocol::handshake, obs_factory }
            };
            test_same(1, config.size());
            expect(throws([&] {
                multiplexer { std::make_unique<mock_socket>(inputs, outputs), multiplexer_config_t {}};
            }));
            expect(boost::ext::ut::v2_1_0::nothrow([&] {
                multiplexer { std::make_unique<mock_socket>(inputs, outputs), multiplexer_config_t { config }};
            }));
        };
        "handshake"_test = [] {
            mock_socket::payload_list outputs {};
            mock_socket::payload_list inputs {
                segment_info { 1, channel_mode::initiator, mini_protocol::handshake, 3 }.read_buf(),
                uint8_vector::from_hex("820001")
            };
            const auto obs = std::make_shared<mock_observer>();
            multiplexer m {
                std::make_unique<mock_socket>(inputs, outputs),
                { { mini_protocol::handshake, [obs](const auto &) -> protocol_observer_ptr { return obs; } } }
            };
            const auto handshake = uint8_vector::from_hex("00010203820001");
            test_same(true, m.try_send(mini_protocol::handshake, handshake));
            while (m.alive() && (m.available_ingress() || m.available_egress())) {
                m.process_egress();
                m.process_ingress();
            }
            test_same(1, outputs.size());
            test_same(1, obs->msgs.size());
            test_same(0, obs->errs.size());
            test_same(0, obs->num_stops);
            test_same(handshake, static_cast<buffer>(outputs.at(0)).subbuf(sizeof(segment_info)));
            test_same(inputs.at(1), obs->msgs.at(0));
        };
        "multi-protocols - receive"_test = [] {
            mock_socket::payload_list outputs {};
            mock_socket::payload_list inputs {
                segment_info { 1, channel_mode::initiator, mini_protocol::handshake, 3 }.read_buf(),
                uint8_vector::from_hex("820001"),
                segment_info { 1, channel_mode::initiator, mini_protocol::handshake, 3 }.read_buf(),
                uint8_vector::from_hex("820203"),
                segment_info { 1, channel_mode::initiator, mini_protocol::chain_sync, 3 }.read_buf(),
                uint8_vector::from_hex("AABBCC"),
                segment_info { 1, channel_mode::initiator, mini_protocol::chain_sync, 3 }.read_buf(),
                uint8_vector::from_hex("DDEEFF")
            };
            const auto hshake_obs = std::make_shared<mock_observer>();
            const auto csync_obs = std::make_shared<mock_observer>();
            multiplexer m { std::make_unique<mock_socket>(inputs, outputs), {
                    { mini_protocol::handshake, [&](const auto &) { return hshake_obs; } },
                    { mini_protocol::chain_sync, [&](const auto &) { return csync_obs; } }
                }
            };
            hshake_obs->handshake_success();
            while (m.state() && (m.available_ingress() || m.available_egress())) {
                m.process_egress();
                m.process_ingress();
            }
            test_same(0, hshake_obs->errs.size());
            test_same(0, hshake_obs->num_stops);
            expect(fatal(test_same(2, hshake_obs->msgs.size())));
            test_same(inputs.at(1), hshake_obs->msgs.at(0));
            test_same(inputs.at(3), hshake_obs->msgs.at(1));

            test_same(0, csync_obs->errs.size());
            test_same(0, csync_obs->num_stops);
            expect(fatal(test_same(2, csync_obs->msgs.size())));
            test_same(inputs.at(5), csync_obs->msgs.at(0));
            test_same(inputs.at(7), csync_obs->msgs.at(1));
        };

        "multi-protocols - send order"_test = [] {
            mock_socket::payload_list outputs {};
            mock_socket::payload_list inputs {};

            const auto hshake_obs = std::make_shared<mock_observer>();
            const auto csync_obs = std::make_shared<mock_observer>();

            multiplexer m {
                std::make_unique<mock_socket>(inputs, outputs), {
                    { mini_protocol::handshake, [&](const auto &) { return hshake_obs; } },
                    { mini_protocol::chain_sync, [&](const auto &) { return csync_obs; } }
                }
            };

            hshake_obs->handshake_success();
            const auto p2_data1 = uint8_vector::from_hex("AABB");
            test_same(true, m.try_send(mini_protocol::chain_sync, p2_data1));
            const auto p1_data1 = uint8_vector::from_hex("0011");
            test_same(true, m.try_send(mini_protocol::handshake, p1_data1));

            m.process_egress();

            test_same(1, outputs.size());
            test_same(p1_data1, static_cast<buffer>(outputs.at(0)).subbuf(sizeof(segment_info)));

            const auto p1_data2 = uint8_vector::from_hex("3344");
            test_same(true, m.try_send(mini_protocol::handshake, p1_data2));
            m.process_egress();
            m.process_ingress();
            test_same(2, outputs.size());
            test_same(p2_data1, static_cast<buffer>(outputs.at(1)).subbuf(sizeof(segment_info)));

            const auto p2_data2 = uint8_vector::from_hex("CCDD");
            test_same(true, m.try_send(mini_protocol::chain_sync, p2_data2));
            
            while (m.state() && (m.available_ingress() || m.available_egress())) {
                m.process_egress();
                m.process_ingress();
            }

            test_same(4, outputs.size());
            test_same(p1_data2, static_cast<buffer>(outputs.at(2)).subbuf(sizeof(segment_info)));
            test_same(p2_data2, static_cast<buffer>(outputs.at(3)).subbuf(sizeof(segment_info)));
        };
        "failed"_test = [] {
            const auto handshake_obs = std::make_shared<mock_observer>();
            const multiplexer_config_t mcfg { { mini_protocol::handshake, [&](const auto &) { return handshake_obs; } } };
            { // ingress
                multiplexer m { std::make_unique<fail_socket>(), multiplexer_config_t { mcfg } };
                expect(std::holds_alternative<multiplexer::ok_t>(m.state()));
                m.process_ingress();
                test_same(true, m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011")));
            }
            { // egress
                multiplexer m { std::make_unique<fail_socket>(), multiplexer_config_t { mcfg } };
                expect(std::holds_alternative<multiplexer::ok_t>(m.state()));
                test_same(true, m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011")));
                m.process_egress();
                expect(std::holds_alternative<multiplexer::failed_t>(m.state()));
                expect(throws([&] {
                    m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011"));
                }));
            }
        };
        "stopped"_test = [] {
            const auto handshake_obs = std::make_shared<mock_observer>();
            const multiplexer_config_t mcfg { { mini_protocol::handshake, [&](const auto &) { return handshake_obs; } } };
            { // ingress
                multiplexer m { std::make_unique<stop_socket>(), multiplexer_config_t { mcfg } };
                expect(std::holds_alternative<multiplexer::ok_t>(m.state()));
                // processing ingress when no data available is OK
                m.process_ingress();
                test_same(true, m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011")));
            }
            { // egress
                multiplexer m { std::make_unique<stop_socket>(), multiplexer_config_t { mcfg } };
                expect(std::holds_alternative<multiplexer::ok_t>(m.state()));
                test_same(true, m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011")));
                m.process_egress();
                expect(std::holds_alternative<multiplexer::stopped_t>(m.state()));
                expect(throws([&] {
                    m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011"));
                }));
            }
        };
    };
};
