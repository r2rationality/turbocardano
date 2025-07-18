/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include "const-reverse-iterator.hpp"
#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;

suite storage_const_reverse_iterator_suite = [] {
    "storage::const_reverse_iterator"_test = [] {
        static std::string data_dir = install_path("./data/chunk-registry");
        const chunk_registry cr { data_dir, chunk_registry::mode::store };

        // ++
        {
            size_t num_blocks = 0;
            size_t num_bytes = 0;
            for (auto it = cr.crbegin(), end = cr.crend(); it != end; ++it) {
                ++num_blocks;
                num_bytes += it->size;
            }
            expect_equal(cr.num_blocks(), num_blocks);
            expect_equal(cr.num_bytes(), num_bytes);
        }
        expect(++cr.crend() == cr.crend());

        // --
        {
            size_t num_blocks  = 0;
            size_t num_bytes = 0;
            for (auto it = cr.crend(), first = cr.crbegin(); it != first;) {
                --it;
                ++num_blocks;
                num_bytes += it->size;
            }
            expect_equal(cr.num_blocks(), num_blocks);
            expect_equal(cr.num_bytes(), num_bytes);
        }
        expect(--cr.crbegin() == cr.crbegin());
    };
};
