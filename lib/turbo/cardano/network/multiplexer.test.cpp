/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/network/multiplexer.hpp>
#include <turbo/cardano/network/message-buffer.hpp>
#include <turbo/cardano/network/miniprotocol/blockfetch/handler.hpp>
#include <turbo/cardano/network/miniprotocol/handshake/handler.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/common/test.hpp>
#include <turbo/storage/test.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cardano;
    using namespace turbo::cardano::network;

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
            if (!out.empty())
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
            expect_equal(1, config.size());
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
            expect_equal(true, m.try_send(mini_protocol::handshake, handshake));
            while (m.alive() && (m.available_ingress() || m.available_egress())) {
                m.process_egress();
                m.process_ingress();
            }
            expect_equal(1, outputs.size());
            expect_equal(1, obs->msgs.size());
            expect_equal(0, obs->errs.size());
            expect_equal(0, obs->num_stops);
            expect_equal(handshake, static_cast<buffer>(outputs.at(0)).subbuf(sizeof(segment_info)));
            expect_equal(inputs.at(1), obs->msgs.at(0));
        };
        "multi-protocols - receive"_test = [] {
            mock_socket::payload_list outputs {};
            mock_socket::payload_list inputs {
                segment_info { 1, channel_mode::initiator, mini_protocol::handshake, 3 }.read_buf(),
                uint8_vector::from_hex("820001"),
                segment_info { 1, channel_mode::initiator, mini_protocol::handshake, 3 }.read_buf(),
                uint8_vector::from_hex("820203"),
                segment_info { 1, channel_mode::initiator, mini_protocol::chain_sync, 3 }.read_buf(),
                uint8_vector::from_hex("820001"),
                segment_info { 1, channel_mode::initiator, mini_protocol::chain_sync, 3 }.read_buf(),
                uint8_vector::from_hex("820203")
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
            expect_equal(0, hshake_obs->errs.size());
            expect_equal(0, hshake_obs->num_stops);
            expect(fatal(expect_equal(2, hshake_obs->msgs.size())));
            expect_equal(inputs.at(1), hshake_obs->msgs.at(0));
            expect_equal(inputs.at(3), hshake_obs->msgs.at(1));

            expect_equal(0, csync_obs->errs.size());
            expect_equal(0, csync_obs->num_stops);
            expect(fatal(expect_equal(2, csync_obs->msgs.size())));
            expect_equal(inputs.at(5), csync_obs->msgs.at(0));
            expect_equal(inputs.at(7), csync_obs->msgs.at(1));
        };

        "fragmented and coalesced messages on interleaved protocols"_test = [] {
            mock_socket::payload_list outputs;
            const mock_socket::payload_list inputs {
                segment_info { 1, channel_mode::initiator, mini_protocol::chain_sync, 2 }.read_buf(),
                uint8_vector::from_hex("8300"),
                segment_info { 1, channel_mode::initiator, mini_protocol::keep_alive, 1 }.read_buf(),
                uint8_vector::from_hex("81"),
                segment_info { 1, channel_mode::initiator, mini_protocol::chain_sync, 4 }.read_buf(),
                uint8_vector::from_hex("01028103"),
                segment_info { 1, channel_mode::initiator, mini_protocol::keep_alive, 1 }.read_buf(),
                uint8_vector::from_hex("00")
            };
            const auto handshake = std::make_shared<mock_observer>();
            const auto chainsync = std::make_shared<mock_observer>();
            const auto keepalive = std::make_shared<mock_observer>();
            multiplexer m { std::make_unique<mock_socket>(inputs, outputs), {
                { mini_protocol::handshake, [=](const auto &) { return handshake; } },
                { mini_protocol::chain_sync, [=](const auto &) { return chainsync; } },
                { mini_protocol::keep_alive, [=](const auto &) { return keepalive; } }
            } };
            handshake->handshake_success();
            m.process_ingress();
            m.process_ingress();
            expect(chainsync->msgs.empty());
            expect(keepalive->msgs.empty());
            m.process_ingress();
            expect(chainsync->msgs == std::vector<uint8_vector> {
                uint8_vector::from_hex("83000102"), uint8_vector::from_hex("8103") });
            expect(keepalive->msgs.empty());
            m.process_ingress();
            expect(keepalive->msgs == std::vector<uint8_vector> { uint8_vector::from_hex("8100") });
        };
        "pipelined BlockFetch requests drain without further ingress"_test = [] {
            namespace blockfetch = miniprotocol::blockfetch;
            const auto cr = std::make_shared<chunk_registry>(storage::sample_registry_path(),
                chunk_registry_settings_t { .mode=chunk_registry::mode::store });
            const std::array<point2, 2> points { cr->cbegin()->point(), *cr->tip() };
            for (const bool coalesced: { false, true }) {
                mock_socket::payload_list inputs, outputs;
                cbor::encoder requests;
                const auto add_segment = [&] {
                    inputs.emplace_back(segment_info { 1, channel_mode::initiator, mini_protocol::block_fetch,
                        numeric_cast<uint16_t>(requests.cbor().size()) }.read_buf());
                    inputs.emplace_back(std::move(requests.cbor()));
                };
                for (const auto &point: points) {
                    blockfetch::msg_request_range_t { point, point }.to_cbor(requests);
                    if (!coalesced)
                        add_segment();
                }
                if (coalesced)
                    add_segment();
                const auto handshake = std::make_shared<mock_observer>();
                multiplexer m { std::make_unique<mock_socket>(inputs, outputs), {
                    { mini_protocol::handshake, [=](const auto &) { return handshake; } },
                    { mini_protocol::block_fetch, [=](const auto &) { return std::make_shared<blockfetch::handler>(cr); } }
                } };
                handshake->handshake_success();
                while (m.available_ingress())
                    m.process_ingress();
                for (size_t i = 0; i < 100; ++i) {
                    m.poll();
                    if (!m.available_egress())
                        break;
                    m.process_egress();
                }
                expect(!m.available_egress());
                message_buffer_t responses { 2'500'000 };
                std::vector<uint64_t> types;
                size_t blocks = 0;
                for (const auto &packet: outputs) {
                    responses.append(static_cast<buffer>(packet).subbuf(sizeof(segment_info)));
                    while (const auto bytes = responses.next()) {
                        auto parsed = cbor::zero2::parse(*bytes);
                        types.emplace_back(parsed.get().array().read().uint());
                        if (types.back() == 4) {
                            auto block_cbor = cbor::zero2::parse(*bytes);
                            const auto msg = blockfetch::msg_t::from_cbor(block_cbor.get());
                            const parsed_block block { std::get<blockfetch::msg_block_t>(msg).bytes };
                            expect_equal(block.blk->point2(), points.at(blocks++));
                        }
                    }
                }
                expect(responses.empty());
                expect(types == std::vector<uint64_t> { 2, 4, 5, 2, 4, 5 });
                expect_equal(blocks, points.size());
            }
        };
        "poll waits for protocol readiness before draining queued requests"_test = [] {
            struct delayed_observer: mock_observer {
                bool ready = false;
                bool release = false;

                bool can_receive() const override { return ready; }
                void poll(const protocol_send_func &) override { ready = release; }
            };
            mock_socket::payload_list outputs;
            const mock_socket::payload_list inputs {
                segment_info { 1, channel_mode::initiator, mini_protocol::chain_sync, 2 }.read_buf(),
                uint8_vector::from_hex("8100")
            };
            const auto handshake = std::make_shared<mock_observer>();
            const auto handler = std::make_shared<delayed_observer>();
            multiplexer m { std::make_unique<mock_socket>(inputs, outputs), {
                { mini_protocol::handshake, [=](const auto &) { return handshake; } },
                { mini_protocol::chain_sync, [=](const auto &) { return handler; } }
            } };
            handshake->handshake_success();
            m.process_ingress();
            m.poll();
            expect(handler->msgs.empty());
            expect(!m.available_ingress());
            handler->release = true;
            m.poll();
            expect(handler->msgs == std::vector<uint8_vector> { inputs.at(1) });
        };
        "incomplete handshake is rejected before dispatch"_test = [] {
            mock_socket::payload_list outputs;
            const mock_socket::payload_list inputs {
                segment_info { 1, channel_mode::initiator, mini_protocol::handshake, 1 }.read_buf(),
                uint8_vector::from_hex("82")
            };
            const auto handshake = std::make_shared<mock_observer>();
            multiplexer m { std::make_unique<mock_socket>(inputs, outputs), {
                { mini_protocol::handshake, [=](const auto &) { return handshake; } }
            } };
            expect(throws<error>([&] { m.process_ingress(); }));
            expect(handshake->msgs.empty());
        };
        "handler incomplete CBOR errors propagate after framing"_test = [] {
            struct failing_observer: mock_observer {
                size_t calls = 0;

                void data(buffer, const protocol_send_func &) override
                {
                    ++calls;
                    throw cbor::zero2::incomplete_error{};
                }
            };
            mock_socket::payload_list outputs;
            const mock_socket::payload_list inputs {
                segment_info { 1, channel_mode::initiator, mini_protocol::chain_sync, 1 }.read_buf(),
                uint8_vector::from_hex("81"),
                segment_info { 1, channel_mode::initiator, mini_protocol::chain_sync, 1 }.read_buf(),
                uint8_vector::from_hex("00")
            };
            const auto handshake = std::make_shared<mock_observer>();
            const auto handler = std::make_shared<failing_observer>();
            multiplexer m { std::make_unique<mock_socket>(inputs, outputs), {
                { mini_protocol::handshake, [=](const auto &) { return handshake; } },
                { mini_protocol::chain_sync, [=](const auto &) { return handler; } }
            } };
            handshake->handshake_success();
            m.process_ingress();
            expect_equal(handler->calls, 0);
            expect(throws<cbor::zero2::incomplete_error>([&] { m.process_ingress(); }));
            expect_equal(handler->calls, 1);
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
            expect_equal(true, m.try_send(mini_protocol::chain_sync, p2_data1));
            const auto p1_data1 = uint8_vector::from_hex("0011");
            expect_equal(true, m.try_send(mini_protocol::handshake, p1_data1));

            m.process_egress();

            expect_equal(1, outputs.size());
            expect_equal(p1_data1, static_cast<buffer>(outputs.at(0)).subbuf(sizeof(segment_info)));

            const auto p1_data2 = uint8_vector::from_hex("3344");
            expect_equal(true, m.try_send(mini_protocol::handshake, p1_data2));
            m.process_egress();
            m.process_ingress();
            expect_equal(2, outputs.size());
            expect_equal(p2_data1, static_cast<buffer>(outputs.at(1)).subbuf(sizeof(segment_info)));

            const auto p2_data2 = uint8_vector::from_hex("CCDD");
            expect_equal(true, m.try_send(mini_protocol::chain_sync, p2_data2));
            
            while (m.state() && (m.available_ingress() || m.available_egress())) {
                m.process_egress();
                m.process_ingress();
            }

            expect_equal(4, outputs.size());
            expect_equal(p1_data2, static_cast<buffer>(outputs.at(2)).subbuf(sizeof(segment_info)));
            expect_equal(p2_data2, static_cast<buffer>(outputs.at(3)).subbuf(sizeof(segment_info)));
        };
        "chunk sending preserves bytes across capacity reuse and growth"_test = [] {
            mock_socket::payload_list outputs;
            const mock_socket::payload_list inputs;
            const auto handshake = std::make_shared<mock_observer>();
            multiplexer m { std::make_unique<mock_socket>(inputs, outputs), {
                { mini_protocol::handshake, [=](const auto &) { return handshake; } },
                { mini_protocol::block_fetch, [](const auto &) { return std::make_shared<mock_observer>(); } }
            } };
            handshake->handshake_success();

            const size_t capacity = segment_info::max_payload_size + 31;
            mock_socket::payload_list chunks;
            size_t packet_count = 0;
            for (const auto size: { capacity, size_t { 17 }, capacity - 3, capacity + 7 }) {
                auto &chunk = chunks.emplace_back(size);
                for (size_t i = 0; i < size; ++i)
                    chunk[i] = static_cast<uint8_t>(i * 17 + chunks.size() * 31);
                packet_count += (size + segment_info::max_payload_size - 1) / segment_info::max_payload_size;
            }
            auto generator = [](mock_socket::payload_list bytes) -> data_generator_t {
                for (auto &chunk: bytes)
                    co_yield std::move(chunk);
            }(chunks);
            expect(m.try_send(mini_protocol::block_fetch, std::move(generator)));
            for (size_t i = 0; i < packet_count && m.available_egress(); ++i)
                m.process_egress();
            expect(!m.available_egress());
            expect(fatal(expect_equal(outputs.size(), packet_count)));

            size_t packet_index = 0;
            for (const auto &chunk: chunks) {
                for (size_t offset = 0; offset < chunk.size();) {
                    const auto size = std::min(segment_info::max_payload_size, chunk.size() - offset);
                    const auto &packet = outputs.at(packet_index++);
                    expect(fatal(expect_equal(packet.size(), sizeof(segment_info) + size)));
                    const auto header = load_unaligned<segment_info>(packet.data());
                    expect(header.mode() == channel_mode::responder);
                    expect(header.mini_protocol_id() == mini_protocol::block_fetch);
                    expect_equal(header.payload_size(), size);
                    expect_equal(static_cast<buffer>(packet).subbuf(sizeof(segment_info)),
                        static_cast<buffer>(chunk).subbuf(offset, size));
                    offset += size;
                }
            }
        };
        "failed"_test = [] {
            const auto handshake_obs = std::make_shared<mock_observer>();
            const multiplexer_config_t mcfg { { mini_protocol::handshake, [&](const auto &) { return handshake_obs; } } };
            { // ingress
                multiplexer m { std::make_unique<fail_socket>(), multiplexer_config_t { mcfg } };
                expect(std::holds_alternative<multiplexer::ok_t>(m.state()));
                m.process_ingress();
                expect_equal(true, m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011")));
            }
            { // egress
                multiplexer m { std::make_unique<fail_socket>(), multiplexer_config_t { mcfg } };
                expect(std::holds_alternative<multiplexer::ok_t>(m.state()));
                expect_equal(true, m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011")));
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
                expect_equal(true, m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011")));
            }
            { // egress
                multiplexer m { std::make_unique<stop_socket>(), multiplexer_config_t { mcfg } };
                expect(std::holds_alternative<multiplexer::ok_t>(m.state()));
                expect_equal(true, m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011")));
                m.process_egress();
                expect(std::holds_alternative<multiplexer::stopped_t>(m.state()));
                expect(throws([&] {
                    m.try_send(mini_protocol::handshake, uint8_vector::from_hex("0011"));
                }));
            }
        };
    };
};
