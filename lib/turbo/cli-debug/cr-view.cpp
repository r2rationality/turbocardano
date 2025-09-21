/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/crypto/blake2b.hpp>

namespace turbo::cli::debug::cr_view {
    struct cmd: command {
        const command_info &info() const override
        {
            static const command_info i { "cr-view", "<data-dir> <offset>", "print a CBOR element located at offset <offset> in the chunk-registry <compressed-dir>" };
            return i;
        }

        void run(const arguments &args) const override
        {
            if (args.size() < 2) _throw_usage();
            const std::string &data_dir = args.at(0);
            const uint64_t offset = std::stoull(args.at(1));
            const chunk_registry cr { data_dir, chunk_registry::mode::store };
            auto val_pv = cr.read(offset);
            auto &val = val_pv.get();
            const auto &info = cr.find_offset(offset);
            val.to_stream(std::cout, 100);
            std::cout << std::endl;
            logger::info("cr-view offset {} size: {} hash: {} path: {} epoch: {}", offset, val.data_raw().size(),
                crypto::blake2b::digest(val.data_raw()), info.rel_path(), cr.make_slot(info.first_slot).epoch());
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}