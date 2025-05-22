/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/test.hpp>
#include <dt/cardano/network/common.hpp>
#include <dt/cardano.hpp>
#include <dt/peer-selection.hpp>

namespace {
    using namespace daedalus_turbo;
    using namespace daedalus_turbo::cardano;
    using namespace daedalus_turbo::cardano::network;
}

suite cardano_network_suite = [] {
    "cardano::network::common"_test = [] {
        const auto &cfg = cardano::config::get();
        cfg.shelley_start_epoch(208);
        "segment_info"_test = [] {
            const segment_info info { 0x0123ABCD, channel_mode::initiator, mini_protocol::chain_sync, 12345 };
            const auto exp = byte_array<8>::from_hex("0123ABCD00023039");
            const buffer act { reinterpret_cast<const uint8_t*>(&info), sizeof(info) };
            test_same(exp, act);
            test_same(channel_mode::initiator, info.mode());
            test_same(mini_protocol::chain_sync, info.mini_protocol_id());
            test_same(12345, info.payload_size());
        };
        const network::address addr = peer_selection_simple::get().next_cardano();
        client_manager &ccm = client_manager_async::get();
        "find_tip"_test = [&] {
            auto c = ccm.connect(addr);
            // run a process cycle without requests to test that successive process calls work
            c->process();
            client::find_response resp {};
            c->find_tip([&](client::find_response &&r) {
                resp = std::move(r);
            });
            c->process();
            expect(fatal(std::holds_alternative<intersection_info_t>(resp.res)));
            expect(resp.addr == addr) << resp.addr.host << resp.addr.port;
            const auto &isect = daedalus_turbo::variant::get_nice<intersection_info_t>(resp.res);
            expect(!isect.isect);
            auto min_slot = cardano::slot::from_time(std::chrono::system_clock::now() - std::chrono::seconds { 600 }, cfg);
            expect(isect.tip.slot >= min_slot) << isect.tip.slot;
            expect(isect.tip.height >= 10'000'000) << isect.tip.height;
        };

        "find_intersection"_test = [&] {
            auto c = ccm.connect(addr);
            // run a process cycle without requests to test that successive process calls work
            c->process();
            client::find_response resp {};
            point2_list points {};
            points.emplace_back(119975873, block_hash::from_hex("5B74C3D89844B010020172ACFBFE2F8FC08D895A7CDD5CF77C7BBD853C4CFB79"));
            points.emplace_back(116812786, block_hash::from_hex("F1C8E2B970338F3E1FDDF5AF8BD2F3B648B2D5AD4FB98406A51EEA149479C83B"));
            c->find_intersection(points, [&](client::find_response &&r) {
                resp = std::move(r);
            });
            c->process();
            expect(resp.addr == addr) << resp.addr.host << resp.addr.port;
            expect(fatal(std::holds_alternative<intersection_info_t>(resp.res)));
            const auto &isect = daedalus_turbo::variant::get_nice<intersection_info_t>(resp.res);
            expect(fatal(isect.isect.has_value()));
            test_same(points[0].slot, isect.isect->slot);
            test_same(points[0].hash, isect.isect->hash);
            const auto min_slot = cardano::slot::from_time(std::chrono::system_clock::now() - std::chrono::seconds { 600 }, cfg);
            expect(isect.tip.slot >= min_slot) << isect.tip.slot;
            expect(isect.tip.height >= 10'000'000) << isect.tip.height;
        };

        "fetch_blocks"_test = [&] {
            const auto c = ccm.connect(addr);
            // run a process cycle without requests to test that successive process calls work
            parsed_block_list blocks {};
            std::optional<std::string> err {};
            const point from  { cardano::block_hash::from_hex("262C9CDDB771CEBF1A831E31895056BD1134236E594657F3059C2AF667FEACA3"), 120001846 };
            const point to { cardano::block_hash::from_hex("AC262A565E7A0190045DE0BE58AC84669C434786A42518BE097F9F0CEC642058"), 120002096 };
            c->fetch_blocks(from, to, [&](client::block_response_t r) {
                return std::visit([&](auto &&rv) -> bool {
                    using T = std::decay_t<decltype(rv)>;
                    if constexpr (std::is_same_v<T, client::error_msg>) {
                        logger::error("fetch_blocks error: {}", rv);
                        return false;
                    } else if constexpr (std::is_same_v<T, client::msg_block_t>) {
                        auto blk = std::make_unique<parsed_block>(rv.bytes);
                        logger::debug("received block {} {}", blk->blk->hash(), blk->blk->slot());
                        blocks.emplace_back(std::move(blk));
                        return true;
                    } else {
                        logger::error("unsupported message: {}", typeid(T).name());
                        return false;
                    }
                }, std::move(r));
            });
            c->process();
            expect(!err);
            test_same(blocks.size(), 10);
        };

        "fetch_headers"_test = [&] {
            auto c = ccm.connect(addr);
            // run a process cycle without requests to test that successive process calls work
            client::header_response resp {};
            point2_list points {};
            points.emplace_back(119975873, block_hash::from_hex("5B74C3D89844B010020172ACFBFE2F8FC08D895A7CDD5CF77C7BBD853C4CFB79"));
            c->fetch_headers(points, 10, [&](auto &&r) {
                resp = std::move(r);
            });
            c->process();
            expect(resp.addr == addr) << resp.addr.host << resp.addr.port;
            expect(static_cast<bool>(resp.intersect));
            if (resp.intersect)
                expect(*resp.intersect == points.front());
            expect(std::holds_alternative<header_list>(resp.res));
            if (std::holds_alternative<header_list>(resp.res)) {
                const auto &headers = std::get<header_list>(resp.res);
                expect(headers.size() == 10_ull);
                auto prev_slot = points.front().slot;
                for (const auto &hdr: headers) {
                    expect(hdr.slot >= prev_slot);
                    prev_slot = hdr.slot;
                }
            } else {
                logger::warn("client error: {}", std::get<client::error_msg>(resp.res));
            }
        };

        "fetch_headers byron"_test = [&] {
            auto c = ccm.connect(addr);
            point start_point { cardano::block_hash::from_hex("89D9B5A5B8DDC8D7E5A6795E9774D97FAF1EFEA59B2CAF7EAF9F8C5B32059DF4"), 0 };
            const auto [hdrs, tip] = c->fetch_headers_sync(start_point, 1);
            expect(!hdrs.empty());
            expect(hdrs.front().slot == 0);
            expect(hdrs.front().hash == cardano::block_hash::from_hex("F0F7892B5C333CFFC4B3C4344DE48AF4CC63F55E44936196F365A9EF2244134F"));
        };

        "fetch_headers shelley"_test = [&] {
            auto c = ccm.connect(addr);
            point start_point { cardano::block_hash::from_hex("F8084C61B6A238ACEC985B59310B6ECEC49C0AB8352249AFD7268DA5CFF2A457"), 4492799 };
            const auto [hdrs, tip] = c->fetch_headers_sync(start_point, 1);
            expect(!hdrs.empty());
            expect(hdrs.front().slot == 4492800);
            expect(hdrs.front().hash == cardano::block_hash::from_hex("AA83ACBF5904C0EDFE4D79B3689D3D00FCFC553CF360FD2229B98D464C28E9DE"))
                << fmt::format("{}", hdrs.front().hash);
        };

        "fetch_headers from scratch"_test = [&] {
            auto c = ccm.connect(addr);
            // run a process cycle without requests to test that successive process calls work
            client::header_response resp {};
            point2_list points {};
            c->fetch_headers(points, 10, [&](auto &&r) {
                resp = std::move(r);
            });
            c->process();
            expect(resp.addr == addr) << resp.addr.host << resp.addr.port;
            expect(!static_cast<bool>(resp.intersect));
            expect(std::holds_alternative<header_list>(resp.res));
            if (std::holds_alternative<header_list>(resp.res)) {
                const auto &headers = std::get<header_list>(resp.res);
                expect(headers.size() == 10_ull);
                uint64_t prev_slot = 0;
                for (const auto &hdr: headers) {
                    expect(hdr.slot >= prev_slot);
                    prev_slot = hdr.slot;
                }
            } else {
                logger::warn("client error: {}", std::get<client::error_msg>(resp.res));
            }
        };
    };
};