/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
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
    };
};
