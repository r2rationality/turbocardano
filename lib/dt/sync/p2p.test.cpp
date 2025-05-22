/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano.hpp>
#include <dt/common/test.hpp>
#include <dt/sync/mocks.hpp>
#include <dt/sync/p2p.hpp>

namespace {
    using namespace daedalus_turbo;
    using namespace daedalus_turbo::sync;
}

suite sync_p2p_suite = [] {
    "sync::p2p"_test = [] {
        const std::string data_dir { "./tmp/test-sync-p2p" };
        auto &ps = peer_selection_simple::get();
        mock_chain_config mock_cfg {};
        const auto good_chain = gen_chain(mock_cfg);
        const cardano::config ccfg { good_chain.cfg };
        cardano_client_manager_mock ccm { good_chain.data };
        "success"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, ccfg };
            p2p::syncer s { cr, ps, ccm };
            expect(s.sync(s.find_peer()));
            test_same(cr.num_blocks(), 9);
        };
        "no work"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, ccfg };
            p2p::syncer s { cr, ps, ccm };
            expect(s.sync(s.find_peer()));
            test_same(cr.num_blocks(), 9);
            expect(!s.sync(s.find_peer()));
            test_same(cr.num_blocks(), 9);
        };
        "failure"_test = [&] {
            mock_chain_config test_mock_cfg { mock_cfg };
            test_mock_cfg.failure_height = 7;
            const auto chain = gen_chain(test_mock_cfg);
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, ccfg };
            cardano_client_manager_mock test_ccm { chain.data };
            p2p::syncer s { cr, ps, test_ccm};
            expect(s.sync(s.find_peer()));
            test_same(cr.num_blocks(), 7);
            expect(!s.sync(s.find_peer()));
            test_same(cr.num_blocks(), 7);
        };
        "max_slot"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, ccfg };
            p2p::syncer s { cr, ps, ccm };
            expect(s.sync(s.find_peer(), 100));
            test_same(cr.num_blocks(), 6);
            test_same(cr.max_slot(), 87);
        };
        "multi chunk"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry::mode::store };
            std::vector<std::string> paths {};
            paths.emplace_back("./data/chunk-registry-new/0-0.chunk");
            paths.emplace_back("./data/chunk-registry-new/0-1.chunk");
            paths.emplace_back("./data/chunk-registry-new/1-0.chunk");
            paths.emplace_back("./data/chunk-registry-new/1-1.chunk");
            paths.emplace_back("./data/chunk-registry-new/2-0.chunk");
            paths.emplace_back("./data/chunk-registry-new/2-1.chunk");
            paths.emplace_back("./data/chunk-registry-new/3-0.chunk");
            cardano_client_manager_mock ccm { paths };
            sync::p2p::syncer s { cr, ps, ccm };
            s.sync(s.find_peer(), 50'000, validation_mode_t::none);
            expect(cr.max_slot() == 50'000_ull);
            s.sync(s.find_peer(), {}, validation_mode_t::none);
            expect(cr.max_slot() == 79'999_ull);
        };
        "find_peer"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir };
            sync::p2p::syncer s { cr, ps, ccm };
            const auto peer_ptr = s.find_peer();
            auto &peer = dynamic_cast<sync::p2p::peer_info &>(*peer_ptr);
            expect(!!peer.tip());
            if (peer.tip()) {
                expect(peer.tip()->slot > 0);
                expect(peer.tip()->height > 0);
            }
            expect(!peer.intersection());
        };
    };
};