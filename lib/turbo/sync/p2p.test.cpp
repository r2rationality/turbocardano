/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <iterator>
#include <turbo/cardano.hpp>
#include <turbo/common/test.hpp>
#include <turbo/sync/mocks.hpp>
#include <turbo/sync/p2p.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::sync;
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
            expect_equal(cr.num_blocks(), 9);
        };
        "no work"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, ccfg };
            p2p::syncer s { cr, ps, ccm };
            expect(s.sync(s.find_peer()));
            expect_equal(cr.num_blocks(), 9);
            expect(!s.sync(s.find_peer()));
            expect_equal(cr.num_blocks(), 9);
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
            expect_equal(cr.num_blocks(), 7);
            expect(!s.sync(s.find_peer()));
            expect_equal(cr.num_blocks(), 7);
        };
        "max_slot"_test = [&] {
            constexpr uint64_t max_slot = 100;
            const auto expected_end = std::ranges::find_if(good_chain.blocks, [&](const auto &blk) {
                return blk->blk->slot() > max_slot;
            });
            expect(expected_end != good_chain.blocks.begin());
            expect(expected_end != good_chain.blocks.end());
            if (expected_end == good_chain.blocks.begin() || expected_end == good_chain.blocks.end())
                return;
            const auto expected_num_blocks = static_cast<size_t>(std::distance(good_chain.blocks.begin(), expected_end));
            const auto expected_max_slot = (*std::prev(expected_end))->blk->slot();

            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry::mode::validate, ccfg };
            p2p::syncer s { cr, ps, ccm };
            expect(s.sync(s.find_peer(), max_slot));
            expect_equal(cr.num_blocks(), expected_num_blocks);
            expect_equal(cr.max_slot(), expected_max_slot);
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
            expect(peer.tip().slot > 0);
            expect(peer.tip().height > 0);
            expect(!peer.intersection());
        };
        "random failures"_test = [&] {
            cardano_client_manager_mock ccm_rf{good_chain.data, 0.20};
            std::filesystem::remove_all(data_dir);
            chunk_registry cr{data_dir, chunk_registry::mode::validate, ccfg};
            p2p::syncer s{cr, ps, ccm_rf};
            expect(s.sync(s.find_peer()));
            expect_equal(cr.num_blocks(), 9);
        };
    };
};
