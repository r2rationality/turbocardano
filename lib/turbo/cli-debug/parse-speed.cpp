/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/storage/partition.hpp>

namespace turbo::cli::parse_speed {
    using namespace cardano;
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "parse-speed";
            cmd.desc = "Measure the speed of parsing the compressed chunks";
            cmd.args.expect({ "<data-dir>" });
        }

        void run(const arguments &args) const override
        {
            const auto res = _parse_parallel(args.at(0));
            const auto parsed_gb = static_cast<double>(res.size) / 1'000'000'000;
            logger::info("Total parsed: {:1.3f} GB, time taken: {:.1f} sec parse speed: {:.3f} GB/sec",
                parsed_gb, res.duration, parsed_gb / res.duration);
        }
    private:
        struct result {
            uint64_t size = 0;
            double duration = 0.0;
        };

        struct part_info {
            std::chrono::high_resolution_clock::time_point start_time;
            uint64_t size = 0;
        };

        static result _parse_parallel(const std::string &data_dir)
        {
            const chunk_registry cr { data_dir, chunk_registry::mode::store };
            mutex::unique_lock::mutex_type res_mutex alignas(mutex::alignment) {};
            result res {};
            storage::parse_parallel_chunk<part_info>(cr,
                [&](auto &part, const auto &blk) {
                    part.size += blk.size();
                },
                [&](const auto, const auto &) {
                    return part_info { std::chrono::high_resolution_clock::now() };
                },
                [&](auto &&part, const auto, const auto &) {
                    mutex::scoped_lock lock { res_mutex };
                    res.duration += std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - part.start_time).count();
                    res.size += part.size;
                },
                "parse-chunk"
            );
            return res;
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
