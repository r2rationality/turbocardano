/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/common/cert.hpp>
#include <dt/cardano/common/common.hpp>
#include <dt/cardano/common/native-script.hpp>
#include <dt/cardano/ledger/state.hpp>
#include <dt/cbor/zero2.hpp>
#include <dt/parallel/ordered-consumer.hpp>
#include <dt/parallel/ordered-queue.hpp>
#include <dt/plutus/context.hpp>
#include <dt/txwit/validator.hpp>
#include <dt/zpp-stream.hpp>

namespace daedalus_turbo::txwit {
    using namespace cardano;
    using namespace cardano::ledger;
    using namespace plutus;

    struct byron_signer_t {
        uint8_vector addr {};

        bool operator<(const byron_signer_t& o) const
        {
            return addr < o.addr;
        }
    };

    using byron_witness_t = std::variant<tx_wit_byron_vkey, tx_wit_byron_redeemer>;

    struct vkey_signer_t {
        key_hash hash {};

        bool operator<(const vkey_signer_t& o) const
        {
            return hash < o.hash;
        }
    };

    struct script_signer_t {
        script_hash hash {};
        redeemer_tag tag = redeemer_tag::spend;

        bool operator<(const script_signer_t& o) const
        {
            if (tag != o.tag)
                return tag < o.tag;
            return hash < o.hash;
        }
    };

    struct bootstrap_signer_t {
        key_hash root_hash {};
        uint8_t typ = 0;

        bool operator<(const bootstrap_signer_t& o) const
        {
            if (typ != o.typ)
                return typ < o.typ;
            return root_hash < o.root_hash;
        }
    };

    struct required_signer_t {
        using value_type = std::variant<vkey_signer_t, script_signer_t, bootstrap_signer_t>;

        value_type val;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.val);
        }

        static required_signer_t from_address(const redeemer_tag typ, const address &addr)
        {
            switch (const auto pay_id = addr.pay_id(); pay_id.type) {
                case pay_ident::ident_type::BYRON_KEY: {
                    const auto b_addr = addr.byron();
                    return { bootstrap_signer_t { b_addr.root(), b_addr.type() } };
                }
                case pay_ident::ident_type::SHELLEY_KEY:
                    return { vkey_signer_t { pay_id.hash } };
                case pay_ident::ident_type::SHELLEY_SCRIPT:
                    return { script_signer_t { pay_id.hash, typ } };
                default:
                    throw error(fmt::format("unsupported pay_ident type: {}", static_cast<int>(pay_id.type)));
            }
        }

        static required_signer_t from_cred(const credential_t &cred)
        {
            if (cred.script)
                return { script_signer_t { cred.hash, redeemer_tag::cert } };
            return { vkey_signer_t { cred.hash } };
        }

        // required only for zpp serialization methods
        required_signer_t() =default;

        required_signer_t(value_type &&v): val { std::move(v) }
        {
        }

        required_signer_t(const required_signer_t &v): val { v.val }
        {
        }

        required_signer_t(required_signer_t &&v): val { std::move(v.val) }
        {
        }

        required_signer_t(const redeemer_tag typ, const address &addr): required_signer_t { from_address(typ, addr) }
        {
        }

        required_signer_t(const credential_t cred): required_signer_t { from_cred(cred) }
        {
        }

        bool operator<(const required_signer_t &o) const
        {
            return std::visit<bool>([&](const auto &v, const auto &ov) {
                using T1 = std::decay_t<decltype(v)>;
                using T2 = std::decay_t<decltype(ov)>;
                if constexpr (!std::is_same_v<T1, T2>)
                    return val.index() < o.val.index();
                if constexpr (std::is_same_v<T1, T2>)
                    return v < ov;
            }, val, o.val);
        }
    };

    struct balances_t {
        uint64_t in_coin = 0;
        uint64_t out_coin = 0;
        multi_asset_map in_assets {};
        multi_asset_map out_assets {};

        bool match() const
        {
            return in_coin == out_coin && in_assets == out_assets;
        }
    };

    // A compact way to reference a transaction in the same batch
    // ZPP serialization does not support bit fields. Thus, the manual bit manipulations.
    struct tx_loc_t {
        tx_loc_t() =default;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self._val);
        }

        tx_loc_t(const uint8_t part_idx, const size_t tx_idx)
        {
            _val = (part_idx << 24) | (tx_idx & 0xFFFFFF);
        }

        uint8_t part_idx() const
        {
            return _val >> 24;
        }

        size_t tx_idx() const
        {
            return _val & 0xFFFFFF;
        }

        bool operator<(const tx_loc_t &o) const noexcept
        {
            return _val < o._val;
        }

        bool operator==(const tx_loc_t &o) const noexcept
        {
            return _val == o._val;
        }
    private:
        uint32_t _val = 0;
    };
    static_assert(sizeof(tx_loc_t) == 4);
}

namespace fmt {
    template<>
        struct formatter<daedalus_turbo::txwit::balances_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::txwit::balances_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using daedalus_turbo::cardano::amount;
            return fmt::format_to(
                ctx.out(),
                "in_coin: {} out_coin: {} in_assets: {} out_assets: {}",
                amount { v.in_coin }, amount { v.out_coin }, v.in_assets, v.out_assets
            );
        }
    };

    template<>
        struct formatter<daedalus_turbo::txwit::bootstrap_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::txwit::bootstrap_signer_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "bootstrap {} {}", v.typ, v.root_hash);
        }
    };

    template<>
        struct formatter<daedalus_turbo::txwit::script_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "script {} {}", v.tag, v.hash);
        }
    };

    template<>
        struct formatter<daedalus_turbo::txwit::byron_witness_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{} {}", v.typ, v.vk);
        }
    };

    template<>
        struct formatter<daedalus_turbo::txwit::byron_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "byron {}", v.addr);
        }
    };

    template<>
        struct formatter<daedalus_turbo::txwit::vkey_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "vkey {}", v.hash);
        }
    };

    template<>
        struct formatter<daedalus_turbo::txwit::required_signer_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return std::visit([&](const auto &vv) -> decltype(ctx.out()) {
                return fmt::format_to(ctx.out(), "{}", vv);
            }, v.val);
        }
    };
}

