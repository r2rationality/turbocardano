/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/sync/mocks.hpp>
#include <turbo/validator.hpp>
#include "base.hpp"

namespace {
    using namespace turbo;
    using namespace turbo::sync;

    struct test_peer_info: peer_info {
        explicit test_peer_info(const cardano::point &tip_)
            : _tip { static_cast<cardano::point3>(tip_) }
        {
        }

        std::string id() const override
        {
            return "test-peer";
        }

        const cardano::optional_point3 &tip() const override
        {
            return _tip;
        }

        const cardano::optional_point &intersection() const override
        {
            return _intersection;
        }

        void intersection(const cardano::optional_point &new_intersection) override
        {
            _intersection = new_intersection;
        }
    private:
        cardano::optional_point3 _tip {};
        cardano::optional_point _intersection {};
    };

    struct partial_progress_syncer: syncer {
        partial_progress_syncer(chunk_registry &cr, uint8_vector first_chunk, uint8_vector second_chunk, std::string obsolete_path,
            std::function<void()> on_retry={}, bool fail_after_progress=true)
            : syncer { cr }, _first_chunk { std::move(first_chunk) }, _second_chunk { std::move(second_chunk) },
                _obsolete_path { std::move(obsolete_path) }, _on_retry { std::move(on_retry) }, _fail_after_progress { fail_after_progress }
        {
        }

    private:
        uint8_vector _first_chunk {};
        uint8_vector _second_chunk {};
        std::string _obsolete_path {};
        std::function<void()> _on_retry {};
        bool _fail_after_progress;

        void cancel_tasks(uint64_t) override
        {
        }

        bool sync_attempt(peer_info &peer, cardano::optional_slot) override
        {
            if (peer.intersection()) {
                if (_on_retry) _on_retry();
                local_chain().add_buffer(local_chain().num_bytes(), std::move(_second_chunk));
                return false;
            }
            local_chain().add_buffer(0, std::move(_first_chunk));
            local_chain().remover().mark(_obsolete_path, std::chrono::seconds { -1 });
            if (_fail_after_progress)
                throw error("recoverable failure after progress");
            return false;
        }
    };
}

suite turbo_sync_base_suite = [] {
    "turbo::sync::base"_test = [] {
        optional_progress_point target{};
        expect_equal(false, optional_point{} < target);
        expect_equal(false, optional_point{point{{}, 0U}} < target);
        "partial sync saves only at completion"_test = [] {
            for (const bool fail_after_progress: { false, true }) {
                const file::tmp_directory dir { "sync-final-snapshot" };
                const auto chain = gen_chain({ .height=3 });
                file_remover remover;
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .validate_vrf=false, .ccfg=chain.cardano_cfg, .fr=remover } };
                const auto split = chain.blocks.front()->blk.raw().size();
                uint8_vector first { static_cast<buffer>(chain.data).subbuf(0, split) };
                uint8_vector second { static_cast<buffer>(chain.data).subbuf(split) };
                const auto obsolete_path = dir.path() + "/obsolete";
                file::write(obsolete_path, std::string_view { "obsolete" });
                bool retried = false;
                partial_progress_syncer syncer { cr, std::move(first), std::move(second), obsolete_path, [&] {
                    retried = true;
                    expect(cr.validator().snapshots().empty());
                }, fail_after_progress };
                auto peer = std::make_shared<test_peer_info>(*chain.tip);
                expect(syncer.sync(peer, {}, validation_mode_t::none));
                expect(retried);
                expect_equal(cr.validator().snapshots().size(), 1);
                expect_equal(cr.validator().snapshots().rbegin()->end_offset, cr.num_bytes());
            }
        };
        "cleanup after recoverable progress"_test = [] {
            static const std::string data_dir { "./tmp/test-sync-base-cleanup" };
            std::filesystem::remove_all(data_dir);
            const auto chain = gen_chain();
            file_remover remover {};
            chunk_registry cr { data_dir, chunk_registry_settings_t { .ccfg=chain.cardano_cfg, .fr=remover } };
            const auto obsolete_path = fmt::format("{}/obsolete-ledger.bin", data_dir);
            file::write(obsolete_path, std::string_view { "obsolete" });
            uint8_vector first_chunk {}, second_chunk {};
            for (size_t i = 0; i < chain.blocks.size(); ++i) {
                if (i < chain.blocks.size() / 2)
                    first_chunk << *chain.blocks.at(i)->data;
                else
                    second_chunk << *chain.blocks.at(i)->data;
            }
            auto peer = std::make_shared<test_peer_info>(*chain.tip);
            partial_progress_syncer syncer { cr, std::move(first_chunk), std::move(second_chunk), obsolete_path };

            expect(syncer.sync(peer, {}, validation_mode_t::none));
            expect(!std::filesystem::exists(obsolete_path));
        };
    };
};
