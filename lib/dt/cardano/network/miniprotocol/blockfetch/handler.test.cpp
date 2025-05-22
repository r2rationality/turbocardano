/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/network/mock.hpp>
#include <dt/cbor/zero2.hpp>
#include <dt/chunk-registry.hpp>
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
    using namespace daedalus_turbo::cardano::network::miniprotocol::blockfetch;

    template<typename T>
    uint8_vector encode(const T &val)
    {
        cbor::encoder enc {};
        val.to_cbor(enc);
        return std::move(enc.cbor());
    }

    msg_t decode(const buffer bytes)
    {
        auto pv = cbor::zero2::parse(bytes);
        return msg_t::from_cbor(pv.get());
    }
}

suite cardano_network_miniprotocol_blockfetch_suite = [] {
    "cardano::network::miniprotocol::blockfetch"_test = [] {
        const auto cr = std::make_shared<chunk_registry>(install_path("data/chunk-registry"), chunk_registry::mode::store);

        "client done"_test = [&] {
            handler h { cr, config_t {} };
            mock_response_processor_t<msg_t> resp { decode };
            h.data(encode(msg_client_done_t {}), std::ref(resp));
            test_same(0, resp.size());
            expect(throws([&] { h.data(encode(msg_client_done_t {}), std::ref(resp)); }));
        };

        "request_range"_test = [&] {
            handler h { cr, config_t {} };
            mock_response_processor_t<msg_t> resp { decode };
            h.data(encode(msg_request_range_t {
                { 21598, block_hash::from_hex("02517B67DAB9416B39E333869B80E8425FE92665FCB0B2B5EE2B4C41D33901AB") },
                { 21599, block_hash::from_hex("3BD04916B6BC2AD849D519CFAE4FFE3B1A1660C098DBCD3E884073DD54BC8911") }
            }), std::ref(resp));
            expect_equal(4, resp.size());
            expect(std::holds_alternative<msg_start_batch_t>(resp.at(0)));
            expect(fatal(std::holds_alternative<msg_block_t>(resp.at(1))));
            //const auto &blk1 = dt::variant::get_nice<msg_block_t>(responses.at(1));
            auto pv1 = cbor::zero2::parse(dt::variant::get_nice<msg_block_t>(resp.at(1)).bytes);
            const block_container blk1 { 0, pv1.get() };
            expect_equal(21598, blk1->slot());
            expect(fatal(std::holds_alternative<msg_block_t>(resp.at(2))));
            auto pv2 = cbor::zero2::parse(dt::variant::get_nice<msg_block_t>(resp.at(2)).bytes);
            const block_container blk2 { 0, pv2.get() };
            expect_equal(21599, blk2->slot());
            expect(std::holds_alternative<msg_batch_done_t>(resp.at(3)));
        };

        "request_range compressed"_test = [&] {
            handler h { cr, config_t { .block_compression=true} };
            mock_response_processor_t<msg_t> resp { decode };
            h.data(encode(msg_request_range_t {
                { 21598, block_hash::from_hex("02517B67DAB9416B39E333869B80E8425FE92665FCB0B2B5EE2B4C41D33901AB") },
                { 21599, block_hash::from_hex("3BD04916B6BC2AD849D519CFAE4FFE3B1A1660C098DBCD3E884073DD54BC8911") }
            }), std::ref(resp));
            expect_equal(3, resp.size());
            expect(std::holds_alternative<msg_start_batch_t>(resp.at(0)));
            expect(std::holds_alternative<msg_compressed_blocks_t>(resp.at(1)));
            expect(std::holds_alternative<msg_batch_done_t>(resp.at(2)));
            const auto &blks = dt::variant::get_nice<msg_compressed_blocks_t>(resp.at(1));
            const auto bytes = blks.bytes();
            cbor::zero2::decoder dec { bytes };
            std::vector<std::unique_ptr<block_container>> blocks {};
            while (!dec.done()) {
                blocks.emplace_back(std::make_unique<block_container>(0, dec.read()));
            }
            test_same(2, blocks.size());
            expect_equal(21598, (*blocks.at(0))->slot());
            expect_equal(21599, (*blocks.at(1))->slot());
        };

        "bad request_range start"_test = [&] {
            handler h { cr, config_t {} };
            mock_response_processor_t<msg_t> resp { decode };
            h.data(encode(msg_request_range_t {
                { 21598, block_hash::from_hex("02517B67DAB9416B39E333869B80E8425FE92665FCB0B2B5EE2B4C41D33901AC") },
                { 21599, block_hash::from_hex("3BD04916B6BC2AD849D519CFAE4FFE3B1A1660C098DBCD3E884073DD54BC8911") }
            }), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(std::holds_alternative<msg_no_blocks_t>(resp.at(0)));
        };

        "bad request_range end"_test = [&] {
            handler h { cr, config_t {} };
            mock_response_processor_t<msg_t> resp { decode };
            h.data(encode(msg_request_range_t {
                { 21598, block_hash::from_hex("02517B67DAB9416B39E333869B80E8425FE92665FCB0B2B5EE2B4C41D33901AB") },
                { 21599, block_hash::from_hex("3BD04916B6BC2AD849D519CFAE4FFE3B1A1660C098DBCD3E884073DD54BC8912") }
            }), std::ref(resp));
            expect(fatal(resp.size() == 1));
            expect(std::holds_alternative<msg_no_blocks_t>(resp.at(0)));
        };

        "wrong_message"_test = [&] {
            handler h { cr, config_t {} };
            mock_response_processor_t<msg_t> resp { decode };
            expect(throws([&]{ h.data(encode(msg_start_batch_t {}), std::ref(resp)); }));
        };

        "stopped"_test = [&] {
            handler h { cr, config_t {} };
            h.stopped();
            mock_response_processor_t<msg_t> resp { decode };
            expect(throws([&]{ h.data(encode(msg_client_done_t {}), std::ref(resp)); }));
        };

        "failed"_test = [&] {
            handler h { cr, config_t {} };
            h.failed("some error");
            mock_response_processor_t<msg_t> resp { decode };
            expect(throws([&]{ h.data(encode(msg_client_done_t {}), std::ref(resp)); }));
        };
    };
};
