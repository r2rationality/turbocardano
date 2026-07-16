/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

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