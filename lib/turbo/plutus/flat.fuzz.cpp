/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/plutus/flat.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, const size_t size)
{
    using namespace turbo;
    using namespace turbo::plutus;
    try {
        flat::script s { buffer { data, size } };
    } catch (const error &err) {
        // ignore the library's exceptions
    }
    return 0;
}