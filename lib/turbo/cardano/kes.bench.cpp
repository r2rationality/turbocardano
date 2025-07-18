/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include "kes.hpp"
#include <turbo/common/benchmark.hpp>
#include <turbo/file.hpp>
#include <turbo/util.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cardano;
}

suite cardano_kes_bench_suite = [] {
    auto vkey_data = file::read("./data/kes-vkey.bin");
    auto sig_data = file::read("./data/kes-sig.bin");
    auto msg_data = file::read("./data/kes-msg.bin");
    "kes"_test = [&] {
        benchmark("kes/create+verify",
            [&] {
                kes_signature<6> sig { sig_data };
                return sig.verify(34, kes_vkey_span(static_cast<buffer>(vkey_data)), msg_data);
            }
        );
    };
};