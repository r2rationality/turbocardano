/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#ifdef MI_OVERRIDE
#   include <mimalloc-new-delete.h>
#endif

#ifdef DT_BENCHMARK
#   pragma message "NANONBECH imeplementation is compiled in " __FILE__
#   define ANKERL_NANOBENCH_IMPLEMENT
#   include <nanobench.h>
#endif

#include <iostream>
#include <dt/common/test.hpp>
#include <dt/config.hpp>
#include <dt/logger.hpp>

int main(const int argc, const char **argv)
{
#ifdef MI_OVERRIDE
    std::cerr << "DT_INIT: mimalloc " << mi_version() << '\n';
#endif
    using namespace daedalus_turbo;
    consider_bin_dir(argv[0]);
    logger::debug("run-test {}", argv[0]);
    if (argc >= 2) {
        std::cerr << "using test-filter mask: " << argv[1] << '\n';
        boost::ut::cfg<boost::ut::override> = { .filter = argv[1] };
    }
    const bool res = boost::ut::cfg<boost::ut::override>.run();
    logger::info("run-test finished with {}", res ? "failures" : "success");
    return 0;
}
