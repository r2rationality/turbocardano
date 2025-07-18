/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include "validator.hpp"
#include <turbo/common/test.hpp>
#include "turbo/chunk-registry.hpp"

namespace {
    using namespace turbo;
}

suite txwit_validator_suite = [] {
    "txwit::validator"_test = [] {
        static const std::string src_dir { "./data/chunk-registry"s };
        const chunk_registry cr { src_dir, chunk_registry::mode::store };
        txwit::validate(cr, {}, cr.find_block_by_slot(7167).point());
    };
};