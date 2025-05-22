/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/network/mock.hpp>
#include <dt/common/test.hpp>
#include <dt/common/variant.hpp>
#include "handler.hpp"
#include "messages.hpp"

namespace {
    namespace dt = daedalus_turbo;
    using namespace daedalus_turbo;
    using namespace daedalus_turbo::cardano;
    using namespace daedalus_turbo::cardano::network;
    using namespace daedalus_turbo::cardano::network::miniprotocol;

    template<typename T>
    uint8_vector encode(const T &val)
    {
        cbor::encoder enc {};
        val.to_cbor(enc);
        return std::move(enc.cbor());
    }

    chainsync::msg_t decode(const buffer bytes)
    {
        auto pv = cbor::zero2::parse(bytes);
        return chainsync::msg_t::from_cbor(pv.get());
    }
}

suite cardano_network_miniprotocol_chainsync_suite = [] {
    "cardano::network::miniprotocol::chainsync"_test = [] {
        const file::tmp_directory cr_empty_dir { "test-chainsync-empty-chain" };
        const auto cr_empty = std::make_shared<chunk_registry>(cr_empty_dir.path(), chunk_registry::mode::store);
        const auto cr = std::make_shared<chunk_registry>(install_path("data/chunk-registry"), chunk_registry::mode::store);

        "find_intersect empty"_test = [&] {
            chainsync::handler h { cr };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            h.data(encode(chainsync::msg_find_intersect_t {}), std::ref(resp));
            expect(fatal(resp.messages().size() == 1));
            expect(std::holds_alternative<chainsync::msg_intersect_not_found_t>(resp.at(0)));
            expect_equal(dt::variant::get_nice<chainsync::msg_intersect_not_found_t>(resp.at(0)).tip, cr->tip().value());
        };

        "find_intersect non-empty"_test = [&] {
            chainsync::handler h { cr };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            const point2 target { 21599, block_hash::from_hex("3BD04916B6BC2AD849D519CFAE4FFE3B1A1660C098DBCD3E884073DD54BC8911") };
            h.data(encode(chainsync::msg_find_intersect_t { point2_list { target } }), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(fatal(std::holds_alternative<chainsync::msg_intersect_found_t>(resp.at(0))));
            const auto &found = dt::variant::get_nice<chainsync::msg_intersect_found_t>(resp.at(0));
            expect_equal(found.isect, target);
            expect_equal(found.tip, cr->tip().value());
        };

        "find_intersect unknown block"_test = [&] {
            chainsync::handler h { cr };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            const point2 target { 21599, block_hash::from_hex("0000000000000000000000000000000000000000000000000000000000000000") };
            h.data(encode(chainsync::msg_find_intersect_t { point2_list { target } }), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(fatal(std::holds_alternative<chainsync::msg_intersect_not_found_t>(resp.at(0))));
            const auto &not_found = dt::variant::get_nice<chainsync::msg_intersect_not_found_t>(resp.at(0));
            expect_equal(not_found.tip, cr->tip().value());
        };

        "find_intersect empty chain"_test = [&] {
            expect(fatal(!cr_empty->tip()));
            chainsync::handler h { cr_empty };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            const point2 target { 21599, block_hash::from_hex("0000000000000000000000000000000000000000000000000000000000000000") };
            h.data(encode(chainsync::msg_find_intersect_t { point2_list { target } }), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(fatal(std::holds_alternative<chainsync::msg_intersect_not_found_t>(resp.at(0))));
            const auto &not_found = dt::variant::get_nice<chainsync::msg_intersect_not_found_t>(resp.at(0));
            expect_equal(not_found.tip, point3 { point2 { 0, cr->config().byron_genesis_hash }, 0 });
        };

        "request_next"_test = [&] {
            chainsync::handler h { cr };
            {
                mock_response_processor_t<chainsync::msg_t> resp { decode };
                const point2 target { 21598, block_hash::from_hex("02517B67DAB9416B39E333869B80E8425FE92665FCB0B2B5EE2B4C41D33901AB") };
                h.data(encode(chainsync::msg_find_intersect_t { point2_list { target } }), std::ref(resp));
                expect(fatal(resp.size() == 1));
                expect(fatal(std::holds_alternative<chainsync::msg_intersect_found_t>(resp.at(0))));
                const auto &found = dt::variant::get_nice<chainsync::msg_intersect_found_t>(resp.at(0));
                expect_equal(found.isect, target);
                expect_equal(found.tip, cr->tip().value());
            }
            {
                mock_response_processor_t<chainsync::msg_t> resp { decode };
                h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
                expect(fatal(resp.size() == 1));
                expect(fatal(std::holds_alternative<chainsync::msg_roll_forward_t>(resp.at(0))));
                const auto &next = dt::variant::get_nice<chainsync::msg_roll_forward_t>(resp.at(0));
                expect_equal(next.tip, cr->tip().value());
                expect_equal(1, next.header->era());
                expect_equal(21599, next.header->slot());
                expect_equal(block_hash::from_hex("3BD04916B6BC2AD849D519CFAE4FFE3B1A1660C098DBCD3E884073DD54BC8911"), next.header->hash());
            }
        };

        "request_next already synced"_test = [&] {
            chainsync::handler h { cr };
            {
                mock_response_processor_t<chainsync::msg_t> resp { decode };
                const point2 target = *cr->tip();
                h.data(encode(chainsync::msg_find_intersect_t { point2_list { target } }), std::ref(resp));
                expect(fatal(resp.size() == 1));
                expect(fatal(std::holds_alternative<chainsync::msg_intersect_found_t>(resp.at(0))));
                const auto &found = dt::variant::get_nice<chainsync::msg_intersect_found_t>(resp.at(0));
                expect_equal(found.isect, cr->tip().value());
                expect_equal(found.tip, cr->tip().value());
            }
            {
                mock_response_processor_t<chainsync::msg_t> resp { decode };
                h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
                expect(fatal(resp.size() == 1));
                expect(fatal(std::holds_alternative<chainsync::msg_await_reply_t>(resp.at(0))));
            }
        };

        "request_next empty chain"_test = [&] {
            expect(fatal(!cr_empty->tip()));
            chainsync::handler h { cr_empty };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(fatal(std::holds_alternative<chainsync::msg_await_reply_t>(resp.at(0))));
        };

        "request_next no intersect"_test = [&] {
            chainsync::handler h { cr };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(fatal(std::holds_alternative<chainsync::msg_roll_forward_t>(resp.at(0))));
            const auto &next = dt::variant::get_nice<chainsync::msg_roll_forward_t>(resp.at(0));
            expect_equal(next.tip, cr->tip().value());
            expect_equal(0, next.header->era());
            expect_equal(0, next.header->slot());
            expect_equal(block_hash::from_hex("89D9B5A5B8DDC8D7E5A6795E9774D97FAF1EFEA59B2CAF7EAF9F8C5B32059DF4"), next.header->hash());
        };

        "wrong_message"_test = [&] {
            chainsync::handler h { cr };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            expect(throws([&]{ h.data(encode(chainsync::msg_await_reply_t{}), std::ref(resp)); }));
        };

        "stopped"_test = [&] {
            chainsync::handler h { cr };
            h.stopped();
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            expect(throws([&]{ h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp)); }));
        };

        "failed"_test = [&] {
            chainsync::handler h { cr };
            h.failed("some error");
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            expect(throws([&]{ h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp)); }));
        };
    };
};