namespace daedalus_turbo::txwit {
    witness_type witness_type_from_str(const std::string_view s)
    {
        if (s == "all")
            return witness_type::all;
        if (s == "vkey")
            return witness_type::vkey;
        if (s == "script")
            return witness_type::script;
        if (s == "none")
            return witness_type::none;
        throw error(fmt::format("unsupported value of the wits options: {}", s));
    }

    struct validator {
        validator(const chunk_registry &cr, const optional_point &intersection, const optional_point &to,
                const witness_type typ, const error_handler_func &error_handler):
            _cr { cr }, _cfg { intersection, to, typ, error_handler }
        {
        }

        [[nodiscard]] optional_point validate() const
        {
            progress_guard pg { "txwit" };

            const auto proc = std::make_shared<stage2_processor>(_cr, _cfg);
            const auto batches = proc->prepare_batches();
            const auto num_batches = batches.size();

            parallel::ordered_consumer batch_consumer {
                [this, proc, num_batches](const auto part_no) {
                    timer t { fmt::format("txwit batch: {} consume_batch", part_no), logger::level::debug };
                    const auto path_main = _batch_path(_cr, part_no, "main");
                    const auto main_start = std::chrono::high_resolution_clock::now();
                    auto part = zpp::load_zstd<batch_info>(path_main);
                    logger::debug("txwit batch: {} epoch: {} seq main deserialization took {:0.5f} sec",
                        part_no, part.epoch, std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - main_start).count());
                    proc->apply_batch(std::move(part));
                    std::filesystem::remove(path_main);
                    std::filesystem::remove(_batch_dir(_cr, part_no));
                    progress::get().update_inform("txwit", part_no, num_batches);
                },
                "consumer-part", 500, _cr.sched()
            };

            logger::run_log_errors([&, proc] {
                auto stats = _process_batches(batch_consumer, batches);
                logger::debug("txwit: stage-1 txs: {} stage-2 txs: {} invalid txs: {}",
                    stats.num_simple_txs, stats.num_plutus_txs, stats.num_invalid_txs);
                logger::debug("txwit: stage-1: witnesses: {}", stats.wit_cnts);
                logger::debug("txwit: stage-2: witnesses: {}", proc->counts());
                stats.wit_cnts += proc->counts();
                logger::info("txwit: total: witnesses: {}", stats.wit_cnts);
            });
            const auto batch_end = batch_consumer.next();
            logger::debug("txwit: batch_end: {}", batch_end);
            if (batch_end > 0)
                return batches[batch_end - 1].back()->blocks.back().point();
            return _cfg.intersection;
        }
    private:
        struct param_update {
            uint64_t slot = 0;
            std::variant<param_update_proposal, param_update_vote> update {};

            bool operator<(const param_update &o) const
            {
                return slot < o.slot;
            }
        };
        using param_update_list = vector<param_update>;

