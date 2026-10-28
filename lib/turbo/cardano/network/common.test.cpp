/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <utility>
#include <turbo/cardano/network/common.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cardano;
    using namespace turbo::cardano::network;
}

suite cardano_network_suite = [] {
    "cardano::network::common"_test = [] {
        "segment_info"_test = [] {
            const segment_info info { 0x0123ABCD, channel_mode::initiator, mini_protocol::chain_sync, 12345 };
            const auto exp = byte_array<8>::from_hex("0123ABCD00023039");
            const buffer act { reinterpret_cast<const uint8_t*>(&info), sizeof(info) };
            expect_equal(exp, act);
            expect_equal(channel_mode::initiator, info.mode());
            expect_equal(mini_protocol::chain_sync, info.mini_protocol_id());
            expect_equal(12345, info.payload_size());
        };

        // Real client/protocol coverage uses the local chain fixture in server.test.cpp.
        "fetch_headers_sync wrappers"_test = [] {
            struct header_client: client {
                using client::client;
                optional_point2_list requested_points;
                size_t requested_count = 0;
                header_response response;
            private:
                header_handler _pending;

                void _fetch_headers_impl(const optional_point2_list &points, size_t count, const header_handler &handler) override
                {
                    requested_points = points;
                    requested_count = count;
                    _pending = handler;
                }

                void _process_impl(scheduler *, asio::worker *) override
                {
                    if (auto handler = std::exchange(_pending, {}))
                        handler(std::move(response));
                }
            };
            header_client c { network::address { "fixture", "0" } };
            const point start { block_hash {}, 10 };
            const point2 next { 20, block_hash {} };
            const optional_point3 expected_tip { optional_point2 { next }, 2 };
            const auto respond = [&] {
                c.response = {};
                c.response.tip = expected_tip;
                c.response.res = header_list { next };
            };
            respond();
            const auto [headers, tip] = c.fetch_headers_sync(std::optional<point> { start }, 1);
            expect(c.requested_points == optional_point2_list { optional_point2 { start }, optional_point2 {} });
            expect_equal(c.requested_count, 1);
            expect(headers == header_list { next });
            expect_equal(tip, expected_tip);

            respond();
            const auto [origin_headers, origin_tip] = c.fetch_headers_sync(std::optional<point> {}, 1);
            expect(c.requested_points == optional_point2_list { optional_point2 {} });
            expect(origin_headers == header_list { next });
            expect_equal(origin_tip, expected_tip);
        };
    };
};
