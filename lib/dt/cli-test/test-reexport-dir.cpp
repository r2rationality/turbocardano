/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#include <dt/cli.hpp>
#include <dt/cardano/ledger/state.hpp>

namespace daedalus_turbo::cli::test_reexport_dir {
    using namespace cardano::ledger;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "test-reexport-dir";
            cmd.desc = "test node export by importing, exporting, and comparing the result for all state files in the directory";
            cmd.args.expect({ "<node-state-dir>" });
        }

        void run(const arguments &args) const override
        {
            const auto &state_dir = args.at(0);
            std::atomic_size_t total = 0;
            std::atomic_size_t err = 0;
            for (const auto &e: std::filesystem::directory_iterator(state_dir)) {
                if (e.is_regular_file() && e.path().extension().empty()) {
                    ++total;
                    const auto orig_path = e.path().string();
                    const auto tmp_path = fmt::format("{}.{}", (e.path().parent_path() / e.path().filename()).string(), "tmp");
                    try {
                        const auto orig_hash = blake2b<blake2b_256_hash>(file::read(orig_path));
                        const auto orig_size = std::filesystem::file_size(orig_path);
                        state st {};
                        const auto tip = st.load_node(orig_path);
                        st.save_node(tmp_path, tip);
                        const auto tmp_size = std::filesystem::file_size(tmp_path);
                        const auto tmp_hash = blake2b<blake2b_256_hash>(file::read(tmp_path));
                        if (orig_hash != tmp_hash)
                            throw error(fmt::format("reserialization of {} didn't produce a byte-perfect result orig: {} bytes new: {} bytes!", orig_path, orig_size, tmp_size));
                        std::filesystem::remove(tmp_path);
                        logger::info("{} OK", orig_path);
                    } catch (const std::exception &ex) {
                        ++err;
                        logger::warn("{} failed: {}", orig_path, ex.what());
                    } catch (...) {
                        logger::warn("{} failed: an unknown error", orig_path);
                    }
                }
            }
            logger::info("tested {} snapshots, of them failed reserialization test: {}", total.load(), err.load());
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}