        // Examples of typical protocol-parameter-based checks
        struct max_stats_t {
            std::optional<uint32_t> max_block_body_size {};
            std::optional<uint16_t> max_block_header_size {};
            std::optional<uint32_t> max_tx_size {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.max_block_body_size, self.max_block_header_size, self.max_tx_size);
            }
        };

        struct batch_stats_t {
            size_t num_simple_txs = 0;
            size_t num_plutus_txs = 0;
            size_t num_invalid_txs = 0;
            wit_cnt wit_cnts {};

            batch_stats_t &operator+=(const batch_stats_t &o)
            {
                num_simple_txs += o.num_simple_txs;
                num_plutus_txs += o.num_plutus_txs;
                num_invalid_txs += o.num_invalid_txs;
                wit_cnts += o.wit_cnts;
                return *this;
            }
        };

        struct cert_info_t {
            // default values are needed only for zpp serialization to work
            cert_t cert { stake_dereg_cert { stake_ident {} } };
            cert_loc_t loc {};
            tx_loc_t tx_loc {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.cert, self.loc, self.tx_loc);
            }
        };

        struct tx_context_t {
            tx_hash tx_id {};
            tx_loc_t tx_loc {};
            uint32_t tx_size = 0;
            uint64_t fee = 0;
            uint32_t slot = 0;
            uint8_t era = 0;
            bool reqires_genesis_delegs_quorum = false;
            balances_t balances {};
            stored_txo_list inputs {};
            stored_txo_list ref_inputs {};
            set<required_signer_t> signers {};
            set<required_signer_t> required_signers {};
            set<script_hash> native_scripts {};
            map<script_hash, std::optional<script_info>> native_script_refs {}; // filled in stage2 so does not need to be serialized
            vector<byron_witness_t> byron_signers {};
            std::optional<stored_tx_context> plutus_ctx {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.tx_id, self.tx_loc, self.tx_size, self.fee, self.slot, self.era, self.reqires_genesis_delegs_quorum,
                    self.balances, self.inputs, self.ref_inputs,
                    self.signers, self.required_signers,
                    self.native_scripts, self.byron_signers, self.plutus_ctx);
            }

            static tx_context_t from_tx(const tx_loc_t &tx_loc, const tx_base &tx)
            {
                return {
                    .tx_id=tx.hash(),
                    .tx_loc=tx_loc,
                    .tx_size=numeric_cast<uint32_t>(tx.size()),
                    .fee=tx.block().era() > 1 ? tx.fee() : 0,
                    .slot=numeric_cast<uint32_t>(tx.block().slot()),
                    .era=numeric_cast<uint8_t>(tx.block().era())
                };
            }
        };

        struct deposit_info_t {
            uint64_t in_coin = 0;
            uint64_t out_coin = 0;
        };

        struct batch_info {
            static constexpr size_t num_parts = 256;

            size_t part_id = 0;
            size_t epoch = 0;
            batch_stats_t stats {};
            // data to update the ledger state
            txo_map utxos {};
            param_update_list updates {};
            vector<cert_info_t> certs {};
            // pre-aggregated data for processing
            max_stats_t max_stats {};
            // fields are not serialized as is:
            // txs - no need to serialize as they have their own data stream
            // registered_certs - no need to serialize as they are computed on the go
            vector<vector<tx_context_t>> txs = vector<vector<tx_context_t>>(num_parts);
            map<tx_loc_t, deposit_info_t> tx_deposits {};

            static constexpr auto serialize(auto &archive, auto &self)
            {
                return archive(self.part_id, self.epoch, self.stats, self.utxos, self.updates, self.certs, self.max_stats);
            }
        };

        struct validation_config_t {
            const optional_point intersection;
            const optional_point to;
            const witness_type typ;
            const error_handler_func error_handler;

            // protocol parameter updates, UTXO data and certificates must be always processed
            // transaction data may be skipped
            void pre_aggregate_data(batch_info &part, const block_container &blk) const
            {
                auto &stats = part.stats;
                auto &max = part.max_stats;
                if (!max.max_block_body_size || *max.max_block_body_size < blk->body_size())
                    max.max_block_body_size = numeric_cast<uint32_t>(blk->body_size());
                if (!max.max_block_header_size || *max.max_block_header_size < blk->header().size())
                    max.max_block_header_size = numeric_cast<uint16_t>(blk->header().size());
                blk->foreach_update_proposal([&](const auto &prop) {
                    part.updates.emplace_back(blk->slot(), prop);
                });
                blk->foreach_update_vote([&](const auto &vote) {
                    part.updates.emplace_back(blk->slot(), vote);
                });
                const auto block_info = storage::block_info::from_block(blk);
                blk->foreach_tx([&](const tx_base &tx) {
                    const uint8_t tx_part_idx = tx.hash()[0];
                    const size_t tx_idx = part.txs[tx_part_idx].size();
                    tx_loc_t tx_loc { tx_part_idx, tx_idx };
                    auto tx_checks = tx_context_t::from_tx(tx_loc, tx);
                    if (tx.block().era() > 1)
                        tx_checks.balances.out_coin += tx.fee();
                    if (!max.max_tx_size || *max.max_tx_size < tx.raw().size())
                        max.max_tx_size = numeric_cast<uint32_t>(tx.raw().size());
                    size_t num_redeemers = 0;
                    if (const auto start_slot = tx.validity_start(); start_slot) {
                        if (*start_slot > blk->slot()) [[unlikely]]
                            throw error(fmt::format("tx {} validity start interval: {} starts after the block's slot: {}",
                                tx.hash(), *start_slot, blk->slot()));
                    }
                    if (const auto end_slot = tx.validity_end(); end_slot) {
                        if (*end_slot <= blk->slot()) [[unlikely]] {
                            // when validity_start is not defined, the validity_end slot is inclusive!
                            if (*end_slot != blk->slot() || !tx.validity_end())
                                throw error(fmt::format("tx {} validity end interval: {} ends before the block's slot: {}",
                                    tx.hash(), *end_slot, blk->slot()));
                        }
                    }
                    tx.foreach_redeemer([&](const auto &) {
                        ++num_redeemers;
                    });
                    if (num_redeemers) {
                        tx_checks.plutus_ctx.emplace(
                            tx.hash(), num_redeemers,
                            uint8_vector { tx.raw() },
                            uint8_vector { tx.witness_raw() }, block_info
                        );
                        tx.foreach_referenced_input([&](const tx_input &txi) {
                            auto &txo = tx_checks.ref_inputs.emplace_back(txi);
                            if (const auto txo_it = part.utxos.find(txo.id); txo_it != part.utxos.end())
                                txo.data = txo_it->second;
                        });
                        ++stats.num_plutus_txs;
                    } else {
                        ++stats.num_simple_txs;
                    }
                    stats.wit_cnts += witnesses_ok_stage1(blk, tx);
                    size_t cert_idx = 0;
                    tx.foreach_cert([&](const auto &cert) {
                        if (std::holds_alternative<instant_reward_cert>(cert.val))
                            tx_checks.reqires_genesis_delegs_quorum = true;
                        const cert_loc_t loc { blk->slot(), tx.index(), cert_idx++ };
                        part.certs.emplace_back(cert, loc, tx_loc);
                        if (const auto r_cred = cert.signing_cred(); r_cred)
                            tx_checks.required_signers.emplace(*r_cred);
                    });
                    tx.foreach_script([&](const auto &s) {
                        if (s.type() == script_type::native)
                            tx_checks.native_scripts.emplace(s.hash());
                    });
                    // The available reward balance is checked by the consensus verification. No need to recheck it here.
                    tx.foreach_withdrawal([&](const tx_withdrawal &withdr) {
                        if (withdr.amount) {
                            tx_checks.balances.in_coin += withdr.amount;
                            const auto stake_id = withdr.address.stake_id();
                            if (stake_id.script)
                                tx_checks.required_signers.emplace(script_signer_t { stake_id.hash, redeemer_tag::reward });
                            else
                                tx_checks.required_signers.emplace(vkey_signer_t { stake_id.hash });
                        }
                    });
                    size_t txo_idx = 0;
                    tx.foreach_output([&](const tx_output &txo) {
                        tx_checks.balances.out_coin += txo.coin;
                        if (const auto addr = txo.addr(); !addr.is_byron()) {
                            if (addr.network() != blk->config().shelley_network_id) [[unlikely]]
                                throw error(fmt::format("the network id of a shelley address: {} does not match the config: {}", addr, blk->config().shelley_network_id));
                        }
                        for (const auto &[policy_id, p_assets]: txo.assets) {
                            for (const auto &[name, coin]: p_assets) {
                                if (coin)
                                    tx_checks.balances.out_assets[policy_id][name] += coin;
                            }
                        }
                        _add_utxo(part.utxos, tx, txo, txo_idx++);
                    });
                    tx.foreach_required_signer([&](const auto vkey) {
                        tx_checks.required_signers.emplace(vkey_signer_t { vkey });
                    });
                    size_t num_inputs = 0;
                    tx.foreach_input([&](const tx_input &txin) {
                        auto &txo = tx_checks.inputs.emplace_back(txin);
                        const auto [it, created] = part.utxos.try_emplace(txo.id);
                        if (!created)
                            txo.data = it->second;
                        _del_utxo(part, it, created);
                        ++num_inputs;
                    });
                    if (!num_inputs) [[unlikely]]
                        throw error(fmt::format("tx {} does not have any inputs!", tx.hash()));
                    tx_checks.balances.out_coin += tx.donation();
                    if (const auto *c_tx = dynamic_cast<const cardano::conway::tx *>(&tx); c_tx) {
                        for (const auto &p: c_tx->proposals())
                            tx_checks.balances.out_coin += p.procedure.deposit;
                    }
                    tx.foreach_witness_byron_vkey([&](const auto &w) {
                        tx_checks.byron_signers.emplace_back(w);
                    });
                    tx.foreach_witness_shelley_vkey([&](const auto &w) {
                        tx_checks.signers.emplace(vkey_signer_t { blake2b<key_hash>(w.vkey) });
                    });
                    tx.foreach_witness_shelley_bootstrap([&](const auto &w) {
                        ed25519::vkey_full vk_full {};
                        static_assert(sizeof(vk_full) == sizeof(w.vkey) + sizeof(w.chain_code));
                        memcpy(vk_full.data(), w.vkey.data(), w.vkey.size());
                        memcpy(vk_full.data() + w.vkey.size(), w.chain_code.data(), w.chain_code.size());
                        tx_checks.signers.emplace(bootstrap_signer_t { byron_addr_root_hash(0, vk_full, w.attrs) });
                    });
                    tx.foreach_mint([&](const auto &policy_id, const auto &assets) {
                        bool minted = false;
                        for (const auto &[name, diff]: assets) {
                            // negative mint values signify an outflow of tokens
                            if (diff < 0) {
                                tx_checks.balances.out_assets[policy_id][name] += -diff;
                                minted = true;
                            } else if (diff > 0) {
                                tx_checks.balances.in_assets[policy_id][name] += diff;
                                minted = true;
                            }
                        }
                        if (minted)
                            tx_checks.required_signers.emplace(script_signer_t { policy_id, redeemer_tag::mint });
                    });
                    part.txs[tx_part_idx].emplace_back(std::move(tx_checks));
                });
                blk->foreach_invalid_tx([&](const auto &tx) {
                    ++stats.num_invalid_txs;
                    tx.foreach_collateral([&](const auto &txi) {
                        _del_utxo(part, txi);
                    });
                    if (const auto *babbage_tx = dynamic_cast<const cardano::babbage::tx *>(&tx); babbage_tx) {
                        if (const auto c_ret = babbage_tx->collateral_return(); c_ret)
                            _add_utxo(part.utxos, tx, *c_ret, babbage_tx->outputs().size());
                    }
                });
            }

            wit_cnt witnesses_ok_stage1(const block_container &blk, const tx_base &tx) const
            {
                const bool first_slot_ok = !intersection || blk.offset() >= intersection->end_offset;
                const bool last_slot_ok = !to || blk.offset() < to->end_offset;
                if (first_slot_ok && last_slot_ok) {
                    try {
                        switch (typ) {
                            case witness_type::all: {
                            case witness_type::vkey:
                                set<key_hash> valid_vkeys {};
                                auto cnts = tx.witnesses_ok_vkey(valid_vkeys);
                                cnts += tx.witnesses_ok_native(valid_vkeys);
                                return cnts;
                            }
                            case witness_type::script:
                            case witness_type::none:
                                return {};
                            default: throw error(fmt::format("unsupported witness type: {}", static_cast<int>(typ)));
                        }
                    } catch (const std::exception &ex) {
                        const auto msg = fmt::format("txwit: slot: {} tx: {} error: {}", blk->slot(), tx.hash(), ex.what());
                        logger::error("{}", msg);
                        error_handler(msg);
                    }
                }
                return {};
            }

            wit_cnt witnesses_ok_stage2(const block_base &blk, const tx_base &tx, const context &ctx) const
            {
                const bool first_slot_ok = !intersection || blk.offset() >= intersection->end_offset;
                const bool last_slot_ok = !to || blk.offset() < to->end_offset;
                wit_cnt cnts {};
                if (first_slot_ok && last_slot_ok) {
                    try {
                        switch (typ) {
                            case witness_type::all:
                            case witness_type::script:
                                cnts += tx.witnesses_ok_plutus(ctx);
                                break;
                            default:
                                break;
                        }
                    } catch (const std::exception &ex) {
                        // There are 2 known cases out 40 million mainnet Plutus evaluations
                        // when the C++ Plutus machine fails to evaluate a Plutus script.
                        // That happens when a bls12_381_g1_compress builtin gets passed a bytestring argument.
                        // According to the Plutus spec that builtin is not supposed to accept a bytestring only a bls12_381_g1_element.
                        // A Rust Plutus machine from Aiken fails exactly the same. Further investigations are non-ongoing.
                        //
                        // Since the primary focus of the current release is the proof of performance and 2 witnesses out 40 million
                        // do not impact it in any measurable way, hardcoding the list of transactions here as a temporary measure.
                        static set<tx_hash> known_cases {
                            tx_hash::from_hex("71579B77AB7D974EB31EF1B50D58F14F2CEAC2BCF540AAC50F777F56A8F24BFF"),
                            tx_hash::from_hex("E998E761F2F7F35DA12799E1F41914686FC2FE8010BAC1BE57FFCBA8F820E752")
                        };
                        const auto msg = fmt::format("txwit slot: {} tx: {} error: {}", blk.slot(), tx.hash(), ex.what());
                        if (known_cases.find(tx.hash()) == known_cases.end()) {
                            logger::error("{}", msg);
                            error_handler(msg);
                        } else {
                            logger::warn("a known incompatibility under investigation: {}", msg);
                        }
                    }
                }
                return cnts;
            }
        };

        // This component keeps its own copy of the ledger state
        // since some checks require the knowledge of the actual protocol parameters or if a given certificate is registered.
        // Therefore, the state processing is limited here. That is OK.
        // The full ledger state processing happens in the consensus validation in lib/dt/validator.cpp
        // The two pieces will be merged  after the transaction witness validation has been tested for enough time.

        struct stage2_processor {
            stage2_processor(const chunk_registry &cr, const validation_config_t &cfg): _cr { cr }, _cfg { cfg }
            {
                _st.start_epoch(0);
                if (_cfg.intersection) {
                    const auto *snap = _cr.validator().snapshots().best([&](const auto &s) { return s.end_offset <=  _cfg.intersection->end_offset; });
                    if (snap)
                        _cr.validator().load_snapshot(_st, *snap);
                }
            }

            void apply_batch(batch_info &&part)
            {
                _apply_epoch_update(part);
                _process_certs_and_param_updates(part);
                _cnts += _validate_witnesses_and_invariants(part);
                _apply_utxos(part);
            }

            const wit_cnt &counts() const
            {
                return _cnts;
            }

            size_t errors() const
            {
                return _num_errs;
            }

            vector<storage::chunk_cptr_list> prepare_batches() const
            {
                vector<storage::chunk_cptr_list> batches {};
                storage::chunk_cptr_list chunks {};
                for (const auto &[last_byte_offset, info]: _cr.chunks()) {
                    if (info.end_offset() > _st.end_offset() && (!_cfg.to || info.first_slot <= _cfg.to->slot))
                        chunks.emplace_back(&info);
                }
                logger::info("txwit: matched chunks for processing: {}", chunks.size());
                if (!chunks.empty()) {
                    if (chunks.front()->offset != _st.end_offset()) [[unlikely]]
                        throw error("internal error: failed to match available chunks with ledger snapshots");
                    logger::info("txwit: first chunk to be processed at offset: {} state end_offset: {}",
                        chunks.front()->offset, _st.end_offset());
                }

                // Bigger batches provide for a higher total throughput
                // However that requires more RAM makes the tasks occupy the scheduler workers for longer,
                // which is a problem for latency sensitive tasks of the second stage
                static constexpr size_t batch_size = 2;
                storage::chunk_cptr_list batch {};
                for (const auto *chunk: chunks) {
                    // ensure that each batch has between 1 and batch_size elements and all from the same epoch
                    if (batch.size() == batch_size || (!batch.empty() && _cr.make_slot(batch.front()->first_slot).epoch() != _cr.make_slot(chunk->first_slot).epoch())) {
                        batches.emplace_back(std::move(batch));
                        batch.clear();
                    }
                    batch.emplace_back(chunk);
                }
                if (!batch.empty())
                    batches.emplace_back(std::move(batch));
                logger::debug("txwit: prepared chunk batches: {}", batches.size());
                return batches;
            }
        private:
            const chunk_registry &_cr;
            const validation_config_t &_cfg;
            state _st {};
            plutus_cost_models _cost_models_raw = _st.params().plutus_cost_models;
            costs::parsed_models _cost_models = costs::parse(_cost_models_raw);
            wit_cnt _cnts {};
            size_t _num_errs = 0;

            void _apply_epoch_update(const batch_info &part)
            {
                if (part.epoch > _st.epoch()) {
                    timer t { fmt::format("txwit batch: {} epoch: {} apply_epoch_update", part.part_id, part.epoch), logger::level::debug };
                    if (part.epoch != _st.epoch() + 1) [[unlikely]]
                        throw error(fmt::format("unexpected epoch: {} after: {}", part.epoch, _st.epoch()));
                    _st.start_epoch(part.epoch);
                    if (_st.params().plutus_cost_models != _cost_models_raw) {
                        _cost_models_raw = _st.params().plutus_cost_models;
                        _cost_models = costs::parse(_cost_models_raw);
                    }
                }
            }

            void _process_certs_and_param_updates(batch_info &part)
            {
                timer t { fmt::format("txwit batch: {} epoch: {} seq process_certs_and_param_updates", part.part_id, part.epoch), logger::level::debug };
                for (const auto &cert: part.certs) {
                    std::visit([&](const auto &c) {
                        using T = std::decay_t<decltype(c)>;
                        if constexpr (std::is_same_v<T, stake_reg_cert>) {
                            if (!_st.has_stake(c.stake_id))
                                part.tx_deposits[cert.tx_loc].out_coin += _st.params().key_deposit;
                        } else if constexpr (std::is_same_v<T, reg_cert>
                                || std::is_same_v<T, stake_reg_deleg_cert>
                                || std::is_same_v<T, vote_reg_deleg_cert>
                                || std::is_same_v<T, stake_vote_reg_deleg_cert>) {
                            if (!_st.has_stake(c.stake_id))
                                part.tx_deposits[cert.tx_loc].out_coin += c.deposit;
                        } else if constexpr (std::is_same_v<T, stake_dereg_cert>) {
                            part.tx_deposits[cert.tx_loc].in_coin += _st.params().key_deposit;
                        } else if constexpr (std::is_same_v<T, unreg_cert>) {
                            part.tx_deposits[cert.tx_loc].in_coin += c.deposit;
                        } else if constexpr (std::is_same_v<T, pool_reg_cert>) {
                            if (!_st.has_pool(c.pool_id))
                                part.tx_deposits[cert.tx_loc].out_coin += _st.params().pool_deposit;
                        } else if constexpr (std::is_same_v<T, reg_drep_cert>) {
                            if (!_st.has_drep(c.drep_id))
                                part.tx_deposits[cert.tx_loc].out_coin += c.deposit;
                        } else if constexpr (std::is_same_v<T, unreg_drep_cert>) {
                            part.tx_deposits[cert.tx_loc].in_coin += c.deposit;
                        }
                    }, cert.cert.val);
                    _st.process_cert(cert.cert, cert.loc);
                }
                for (const auto &pu: part.updates) {
                    std::visit([&](const auto &u) {
                        using T = std::decay_t<decltype(u)>;
                        if constexpr (std::is_same_v<T, param_update_proposal>) {
                            _st.propose_update(pu.slot, u);
                            //std::cout << fmt::format("update proposal slot: {} data: {}\n", e.slot, std::get<cardano::param_update_proposal>(e.update));
                        } else if constexpr (std::is_same_v<T, param_update_vote>) {
                            //std::cout << fmt::format("update vote slot: {} data: {}\n", e.slot, std::get<cardano::param_update_vote>(e.update));
                            _st.proposal_vote(pu.slot, u);
                        } else {
                            throw error(fmt::format("unsupported parameter update type: {}", typeid(T).name()));
                        }
                    }, pu.update);
                }
            }

            void _apply_utxos(batch_info &part)
            {
                timer t { fmt::format("txwit batch: {} epoch: {} par apply_utxos", part.part_id, part.epoch), logger::level::debug };
                static const std::string task_id { "resolve-apply-utxos" };
                auto &sched = _cr.sched();
                sched.wait_all_done(task_id, part.utxos.num_parts, [&] {
                    for (size_t pi = 0; pi < part.utxos.num_parts; ++pi) {
                        sched.submit_void(task_id, 2000, [&, pi] {
                            auto &utxo_part =  const_cast<txo_map &>(_st.utxos()).partition(pi);
                            auto &ue_part = part.utxos.partition(pi);
                            for (auto &&[txo_id, txo_data]: ue_part) {
                                if (txo_data) {
                                    if (auto [it, created] = utxo_part.try_emplace(txo_id, std::move(txo_data)); !created) [[unlikely]]
                                        logger::warn("txwit: epoch: {} a non-unique TXO {}!", part.epoch, it->first);
                                } else {
                                    if (auto it = utxo_part.find(txo_id); it != utxo_part.end()) [[likely]] {
                                        utxo_part.erase(it);
                                    } else {
                                        throw error(fmt::format("epoch: {} request to remove an unknown TXO {}!", part.epoch, txo_id));
                                    }
                                }
                            }
                        });
                    }
                });
            }

            std::unique_ptr<context> _prep_plutus_ctx(tx_context_t &tx) const
            {
                const auto &utxos = _st.utxos();
                // process inputs before they are moved into the plutus::context
                for (auto &[id, data]: tx.inputs) {
                    if (!data) {
                        const auto it = utxos.find(id);
                        if (it == utxos.end()) [[unlikely]]
                            throw error(fmt::format("tx {} references an unknown TXO {}!", tx.tx_id, id));
                        data = it->second;
                    }
                    tx.balances.in_coin += data.coin;
                    for (const auto &[policy_id, assets]: data.assets) {
                        for (const auto &[name, coin]: assets) {
                            if (coin)
                                tx.balances.in_assets[policy_id][name] += coin;
                        }
                    }
                    if (data.script_ref) {
                        if (data.script_ref->type() == script_type::native)
                            tx.native_script_refs.try_emplace(data.script_ref->hash(), *data.script_ref);
                    }
                }
                for (auto &[id, data]: tx.ref_inputs) {
                    if (!data) {
                        const auto it = utxos.find(id);
                        if (it == utxos.end()) [[unlikely]]
                            throw error(fmt::format("tx {} references an unknown TXO {}!", tx.tx_id, id));
                        data = it->second;
                    }
                    if (data.script_ref) {
                        if (data.script_ref->type() == script_type::native)
                            tx.native_script_refs.try_emplace(data.script_ref->hash(), *data.script_ref);
                    }
                }
                if (tx.plutus_ctx) {
                    auto p_ctx = std::make_unique<context>(
                        std::move(tx.plutus_ctx->body), std::move(tx.plutus_ctx->wits),
                        tx.plutus_ctx->block, _cr.config()
                    );
                    tx.plutus_ctx.reset();
                    p_ctx->set_inputs(std::move(tx.inputs), std::move(tx.ref_inputs));
                    return p_ctx;
                }
                return nullptr;
            }

            void _validate_byron_tx_invariants(const batch_info &part, tx_context_t &tx) const
            {
                // In Byron the difference between the inputs and the outputs is the fee, so not check that
                size_t byron_input_idx = 0;
                for (const auto &[id, data]: tx.inputs) {
                    const auto b_addr = byron_addr::from_bytes(data.address_raw);
                    std::visit([&](const auto &w) {
                        using T = std::decay_t<decltype(w)>;
                        if constexpr (std::is_same_v<T, tx_wit_byron_vkey>) {
                            if (!b_addr.vkey_ok(w.vkey, 0)) [[unlikely]]
                                throw error(fmt::format("epoch: {} slot: {} tx {} the byron witness #{} does not match the address: {}!",
                                    part.epoch, tx.slot, tx.tx_id, byron_input_idx, b_addr));
                        } else if constexpr (std::is_same_v<T, tx_wit_byron_redeemer>) {
                            if (!b_addr.vkey_ok(w.vkey, 2)) [[unlikely]]
                                throw error(fmt::format("epoch: {} slot: {} tx {} the byron witness #{} does not match the address: {}!",
                                    part.epoch, tx.slot, tx.tx_id, byron_input_idx, b_addr));
                        } else {
                            throw error(fmt::format("unsupported byron signer type: {}", typeid(T).name()));
                        }
                    }, tx.byron_signers.at(byron_input_idx));
                    ++byron_input_idx;
                }
            }

            void _validate_shelley_tx_invariants(const batch_info &part, tx_context_t &tx, const context *plutus_ctx) const
            {
                if (const auto it = part.tx_deposits.find(tx.tx_loc); it != part.tx_deposits.end()) {
                    tx.balances.in_coin += it->second.in_coin;
                    tx.balances.out_coin += it->second.out_coin;
                }
                if (!tx.balances.match()) [[unlikely]]
                    throw error(fmt::format("tx {}: consumed != produced: {}", tx.tx_id, tx.balances));
                if (plutus_ctx) {
                    for (const auto &[rid, rdata]: plutus_ctx->redeemers())
                        tx.signers.emplace(script_signer_t { plutus_ctx->redeemer_script(rid), rid.tag });
                }
                std::optional<set<key_hash>> vkey_signers {};
                for (const auto &rs: tx.required_signers) {
                    if (tx.signers.contains(rs))
                        continue;
                    if (std::holds_alternative<script_signer_t>(rs.val)) {
                        const auto &s_hash = std::get<script_signer_t>(rs.val).hash;
                        if (tx.native_scripts.contains(s_hash))
                            continue;
                        // validate references native scripts here since they do not have their own tx_witness entries
                        // the optional in s_it->second has value only the first time a given script is referenced
                        if (const auto s_it = tx.native_script_refs.find(s_hash); s_it != tx.native_script_refs.end() && s_it->second) {
                            if (!vkey_signers) {
                                vkey_signers.emplace();
                                for (const auto &s: tx.signers) {
                                    if (std::holds_alternative<vkey_signer_t>(s.val))
                                        vkey_signers->emplace(std::get<vkey_signer_t>(s.val).hash);
                                }
                                const auto &script = *s_it->second;
                                if (const auto err = native_script::validate(cbor::zero2::parse(script.script()).get(), tx.slot, *vkey_signers); err) [[unlikely]]
                                    throw error(fmt::format("native script: {} failed to validate tx {}: {}", script.hash(), tx.tx_id, err));
                            }
                            s_it->second.reset();
                            continue;
                        }
                    }
                    throw error(fmt::format("epoch: {} tx {} missing a required_signer: {}", part.epoch, tx.tx_id, rs));
                }
                if (tx.reqires_genesis_delegs_quorum) [[unlikely]] {
                    size_t num_signers = 0;
                    for (const auto &vk_hash: _cr.config().byron_delegate_hashes) {
                        if (tx.signers.contains({ vkey_signer_t { vk_hash } }))
                            ++num_signers;
                    }
                    const auto quorum = _cr.config().shelley_update_quorum;
                    if (num_signers < quorum) [[unlikely]]
                        throw error(fmt::format("a quorum of {} genesis delegates is required but got only: {}", quorum, num_signers));
                    logger::debug("epoch: {} tx: {} requires a quorum of {} genesis delegates and got {}",
                        part.epoch, tx.tx_id, quorum, num_signers);
                }
            }

            void _validate_tx_invariants(const batch_info &part, tx_context_t &tx, const context *plutus_ctx) const
            {
                try {
                    const auto min_fee = _st.params().min_fee_a * tx.tx_size + _st.params().min_fee_b;
                    if (tx.fee < min_fee) [[unlikely]]
                        throw error(fmt::format("epoch: {} tx {} an insufficient fee for a tx of size {}: {} < {}",
                            part.epoch, tx.tx_id, static_cast<size_t>(tx.tx_size), tx.fee, min_fee));
                    switch (tx.era) {
                        case 0:
                            throw error(fmt::format("transaction {} in era 0!", tx.tx_id));
                        case 1:
                            _validate_byron_tx_invariants(part, tx);
                            break;
                        default:
                            _validate_shelley_tx_invariants(part, tx, plutus_ctx);
                            break;
                    }
                } catch (const std::exception &ex) {
                    const auto msg = fmt::format("txwit epoch: {} tx: {}: {}", part.epoch, tx.tx_id, ex.what());
                    logger::error("{}", msg);
                    _cfg.error_handler(msg);
                }
            }

            wit_cnt _validate_witnesses(batch_info &part) const
            {
                timer t { fmt::format("txwit batch: {} epoch: {} par validate_witnesses", part.part_id, part.epoch), logger::level::debug };
                static const std::string task_id { "validate-batch" };
                auto &sched = _cr.sched();
                mutex::unique_lock::mutex_type part_mutex alignas(mutex::alignment) {};
                wit_cnt cnts {};
                sched.wait_all_done(task_id, batch_info::num_parts, [&] {
                    for (size_t pi = 0; pi < batch_info::num_parts; ++pi) {
                        sched.submit_void(task_id, 2000, [&, pi] {
                            wit_cnt batch_cnts {};
                            const auto part_path = _batch_path(_cr, part.part_id, fmt::format("txs-{:02X}", pi));
                            auto txs = zpp::load_zstd<vector<tx_context_t>>(part_path);
                            for (auto &tx_ctx: txs) {
                                std::unique_ptr<context> plutus_ctx {};
                                plutus_ctx = _prep_plutus_ctx(tx_ctx);
                                _validate_tx_invariants(part, tx_ctx, plutus_ctx.get());
                                if (plutus_ctx) {
                                    plutus_ctx->cost_models(_cost_models);
                                    const auto &tx = plutus_ctx->tx();
                                    batch_cnts += _cfg.witnesses_ok_stage2(tx.block(), tx, *plutus_ctx);
                                }
                            }
                            // delete only when all txs validate to have the data for error analysis
                            std::filesystem::remove(part_path);
                            mutex::scoped_lock lk { part_mutex };
                            cnts += batch_cnts;
                        });
                    }
                });
                return cnts;
            }

            void _validate_max_stats(const batch_info &part) const
            {
                const auto &max = part.max_stats;
                if (max.max_block_body_size && *max.max_block_body_size > _st.params().max_block_body_size) [[unlikely]]
                    throw error(fmt::format("block body size of {} exceeded the limit of {}", *max.max_block_body_size, _st.params().max_block_body_size));
                if (max.max_block_header_size && *max.max_block_header_size > _st.params().max_block_header_size) [[unlikely]]
                    throw error(fmt::format("block header size of {} exceeded the limit of {}", *max.max_block_header_size, _st.params().max_block_header_size));
                if (max.max_tx_size && *max.max_tx_size > _st.params().max_transaction_size) [[unlikely]]
                    throw error(fmt::format("tx size of {} exceeded the limit of {}", *max.max_tx_size, _st.params().max_transaction_size));
            }

            wit_cnt _validate_witnesses_and_invariants(batch_info &part)
            {
                _validate_max_stats(part);
                return _validate_witnesses(part);
            }
        };

        const chunk_registry &_cr;
        const validation_config_t _cfg;

        static batch_stats_t _process_batch_stage1(const chunk_registry &cr, const size_t batch_no, const storage::chunk_cptr_list &batch, const validation_config_t &cfg)
        {
            if (batch.empty()) [[unlikely]]
                throw error(fmt::format("batch {} is empty!", batch_no));
            batch_info part { batch_no, cr.make_slot(batch.front()->first_slot).epoch() };
            for (const auto *chunk_ptr: batch) {
                const auto &chunk = *chunk_ptr;
                const auto first_epoch = cr.make_slot(chunk.first_slot).epoch();
                const auto last_epoch = cr.make_slot(chunk.last_slot).epoch();
                if (first_epoch != part.epoch || last_epoch != part.epoch) [[unlikely]]
                    throw error(fmt::format("batch: {} contains data from multiple epochs: {}, {}, {}", batch_no, part.epoch, first_epoch, last_epoch));

                const auto canon_path = cr.full_path(chunk.rel_path());
                const auto data = zstd::read(canon_path);
                cbor::zero2::decoder dec { data };
                while (!dec.done()) {
                    auto &block_tuple = dec.read();
                    const block_container blk { numeric_cast<uint64_t>(chunk.offset + block_tuple.data_begin() - data.data()), block_tuple, cr.config() };
                    // Byron epoch boundary blocks contain no information and therefore are skipped
                    if (blk->era() > 0) [[likely]] {
                        try {
                            cfg.pre_aggregate_data(part, blk);
                        } catch (const std::exception &ex) {
                            throw error(fmt::format("failed to parse block at slot: {} hash: {}: {}", blk->slot_object(), blk->hash(), ex.what()));
                        }
                    }
                }
            }
            for (size_t pi = 0; pi < batch_info::num_parts; ++pi) {
                const auto part_path = _batch_path(cr, part.part_id, fmt::format("txs-{:02X}", pi));
                zpp::save_zstd(part_path, part.txs[pi]);
            }
            const auto path_main = _batch_path(cr, part.part_id, "main");
            zpp::save_zstd(path_main, part);
            return part.stats;
        }

        static void _del_utxo(batch_info &part, const txo_map::iterator it, bool created)
        {
            // If a txo is created and consumed within the same chunk, no need to report it further.
            if (!created) {
                if (it->second) [[likely]] {
                    part.utxos.erase(it);
                } else {
                    throw error(fmt::format("found a non-unique TXO in the same chunk {}", it->first));
                }
            }
        }

        static void _del_utxo(batch_info &part, const tx_out_ref &txo_id)
        {
            auto [it, created] = part.utxos.try_emplace(txo_id);
            _del_utxo(part, it, created);
        }

        static void _add_utxo(txo_map &idx, const tx_base &tx, const tx_output &txo, const size_t txo_idx)
        {
            if (const auto [it, created] = idx.try_emplace(tx_out_ref { tx.hash(), txo_idx }, txo); !created) [[unlikely]]
                throw error(fmt::format("found a non-unique TXO {}#{}", tx.hash(), txo_idx));
        }

        static std::filesystem::path _batch_dir(const chunk_registry &cr, const size_t batch_id)
        {
            return cr.data_dir() / "txwit" / fmt::format("batch-{:05}", batch_id);
        }

        static std::string _batch_path(const chunk_registry &cr, const size_t batch_id, const std::string_view suffix)
        {
            return (_batch_dir(cr, batch_id)/ fmt::format("{}.zpp", suffix) ).string();
        }

        batch_stats_t _process_batches(parallel::ordered_consumer &part_c, const vector<storage::chunk_cptr_list> &batches) const
        {
            mutex::unique_lock::mutex_type all_mutex alignas(mutex::alignment) {};
            batch_stats_t all {};
            auto &sched = _cr.sched();
            static const std::string task_id { "parse" };
            const auto ex_ptr = logger::run_log_errors([&] {
                std::shared_ptr<parallel::ordered_queue> part_q = std::make_shared<parallel::ordered_queue>();
                for (size_t bi = 0; bi < batches.size(); ++bi) {
                    sched.submit_void(task_id, -static_cast<int64_t>(bi), [&, bi] {
                        try {
                            if (!part_c.cancel()) {
                                {
                                    auto stats = _process_batch_stage1(_cr, bi, batches[bi], _cfg);
                                    mutex::scoped_lock lk { all_mutex };
                                    all += stats;
                                }
                                part_q->put(bi);
                                part_q->take_all();
                                part_c.try_push(part_q->next());
                            }
                        } catch (const std::exception &ex) {
                            // cancel all scheduled parse tasks for batches after this one
                            // since now we know that the work cannot be incorporated
                            sched.cancel([bi](const auto &t_task_id, const auto &t_param) {
                                return task_id == t_task_id && t_param && std::any_cast<size_t>(*t_param) >= bi;
                            });
                            vector<std::string> chunk_info {};
                            for (const auto *c_ptr: batches[bi])
                                chunk_info.emplace_back(fmt::format("{} first slot: {}", c_ptr->rel_path(), c_ptr->first_slot));
                            const auto msg = fmt::format("batch {}: pre-processing failed: {} chunks: {}", bi, ex.what(), chunk_info);
                            throw error(msg);
                        }
                    }, bi);
                }
                // let the running consumer tasks to finish
                sched.process(true);
                logger::debug("txwit: exited the processs_batches loop part_q: {} part_c: {}", part_q->next(), part_c.next());
                part_q->take_all();
                progress::get().update_inform("txwit", part_c.next(), batches.size());
                logger::debug("txwit: pushed the remaining parts part_q: {} part_c: {}", part_q->next(), part_c.next());
                if (part_c.next() < part_q->next()) {
                    if (!part_c.try_push(part_q->next())) [[unlikely]]
                        throw error("failed to schedule the final work items");
                    sched.process(true);
                }
                logger::debug("txwit: consumed the remaining parts part_q: {} part_c: {}", part_q->next(), part_c.next());
            });
            // ensure there are no runaway tasks
            if (ex_ptr)
                sched.process(true);
            return all;
        }
    };

    optional_point validate(const chunk_registry &cr, const optional_point &intersection, const optional_point &to, const witness_type typ,
        const error_handler_func &error_handler)
    {
        validator v { cr, intersection, to, typ, error_handler };
        return v.validate();
    }
}