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

namespace turbo::cli::pool_certs {
    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "pool-certs";
            cmd.desc = "print pool certificates";
            cmd.args.expect({ "<data-dir>" });
        }

        void run(const arguments &args) const override
        {
            const chunk_registry cr { args.at(0), chunk_registry::mode::store };
            std::mutex all_mutex alignas(mutex::alignment) {};
            part_info_t all {};
            storage::parse_parallel_chunk<part_info_t>(cr,
                [&](auto &part, const auto &blk) {
                    size_t tx_idx = 0;
                    for (const auto &tx: blk->txs()) {
                        size_t cert_idx = 0;
                        for (const auto &cert: tx->certs()) {
                            std::visit([&](const auto &cv) {
                                using T = std::decay_t<decltype(cv)>;
                                if constexpr (std::is_same_v<T, cardano::pool_reg_cert> || std::is_same_v<T, cardano::pool_retire_cert>) {
                                    part.certs.emplace_back(cert, cardano::cert_loc_t { blk->slot(), tx_idx, cert_idx++ });
                                }
                            }, cert.val);
                        }
                        ++tx_idx;
                    }
                },
                [&](size_t, const auto &) {
                    return part_info_t {};
                },
                [&](auto &&part, size_t, const auto &) {
                    mutex::scoped_lock lk { all_mutex };
                    all.certs.reserve(part.certs.size() + all.certs.size());
                    all.certs.insert(all.certs.end(), part.certs.begin(), part.certs.end());
                },
                "parse-chunks"
            );
            std::sort(all.certs.begin(), all.certs.end());
            for (const auto &c: all.certs) {
                logger::info("{},{},{}: {}", cr.make_slot(c.loc.slot), c.loc.tx_idx, c.loc.cert_idx, c.cert);
            }
        }
    private:
        struct cert_info_t {
            cardano::cert_t cert;
            cardano::cert_loc_t loc;

            bool operator<(const cert_info_t &other) const
            {
                return loc < other.loc;
            }
        };

        struct part_info_t {
            std::vector<cert_info_t> certs {};
        };
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
