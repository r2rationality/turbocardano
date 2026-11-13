/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/network/message-buffer.hpp>
#include <turbo/cbor/encoder.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cardano::network;
}

suite cardano_network_message_buffer_suite = [] {
    "cardano::network::message_buffer"_test = [] {
        "every split and bytewise delivery"_test = [] {
            for (const auto hex: {
                "00", "40", "60", "80", "A0", "9FFF", "BFFF", "5FFF", "7FFF",
                "1818", "190100", "1A00010000", "1B0000000100000000", "3818",
                "83000102", "A201818002C2420102", "9F01BF0203FFFF", "5F4201024103FF",
                "7F6161626263FF", "F93C00", "FA3F800000"
            }) {
                const auto bytes = uint8_vector::from_hex(hex);
                for (size_t split = 0; split < bytes.size(); ++split) {
                    message_buffer_t messages;
                    messages.append(static_cast<buffer>(bytes).subbuf(0, split));
                    expect(!messages.next()) << hex;
                    messages.append(static_cast<buffer>(bytes).subbuf(split));
                    const auto message = messages.next();
                    expect(fatal(message.has_value())) << hex;
                    expect_equal(*message, static_cast<buffer>(bytes));
                    expect(messages.empty());
                    expect(!messages.next());
                }
                message_buffer_t messages;
                for (size_t i = 0; i < bytes.size(); ++i) {
                    messages.append(static_cast<buffer>(bytes).subbuf(i, 1));
                    const auto message = messages.next();
                    if (i + 1 < bytes.size()) {
                        expect(!message) << hex;
                    } else {
                        expect(fatal(message.has_value())) << hex;
                        expect_equal(*message, static_cast<buffer>(bytes));
                    }
                }
            }
        };
        "coalesced messages retain a partial suffix"_test = [] {
            message_buffer_t messages { 3 };
            messages.append(uint8_vector::from_hex("8100810119"));
            const auto first = messages.next();
            const auto second = messages.next();
            expect(fatal(first.has_value() && second.has_value()));
            expect_equal(*first, static_cast<buffer>(uint8_vector::from_hex("8100")));
            expect_equal(*second, static_cast<buffer>(uint8_vector::from_hex("8101")));
            expect(!messages.next());
            messages.append(uint8_vector::from_hex("01008102"));
            for (const auto hex: { "190100", "8102" }) {
                const auto message = messages.next();
                expect(fatal(message.has_value()));
                expect_equal(*message, static_cast<buffer>(uint8_vector::from_hex(hex)));
            }
            expect(messages.empty());
        };
        "only committed bytes are readable"_test = [] {
            message_buffer_t messages { 3 };
            auto bytes = messages.prepare(3);
            bytes[0] = 0x82;
            bytes[1] = 0;
            bytes[2] = 0xFF;
            expect(messages.empty());
            expect(!messages.next());
            messages.commit(2);
            expect(!messages.empty());
            expect(!messages.next());
            bytes = messages.prepare(2);
            bytes[0] = 1;
            bytes[1] = 0xFF;
            messages.commit(1);
            const auto message = messages.next();
            expect(fatal(message.has_value()));
            expect_equal(*message, static_cast<buffer>(uint8_vector::from_hex("820001")));
            expect(messages.empty());
            expect(!messages.next());
        };
        "large opaque payload crosses mux segments"_test = [] {
            const uint8_vector payload(150'000, 0xFF);
            cbor::encoder enc;
            enc.array(2).uint(4).tag(24).bytes(payload);
            const auto &bytes = enc.cbor();
            message_buffer_t messages { bytes.size() };
            for (size_t offset = 0; offset < bytes.size();) {
                const auto size = std::min(message_buffer_t::max_segment_size, bytes.size() - offset);
                const auto dest = messages.prepare(size);
                std::memcpy(dest.data(), bytes.data() + offset, size);
                messages.commit(size);
                offset += size;
                const auto message = messages.next();
                if (offset < bytes.size()) {
                    expect(!message);
                } else {
                    expect(fatal(message.has_value()));
                    expect_equal(*message, static_cast<buffer>(bytes));
                }
            }
        };
        "decoder errors propagate instead of requesting more bytes"_test = [] {
            for (const auto hex: { "1C", "1D", "1E", "1F", "DF", "5F6100", "7F4100", "5F5F", "F820", "FB3FF0000000000000" }) {
                message_buffer_t messages;
                messages.append(uint8_vector::from_hex(hex));
                expect(throws<error>([&] { messages.next(); })) << hex;
            }
        };
        "message limits apply before dispatch"_test = [] {
            for (const auto hex: { "4400000000", "8400000000", "A200000000" }) {
                message_buffer_t messages { 4 };
                messages.append(uint8_vector::from_hex(hex));
                expect(throws<error>([&] { messages.next(); })) << hex;
            }
            message_buffer_t indefinite { 4 };
            indefinite.append(uint8_vector::from_hex("9F0000"));
            expect(!indefinite.next());
            indefinite.append(uint8_vector::from_hex("00"));
            expect(throws<error>([&] { indefinite.next(); }));

            message_buffer_t state_limit { 100 };
            state_limit.append(uint8_vector::from_hex("4400000000"));
            expect(throws<error>([&] { state_limit.next(4); }));

            message_buffer_t segment_limit;
            expect(throws<error>([&] { segment_limit.prepare(message_buffer_t::max_segment_size + 1); }));
        };
        "handshake must fit exactly one segment"_test = [] {
            message_buffer_t::check_handshake(uint8_vector::from_hex("820001"));
            for (const auto hex: { "", "82", "8200", "82000100" })
                expect(throws<error>([&] { message_buffer_t::check_handshake(uint8_vector::from_hex(hex)); }));
        };
    };
};
