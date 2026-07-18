/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "kes.hpp"
#include <turbo/common/benchmark.hpp>
#include <turbo/file.hpp>
#include <turbo/util.hpp>

namespace {
    using namespace std::literals::string_view_literals;
    using namespace turbo;
    using namespace turbo::cardano;
}

suite cardano_kes_bench_suite = [] {
    auto vkey_data = file::read("./data/kes-vkey.bin"sv);
    auto sig_data = file::read("./data/kes-sig.bin"sv);
    auto msg_data = file::read("./data/kes-msg.bin"sv);
    "kes"_test = [&] {
        benchmark("kes/create+verify",
            [&] {
                kes_signature<6> sig { sig_data };
                return sig.verify(34, kes_vkey_span(static_cast<buffer>(vkey_data)), msg_data);
            }
        );
    };
};
