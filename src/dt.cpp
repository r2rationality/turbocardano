/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#ifdef MI_OVERRIDE
#   include <mimalloc-new-delete.h>
#endif
#include <../lib/turbo/common/cli.hpp>

int main(const int argc, const char **argv)
{
#ifdef MI_OVERRIDE
    std::cerr << "DT_INIT: mimalloc " << mi_version() << '\n';
#endif
    using namespace turbo;
    consider_bin_dir(argv[0]);
    return cli::run(argc, argv);
}