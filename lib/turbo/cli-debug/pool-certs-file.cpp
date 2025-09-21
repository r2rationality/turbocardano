/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cli/common.hpp>
#include <turbo/cardano.hpp>
#include <turbo/cbor/zero2.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/storage/partition.hpp>

namespace turbo::cli::pool_certs_file {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "pool-certs-file";
            cmd.desc = "print pool certificates in a file";
            cmd.args.expect({ "<chunk-path>" });
        }

        void run(const arguments &args) const override
        {
            const auto chunk = file::read_auto(args.at(0));
            cbor::zero2::decoder dec { chunk };

            while (!dec.done()) {
                auto &block_tuple = dec.read();
                const cardano::block_container blk { numeric_cast<uint64_t>(block_tuple.data_begin() - chunk.data()), block_tuple };
                size_t tx_idx = 0;
                for (const auto &tx: blk->txs()) {
                    size_t cert_idx = 0;
                    for (const auto &cert: tx->certs()) {
                        std::visit([&](const auto &cv) {
                            using T = std::decay_t<decltype(cv)>;
                            if constexpr (std::is_same_v<T, cardano::pool_reg_cert> || std::is_same_v<T, cardano::pool_retire_cert>) {
                                logger::info("{},{},{}: {}", blk->slot_object(), tx_idx, cert_idx++, cv);
                            }
                        }, cert.val);
                    }
                    ++tx_idx;
                }
            }
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
