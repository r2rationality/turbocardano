/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/storage/test.hpp>
#include "validator.hpp"
#include "turbo/chunk-registry.hpp"

namespace {
    using namespace turbo;
}

suite txwit_validator_suite = [] {
    "txwit::validator"_test = [] {
        static const std::string src_dir { turbo::storage::sample_registry_path() };
        const chunk_registry cr { src_dir, chunk_registry_settings_t { .mode=chunk_registry::mode::store } };
        const cardano::optional_point target { cr.find_block_by_slot(7167).point() };
        expect_equal(txwit::validate(cr, {}, target), target);

        "epoch setup gates ledger application"_test = [&] {
            cardano::ledger::state st { cr.config(), cr.sched() };
            txwit::processor proc { cr, st };
            storage::chunk_cptr_list chunks;
            for (const auto &[id, chunk]: cr.chunks()) {
                if (chunk.offset >= target->end_offset)
                    break;
                chunks.push_back(&chunk);
            }
            size_t setup_calls = 0;
            proc.apply({}, {}, target, txwit::witness_type::all, [&] {
                ++setup_calls;
                return false;
            });
            expect_equal(setup_calls, 1);
            proc.apply(chunks, {}, target, txwit::witness_type::all, [&] {
                ++setup_calls;
                return false;
            });
            expect_equal(setup_calls, 2);
            expect_equal(st.end_offset(), 0);
            expect(throws([&] {
                proc.apply(chunks, {}, target, txwit::witness_type::all, [&]() -> bool {
                    throw error("epoch setup failed");
                });
            }));
            expect_equal(st.end_offset(), 0);
            proc.apply(chunks, {}, target, txwit::witness_type::all, [&] {
                ++setup_calls;
                expect_equal(st.end_offset(), 0);
                st.start_epoch(0);
                return true;
            });
            expect_equal(setup_calls, 3);
            expect_equal(proc.end_offset(), target->end_offset);
            expect_equal(st.end_offset(), target->end_offset);
        };
    };
};
