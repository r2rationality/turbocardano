/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "handler.hpp"
#include <turbo/cardano/network/mock.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cardano;
    using namespace turbo::cardano::network;
    using namespace turbo::cardano::network::miniprotocol;

    static uint8_vector decode(const buffer bytes)
    {
        return { bytes };
    }
}

suite cardano_network_miniprotocol_handshake_suite = [] {
    "cardano::network::miniprotocol::handshake"_test = [] {
        "bad version map"_test = [] {
            expect(throws([] {
                handshake::handler h {
                    handshake::version_map {
                        { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                    },
                    20
                };
            }));
            expect(throws([] {
                handshake::handler h {
                    handshake::version_map {},
                    20
                };
            }));
        };
        "propose empty"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            enc1.array(2)
                .uint(0)
                .map(0);
            h.data(enc1.cbor(), std::ref(resp));
            expect_equal(uint8_vector::from_hex("820282008116"), resp.at(0));
        };
        "bad message"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            enc1.array(2)
                .uint(3)
                .map(1)
                    .uint(20)
                    .array(4)
                        .uint(1234)
                        .uint(1)
                        .uint(0)
                        .uint(0);
            h.data(enc1.cbor(), std::ref(resp));
            expect_equal(uint8_vector::from_hex("820283011670696E76616C696420656E636F64696E67"), resp.at(0));
        };
        "accept"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            enc1.array(2)
                .uint(0)
                .map(1)
                    .uint(20)
                    .array(4)
                        .uint(1234)
                        .s_true()
                        .uint(0)
                        .s_false();
            h.data(enc1.cbor(), std::ref(resp));
            expect_equal(uint8_vector::from_hex("830114841904D2F500F4"), resp.at(0));
        };
        "query"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            enc1.array(2)
                .uint(0)
                .map(1)
                    .uint(20)
                    .array(4)
                        .uint(1234)
                        .s_true()
                        .uint(0)
                        .s_true();
            h.data(enc1.cbor(), std::ref(resp));
            expect_equal(uint8_vector::from_hex("8203A214841904D2F400F416841904D2F400F4"), resp.at(0));
        };
        "reject encoding"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            h.data(uint8_vector::from_hex("88"), std::ref(resp));
            expect_equal(uint8_vector::from_hex("820283011670696E76616C696420656E636F64696E67"), resp.at(0));
        };
        "reject protocol"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            enc1.array(2)
                .uint(0)
                .map(1)
                    .uint(23)
                    .array(4)
                        .uint(1234)
                        .s_true()
                        .uint(0)
                        .s_false();
            h.data(enc1.cbor(), std::ref(resp));
            expect_equal(uint8_vector::from_hex("82028200821416"), resp.at(0));
        };
        "reject network magic"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            enc1.array(2)
                .uint(0)
                .map(1)
                    .uint(22)
                    .array(4)
                        .uint(123456789)
                        .s_true()
                        .uint(0)
                        .s_false();
            h.data(enc1.cbor(), std::ref(resp));
            expect_equal(uint8_vector::from_hex("8202830216783E7468652070726F706F736564206D61676963206973206E6F7420737570706F727465643A207265713A2031323334353637383920686176653A2031323334"), resp.at(0));
        };
        "reject diffusion"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            enc1.array(2)
                .uint(0)
                .map(1)
                    .uint(22)
                    .array(4)
                        .uint(1234)
                        .s_false()
                        .uint(0)
                        .s_false();
            h.data(enc1.cbor(), std::ref(resp));
            expect_equal(uint8_vector::from_hex("8202830216783961206E6567617469766520696E69746961746F725F6F6E6C795F646966667573696F6E5F6D6F6465206973206E6F7420737570706F72746564"), resp.at(0));
        };
        "reject peer sharing"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            enc1.array(2)
                .uint(0)
                .map(1)
                    .uint(22)
                    .array(4)
                        .uint(1234)
                        .s_true()
                        .uint(1)
                        .s_false();
            h.data(enc1.cbor(), std::ref(resp));
            expect_equal(uint8_vector::from_hex("8202830216781D706565725F73686172696E67206973206E6F7420737570706F72746564"), resp.at(0));
        };
        "reject query"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } },
                    { 22, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                22
            };
            mock_response_processor_t<uint8_vector> resp { decode };
            cbor::encoder enc1 {};
            enc1.array(2)
                .uint(0)
                .map(1)
                    .uint(23)
                    .array(4)
                        .uint(1234)
                        .s_true()
                        .uint(0)
                        .s_true();
            h.data(enc1.cbor(), std::ref(resp));
            expect_equal(uint8_vector::from_hex("82028200821416"), resp.at(0));
        };
        "failure reporting"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                20
            };
            h.failed("some error");
            mock_response_processor_t<uint8_vector> resp { decode };
            expect(throws([&]{ h.data(uint8_vector::from_hex(""), std::ref(resp)); }));
            expect_equal(0, resp.size());
        };
        "cancellation reporting"_test = [] {
            handshake::handler h {
                handshake::version_map {
                    { 20, handshake::node_to_node_version_data_t { 1234, false, false, false } }
                },
                20
            };
            h.stopped();
            mock_response_processor_t<uint8_vector> resp { decode };
            expect(throws([&]{ h.data(uint8_vector::from_hex(""), std::ref(resp)); }));
            expect_equal(0, resp.size());
        };
    };
};
