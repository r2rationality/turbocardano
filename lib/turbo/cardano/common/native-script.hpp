#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano::native_script {
    using optional_error_string = std::optional<std::string>;
    extern optional_error_string validate(cbor::zero2::value &script, uint64_t slot, const std::set<key_hash> &vkeys);
}