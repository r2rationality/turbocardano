/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/sync/mocks.hpp>
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

        const cardano::point3 &tip() const override
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
        cardano::point3 _tip {};
        cardano::optional_point _intersection {};
    };

    struct recoverable_failure_syncer: syncer {
        recoverable_failure_syncer(chunk_registry &cr, uint8_vector first_chunk, uint8_vector second_chunk, std::string obsolete_path)
            : syncer { cr }, _first_chunk { std::move(first_chunk) }, _second_chunk { std::move(second_chunk) },
                _obsolete_path { std::move(obsolete_path) }
        {
        }

    private:
        uint8_vector _first_chunk {};
        uint8_vector _second_chunk {};
        std::string _obsolete_path {};

        void cancel_tasks(uint64_t) override
        {
        }

        void sync_attempt(peer_info &peer, cardano::optional_slot) override
        {
            if (peer.intersection()) {
                local_chain().add_buffer(local_chain().num_bytes(), std::move(_second_chunk));
                return;
            }
            local_chain().add_buffer(0, std::move(_first_chunk));
            local_chain().remover().mark(_obsolete_path, std::chrono::seconds { -1 });
            throw error("recoverable failure after progress");
        }
    };
}

suite turbo_sync_base_suite = [] {
    "turbo::sync::base"_test = [] {
        optional_progress_point target{};
        expect_equal(false, optional_point{} < target);
        expect_equal(false, optional_point{point{{}, 0U}} < target);
        "cleanup after recoverable progress"_test = [] {
            static const std::string data_dir { "./tmp/test-sync-base-cleanup" };
            std::filesystem::remove_all(data_dir);
            const auto chain = gen_chain();
            file_remover remover {};
            chunk_registry cr { data_dir, chunk_registry::mode::store, chain.cardano_cfg, scheduler::get(), remover };
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
            recoverable_failure_syncer syncer { cr, std::move(first_chunk), std::move(second_chunk), obsolete_path };

            expect(syncer.sync(peer, {}, validation_mode_t::none));
            expect(!std::filesystem::exists(obsolete_path));
        };
    };
};
