/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano/ledger/state.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/plutus/context.hpp>
#include <turbo/plutus/flat-encoder.hpp>

namespace turbo::cli::test_flat_encoder {
    using namespace cardano;
    using namespace cardano::ledger;
    using namespace plutus;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "test-flat-encoder";
            cmd.desc = "decode and re-encode plutus scripts them and check the result is the same";
            cmd.args.expect({ "<script-dir>" });
        }

        void run(const arguments &args) const override {
            const auto &script_dir = args.at(0);
            auto &sched = scheduler::get();
            const auto paths = file::files_with_ext(fmt::format("{}", script_dir), ".flat");
            std::atomic_size_t err { 0 };
            for (size_t i = 0; i < paths.size(); ++i) {
                if (std::filesystem::file_size(paths[i]) > 0) {
                    sched.submit("extract", -static_cast<int64_t>(i), [&, i]() {
                        const auto script_path = paths[i];
                        const auto ex_ptr = logger::run_log_errors([&] {
                            try {
                                allocator alloc {};
                                const auto exp = file::read(script_path);
                                flat::script s { alloc, exp };
                                const auto act = flat::encode_cbor(s.version(), s.program());
                                if (act != exp) [[unlikely]] {
                                    logger::warn("{}: {}", script_path, stringify_diff(exp, act));
                                    throw error(fmt::format("reencoded value does not match!", script_path));
                                }
                            } catch (const std::exception &ex) {
                                throw error(fmt::format("failed to re-encode script {}: {}", script_path, ex.what()));
                            }
                        });
                        if (ex_ptr)
                            err.fetch_add(1, std::memory_order_relaxed);
                    });
                }
            }
            sched.process();
            logger::info("tested unique scripts: {} errors: {}", paths.size(), err.load(std::memory_order_relaxed));
        }
    private:
        static std::string stringify_diff(const buffer b1, const buffer b2)
        {
            std::string res {};
            auto out_it = std::back_inserter(res);
            if (b1.size() != b2.size())
                out_it = fmt::format_to(out_it, "sizes mismatch: {} vs {}\n", b1.size(), b2.size());
            const auto min_sz = std::min(b1.size(), b2.size());
            for (size_t i = 0; i < min_sz; ++i) {
                if (b1[i] != b2[i]) {
                    out_it = fmt::format_to(out_it, "diff at byte {}: {:02x} != {:02x}\n", i, b1[i], b2[i]);
                }
            }
            return res;
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
