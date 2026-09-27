/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "handler.hpp"
#include <turbo/cardano/network/mock.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>
#include <turbo/common/variant.hpp>
#include "messages.hpp"
#include <turbo/sync/mocks.hpp>

namespace {
    namespace dt = turbo;
    using namespace turbo;
    using namespace turbo::cardano;
    using namespace turbo::cardano::network;
    using namespace turbo::cardano::network::miniprotocol;

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
            expect_equal(dt::variant::get_nice<chainsync::msg_intersect_not_found_t>(resp.at(0)).tip, optional_point3 { cr->tip() });
        };

        "find_intersect non-empty"_test = [&] {
            chainsync::handler h { cr };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            const point2 target { 21599, block_hash::from_hex("3BD04916B6BC2AD849D519CFAE4FFE3B1A1660C098DBCD3E884073DD54BC8911") };
            h.data(encode(chainsync::msg_find_intersect_t { optional_point2_list { target } }), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(fatal(std::holds_alternative<chainsync::msg_intersect_found_t>(resp.at(0))));
            const auto &found = dt::variant::get_nice<chainsync::msg_intersect_found_t>(resp.at(0));
            expect_equal(found.isect, optional_point2 { target });
            expect_equal(found.tip, optional_point3 { cr->tip() });
        };

        "find_intersect unknown block"_test = [&] {
            chainsync::handler h { cr };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            const point2 target { 21599, block_hash::from_hex("0000000000000000000000000000000000000000000000000000000000000000") };
            h.data(encode(chainsync::msg_find_intersect_t { optional_point2_list { target } }), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(fatal(std::holds_alternative<chainsync::msg_intersect_not_found_t>(resp.at(0))));
            const auto &not_found = dt::variant::get_nice<chainsync::msg_intersect_not_found_t>(resp.at(0));
            expect_equal(not_found.tip, optional_point3 { cr->tip() });
        };

        "find_intersect empty chain"_test = [&] {
            expect(fatal(!cr_empty->tip()));
            chainsync::handler h { cr_empty };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            const point2 target { 21599, block_hash::from_hex("0000000000000000000000000000000000000000000000000000000000000000") };
            h.data(encode(chainsync::msg_find_intersect_t { optional_point2_list { target } }), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(fatal(std::holds_alternative<chainsync::msg_intersect_not_found_t>(resp.at(0))));
            const auto &not_found = dt::variant::get_nice<chainsync::msg_intersect_not_found_t>(resp.at(0));
            expect(!not_found.tip);
        };

        "origin is a found intersection on empty and populated chains"_test = [&] {
            for (const auto &chain: { cr_empty, cr }) {
                chainsync::handler h { chain };
                mock_response_processor_t<chainsync::msg_t> resp { decode };
                h.data(encode(chainsync::msg_find_intersect_t { optional_point2_list { optional_point2 {} } }), std::ref(resp));
                expect(fatal(resp.size() == 1));
                expect(fatal(std::holds_alternative<chainsync::msg_intersect_found_t>(resp.at(0))));
                const auto &found = dt::variant::get_nice<chainsync::msg_intersect_found_t>(resp.at(0));
                expect(!found.isect);
                expect_equal(found.tip.has_value(), chain->tip().has_value());
                h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
                expect(fatal(std::holds_alternative<chainsync::msg_roll_backward_t>(resp.at(1))));
                const auto &back = dt::variant::get_nice<chainsync::msg_roll_backward_t>(resp.at(1));
                expect(!back.target);
                expect_equal(back.tip.has_value(), chain->tip().has_value());
                h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
                if (chain->tip()) {
                    expect(fatal(std::holds_alternative<chainsync::msg_roll_forward_t>(resp.at(2))));
                    const auto &next = dt::variant::get_nice<chainsync::msg_roll_forward_t>(resp.at(2));
                    expect_equal(next.header->slot(), 0);
                } else {
                    expect(std::holds_alternative<chainsync::msg_await_reply_t>(resp.at(2)));
                }
            }
        };

        "request_next"_test = [&] {
            chainsync::handler h { cr };
            {
                mock_response_processor_t<chainsync::msg_t> resp { decode };
                const point2 target { 21598, block_hash::from_hex("02517B67DAB9416B39E333869B80E8425FE92665FCB0B2B5EE2B4C41D33901AB") };
                h.data(encode(chainsync::msg_find_intersect_t { optional_point2_list { target } }), std::ref(resp));
                expect(fatal(resp.size() == 1));
                expect(fatal(std::holds_alternative<chainsync::msg_intersect_found_t>(resp.at(0))));
                const auto &found = dt::variant::get_nice<chainsync::msg_intersect_found_t>(resp.at(0));
                expect_equal(found.isect, optional_point2 { target });
                expect_equal(found.tip, optional_point3 { cr->tip() });
            }
            {
                mock_response_processor_t<chainsync::msg_t> resp { decode };
                h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
                expect(fatal(std::holds_alternative<chainsync::msg_roll_backward_t>(resp.at(0))));
            }
            {
                mock_response_processor_t<chainsync::msg_t> resp { decode };
                h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
                expect(fatal(resp.size() == 1));
                expect(fatal(std::holds_alternative<chainsync::msg_roll_forward_t>(resp.at(0))));
                const auto &next = dt::variant::get_nice<chainsync::msg_roll_forward_t>(resp.at(0));
                expect_equal(next.tip, optional_point3 { cr->tip() });
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
                h.data(encode(chainsync::msg_find_intersect_t { optional_point2_list { target } }), std::ref(resp));
                expect(fatal(resp.size() == 1));
                expect(fatal(std::holds_alternative<chainsync::msg_intersect_found_t>(resp.at(0))));
                const auto &found = dt::variant::get_nice<chainsync::msg_intersect_found_t>(resp.at(0));
                expect_equal(found.isect, optional_point2 { static_cast<point2>(*cr->tip()) });
                expect_equal(found.tip, optional_point3 { cr->tip() });
            }
            {
                mock_response_processor_t<chainsync::msg_t> resp { decode };
                h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
                expect(fatal(std::holds_alternative<chainsync::msg_roll_backward_t>(resp.at(0))));
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
            expect_equal(next.tip, optional_point3 { cr->tip() });
            expect_equal(0, next.header->era());
            expect_equal(0, next.header->slot());
            expect_equal(block_hash::from_hex("89D9B5A5B8DDC8D7E5A6795E9774D97FAF1EFEA59B2CAF7EAF9F8C5B32059DF4"), next.header->hash());
        };

        "live publication and rollback while awaiting"_test = [] {
            const file::tmp_directory dir { "chainsync-live" };
            const auto chain = sync::gen_chain({ .height=2 });
            file_remover remover;
            auto live = std::make_shared<chunk_registry>(dir.path(), chunk_registry::mode::store,
                cardano::config { chain.cfg }, scheduler::get(), remover);
            auto source = std::make_shared<chain_source>(live);
            chainsync::handler h { source };
            mock_response_processor_t<chainsync::msg_t> resp { decode };
            h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
            expect(fatal(std::holds_alternative<chainsync::msg_await_reply_t>(resp.at(0))));
            h.poll(std::ref(resp));
            expect_equal(resp.size(), 1);
            live->accept_anything_or_throw({}, chain.tip, [&] { live->add_buffer(0, chain.data); });
            h.poll(std::ref(resp));
            expect_equal(resp.size(), 1); // unannounced commits remain invisible
            source->publish({});
            h.poll(std::ref(resp));
            expect(fatal(std::holds_alternative<chainsync::msg_roll_forward_t>(resp.at(1))));
            for (size_t i = 1; i < chain.blocks.size(); ++i)
                h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
            h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
            expect(fatal(std::holds_alternative<chainsync::msg_await_reply_t>(resp.messages().back())));
            const auto target = live->find_block_by_slot(chain.blocks.front()->blk->slot()).point();
            live->truncate(target);
            source->publish(target);
            const auto before = resp.size();
            h.poll(std::ref(resp));
            expect_equal(resp.size(), before + 1);
            const auto &back = dt::variant::get_nice<chainsync::msg_roll_backward_t>(resp.messages().back());
            expect(fatal(back.target.has_value()));
            expect_equal(*back.target, static_cast<point2>(target));
            expect_equal(back.tip, optional_point3 { target });
            const auto tail = static_cast<buffer>(chain.data).subbuf(target.end_offset);
            live->accept_anything_or_throw(target, chain.tip, [&] { live->add_buffer(target.end_offset, uint8_vector { tail }); });
            source->publish(target);
            h.data(encode(chainsync::msg_request_next_t {}), std::ref(resp));
            const auto &forward = dt::variant::get_nice<chainsync::msg_roll_forward_t>(resp.messages().back());
            expect_equal(forward.header->hash(), chain.blocks.back()->blk->hash());
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
