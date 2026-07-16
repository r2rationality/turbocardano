/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "const-iterator.hpp"
#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;

suite storage_const_iterator_suite = [] {
    using namespace turbo::storage;
    using const_iterator = storage::const_iterator;
    "storage::const_iterator"_test = [] {
        static std::string data_dir = install_path("./data/chunk-registry");
        const chunk_registry cr { data_dir, chunk_registry::mode::store };

        // ++
        {
            size_t num_blocks = 0;
            size_t num_bytes = 0;
            for (auto it = cr.cbegin(), end = cr.cend(); it != end; ++it) {
                ++num_blocks;
                num_bytes += it->size;
            }
            expect_equal(cr.num_blocks(), num_blocks);
            expect_equal(cr.num_bytes(), num_bytes);
        }
        expect(++cr.cend() == cr.cend());

        // --
        {
            size_t num_blocks  = 0;
            size_t num_bytes = 0;
            for (auto it = cr.cend(), first = cr.cbegin(); it != first;) {
                --it;
                ++num_blocks;
                num_bytes += it->size;
            }
            expect_equal(cr.num_blocks(), num_blocks);
            expect_equal(cr.num_bytes(), num_bytes);
        }
        expect(--cr.cbegin() == cr.cbegin());

        // -
        expect_equal(storage::const_iterator::difference_type { 0 }, cr.cbegin() - cr.cbegin());
        expect_equal(storage::const_iterator::difference_type { 0 }, cr.cend() - cr.cend());
        expect_equal(static_cast<storage::const_iterator::difference_type>(cr.num_blocks()), cr.cend() - cr.cbegin());
        expect_equal(static_cast<storage::const_iterator::difference_type>(cr.num_blocks() - 2), --cr.cend() - ++cr.cbegin());
        expect_equal(-static_cast<storage::const_iterator::difference_type>(cr.num_blocks()), cr.cbegin() - cr.cend());
        expect_equal(-static_cast<storage::const_iterator::difference_type>(cr.num_blocks() - 2), ++cr.cbegin() - --cr.cend());

        // block_distance
        expect(block_distance_t { 0, 0 } == const_iterator::block_distance(cr.cbegin(), cr.cbegin()));
        expect(block_distance_t { 0, 0 } == const_iterator::block_distance(cr.cend(), cr.cend()));
        expect(block_distance_t { static_cast<ptrdiff_t>(cr.num_blocks()), 1 } == const_iterator::block_distance(cr.cend(), cr.cbegin()));
        expect(block_distance_t {  static_cast<ptrdiff_t>(cr.num_blocks()) - 2, 0 } == const_iterator::block_distance(--cr.cend(), ++cr.cbegin()));

        // +
        expect(cr.cend() == cr.cbegin() + static_cast<ptrdiff_t>(cr.num_blocks()));
        expect(cr.cbegin() == cr.cend() + (-static_cast<ptrdiff_t>(cr.num_blocks())));
        expect(cr.cbegin() == cr.cbegin() + 0);
        expect(cr.cend() == cr.cend() + 0);
        expect_equal(static_cast<storage::const_iterator::difference_type>(cr.num_blocks() - 4), (cr.cend() + -2) - (cr.cbegin() + 2));
    };
};