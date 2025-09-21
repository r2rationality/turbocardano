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
    std::cerr << "INIT: mimalloc " << mi_version() << '\n';
#endif
    using namespace turbo;
    consider_bin_dir(argv[0]);
    cli::global_options_t gopts {
        {{"config-dir",  {"the directory with configuration files"}}},
        [&](const cli::options &opts) {
            if (const auto it = opts.find("config-dir"); it != opts.end() && it->second) {
                configs_dir::set_default_path(*it->second);
            }
        }
    };
    return cli::run(argc, argv);
}