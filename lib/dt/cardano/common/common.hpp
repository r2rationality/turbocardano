#pragma once
#ifndef DAEDALUS_TURBO_CARDANO_COMMON_HPP
#define DAEDALUS_TURBO_CARDANO_COMMON_HPP
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <variant>
#include <dt/array.hpp>
#include <dt/cardano/common/types.hpp>
#include <dt/cardano/common/cert.hpp>
#include <dt/cardano/common/config.hpp>
#include <dt/container.hpp>
#include <dt/file.hpp>
#include <dt/common/format.hpp>
#include <dt/common/bytes.hpp>
#include <dt/rational.hpp>

namespace daedalus_turbo::plutus {
    struct context;
    struct term_list;
}

namespace daedalus_turbo::cardano {
    static constexpr uint64_t density_default_window = 9600; // slots

    struct amount_asset: amount {
        buffer policy_id;
        std::string_view asset {};

        inline json::value to_json() const;
    };

    struct balance_change {
        int64_t change { 0 };

        operator int64_t() const
        {
            return change;
        }

        inline json::value to_json() const;
    };

    struct multi_balance: map<std::string, uint64_t> {
        using base_type = map<std::string, uint64_t>;
        using map::map;

        json::object to_json(size_t offset=0, size_t max_items=1000) const;
    };

    using multi_balance_change = map<std::string, int64_t>;

    struct tx_size {
        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self._size);
        }

        tx_size(): _size { 0 }
        {
        }
        
        tx_size(size_t sz)
        {
            size_t packed_sz = sz >> 8;
            if (packed_sz >= 256 || (packed_sz == 255 && sz & 0xFF)) throw error(fmt::format("tx size is too big: {}!", sz));
            if (sz & 0xFF) ++packed_sz;
            _size = (uint8_t)packed_sz;
        }

        operator std::size_t() const
        {
            return (size_t)(_size) << 8;
        }
    private:
        uint8_t _size;
    };

    struct cert_idx {

        cert_idx(): _idx { 0 } {}

        cert_idx(size_t idx)
        {
            if (idx >= (1U << 16))
                throw error(fmt::format("tx out idx is too big: {}!", idx));
            _idx = idx;
        }

        cert_idx &operator=(size_t idx)
        {
            if (idx >= (1U << 16))
                throw error(fmt::format("tx out idx is too big: {}!", idx));
            _idx = idx;
            return *this;
        }

        operator std::size_t() const
        {
            return _idx;
        }
    private:
        uint16_t _idx;
    };

    struct tx_withdrawal {
        const cardano::address address;
        const cardano::amount amount;
    };

    enum class redeemer_tag: uint8_t {
        spend, mint, cert, reward, vote, propose
    };

    extern redeemer_tag redeemer_tag_from_cbor(cbor::zero2::value &v);

    struct redeemer_id {
        redeemer_tag tag;
        uint16_t ref_idx;

        bool operator<(const redeemer_id &o) const
        {
            if (tag != o.tag)
                return tag < o.tag;
            return ref_idx < o.ref_idx;
        }
    };

    struct redeemer_info {
        buffer data;
        ex_units budget;
    };

    struct tx_redeemer {
        redeemer_tag tag;
        uint16_t ref_idx;
        uint8_vector data;
        ex_units budget;

        static tx_redeemer from_cbor(cbor::zero2::array_reader &it);
        static tx_redeemer from_cbor(cbor::zero2::map_reader &it);

        redeemer_id id() const
        {
            return { tag, ref_idx };
        }
    };

    struct kes_signature {
        const buffer vkey_hot;
        const buffer vkey_sig;
        const buffer vkey_cold; // issuer_vkey
        const buffer sig;
        const buffer header_body;
        uint64_t counter = 0;
        uint64_t period = 0;
        uint64_t slot = 0;

        bool verify() const;
    };

    struct block_vrf {
        const buffer vkey;
        const buffer leader_result;
        const buffer leader_proof;
        const buffer nonce_result;
        const buffer nonce_proof;
    };

    struct block_header_base {
        block_header_base(const uint64_t era, const cardano::config &cfg):
            _era { numeric_cast<uint8_t>(era) }, _cfg { cfg }
        {
        }

        uint64_t era() const
        {
            return _era;
        }

        size_t size() const
        {
            return data_raw().size();
        }

        const cardano::config &config() const
        {
            return _cfg;
        }

        point2 point2() const
        {
            return { slot(), hash() };
        }

        virtual ~block_header_base() =default;
        virtual const buffer &data_raw() const =0;
        virtual uint64_t height() const =0;
        virtual const block_hash &hash() const =0;
        virtual const block_hash &prev_hash() const =0;
        virtual uint64_t slot() const =0;
        virtual protocol_version protocol_ver() const =0;
        virtual buffer issuer_vkey() const =0;

        virtual buffer protocol_magic_raw() const
        {
            thread_local cbor::encoder enc {};
            enc.cbor().clear();
            enc.uint(_cfg.byron_protocol_magic);
            return enc.cbor();
        }
    protected:
        const uint8_t _era;
        const cardano::config &_cfg;
    };

    struct tx_base;

    using tx_list = vector<tx_base*>;

    using tx_observer_t = std::function<void(const tx_base &)>;
    using update_observer_t = std::function<void(const param_update_proposal &)>;
    using update_vote_observer_t = std::function<void(const param_update_vote &)>;

    struct invalid_tx_set: set_t<uint16_t> {
        using base_type = set_t<uint16_t>;

        buffer raw;

        invalid_tx_set() =default;
        invalid_tx_set(cbor::zero2::value &v);
    };

    template<typename TX>
    struct block_tx_list {
        vector<TX> txs;
        buffer raw;
        buffer wits_raw;
        vector<tx_base *> txs_view;

        block_tx_list(block_tx_list &&o):
            txs { std::move(o.txs) },
            raw { o.raw },
            wits_raw { o.wits_raw },
            txs_view { std::move(o.txs_view) }
        {
        }

        block_tx_list(vector<TX> &&t, const buffer &traw, const buffer &wraw):
            txs { std::move(t) },
            raw { traw },
            wits_raw { wraw }
        {
            txs_view.reserve(txs.size());
            for (auto &tx: txs)
                txs_view.push_back(&tx);
        }
    };

    struct block_base {
        block_base(const block_base &) =delete;
        block_base(block_base &&) =delete;
        block_base() =delete;

        block_base(const uint64_t offset, const uint64_t hdr_offset):
            _offset { offset }, _hdr_offset { numeric_cast<uint16_t>(hdr_offset) }
        {
        }

        virtual ~block_base() =default;
        virtual const block_header_base &header() const =0;
        virtual const tx_list &txs() const =0;
        virtual uint32_t body_size() const =0;

        uint16_t header_offset() const
        {
            return _hdr_offset;
        }

        uint64_t offset() const
        {
            return _offset;
        }

        uint64_t slot() const
        {
            return header().slot();
        }

        cardano::slot slot_object() const
        {
            return { slot(), config() };
        }

        point2 point2() const
        {
            return header().point2();
        }

        uint64_t era() const
        {
            return header().era();
        };

        uint64_t height() const
        {
            return header().height();
        }

        const block_hash &hash() const
        {
            return header().hash();
        }

        const block_hash &prev_hash() const
        {
            return header().prev_hash();
        }

        const protocol_version protocol_ver() const
        {
            return header().protocol_ver();
        }

        buffer issuer_vkey() const
        {
            return header().issuer_vkey();
        }

        pool_hash issuer_hash() const
        {
            return blake2b<pool_hash>(issuer_vkey());
        }

        const cardano::config &config() const
        {
            return header().config();
        }

        void foreach_tx(const tx_observer_t &) const;
        void foreach_invalid_tx(const tx_observer_t &) const;
        size_t tx_count() const;

        virtual const invalid_tx_set &invalid_txs() const
        {
            static const invalid_tx_set empty_set {};
            return empty_set;
        }

        virtual void foreach_update_proposal(const update_observer_t &) const
        {
        }

        virtual void foreach_update_vote(const update_vote_observer_t &) const
        {
        }

        virtual const kes_signature kes() const
        {
            throw error("cardano::block_base::kes is not unsupported");
        }

        virtual const block_vrf vrf() const
        {
            throw error("cardano::block_base::block_vrf is not unsupported");
        }

        virtual bool body_hash_ok() const
        {
            throw error("cardano::block_base::body_hash is not unsupported");
        }

        virtual bool signature_ok() const
        {
            throw error("cardano::block_base::signature_ok is not unsupported");
        }
    protected:
        void mark_invalid_tx(size_t idx);
    private:
        uint64_t _offset;
        uint16_t _hdr_offset;
    };

    struct tx_wit_byron_vkey {
        ed25519::vkey_full vkey {};
        ed25519::signature sig {};

        static tx_wit_byron_vkey from_cbor(cbor::zero2::value &v);
    };

    struct tx_wit_byron_redeemer {
        ed25519::vkey vkey {};
        ed25519::signature sig {};

        static tx_wit_byron_redeemer from_cbor(cbor::zero2::value &v);
    };

    struct tx_wit_shelley_vkey {
        ed25519::vkey vkey {};
        ed25519::signature sig {};

        static tx_wit_shelley_vkey from_cbor(cbor::zero2::value &);
    };

    struct tx_wit_shelley_bootstrap {
        ed25519::vkey vkey {};
        ed25519::signature sig {};
        ed25519::vkey chain_code {};
        uint8_vector attrs {};

        static tx_wit_shelley_bootstrap from_cbor(cbor::zero2::value &);
    };

    struct tx_wit_datum {
        datum_hash hash {};
        uint8_vector data {};

        static tx_wit_datum from_cbor(cbor::zero2::value &);
    };

    using tx_wit_base_t = std::variant<
        tx_wit_byron_vkey, tx_wit_byron_redeemer,
        tx_wit_shelley_vkey, tx_wit_shelley_bootstrap,
        tx_redeemer, tx_wit_datum,
        script_info>;
    struct tx_wit: tx_wit_base_t {
        using tx_wit_base_t::tx_wit_base_t;
    };
    using tx_wit_list = vector<tx_wit>;

    struct block_meta_map {
        buffer raw;

        block_meta_map(cbor::zero2::value &v):
            raw { v.data_raw() }
        {
        }
    };

    using tail_relative_stake_map = map<point, double>;

    struct wit_cnt {
        size_t vkey = 0;
        size_t native_script = 0;
        size_t plutus_v1_script = 0;
        size_t plutus_v2_script = 0;
        size_t plutus_v3_script = 0;

        wit_cnt &operator+=(const script_type &typ)
        {
            switch (typ) {
                case script_type::native: ++native_script; break;
                case script_type::plutus_v1: ++plutus_v1_script; break;
                case script_type::plutus_v2: ++plutus_v2_script; break;
                case script_type::plutus_v3: ++plutus_v3_script; break;
                default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(typ)));
            }
            return *this;
        }

        wit_cnt &operator+=(const wit_cnt &o)
        {
            vkey += o.vkey;
            native_script += o.native_script;
            plutus_v1_script += o.plutus_v1_script;
            plutus_v2_script += o.plutus_v2_script;
            plutus_v3_script += o.plutus_v3_script;
            return *this;
        }

        operator bool() const noexcept
        {
            return vkey + native_script + plutus_v1_script + plutus_v2_script + plutus_v3_script;
        }
    };

    using byron_vkey_witness_t = std::variant<tx_wit_byron_vkey, tx_wit_byron_redeemer>;

    using witness_observer_t = std::function<void(const tx_wit &)>;
    using byron_vkey_wit_observer_t = std::function<void(const byron_vkey_witness_t &)>;
    using shelley_vkey_observer_t = std::function<void(const tx_wit_shelley_vkey &)>;
    using shelley_bootstrap_observer_t = std::function<void(const tx_wit_shelley_bootstrap &)>;
    using script_observer_t = std::function<void(const script_info &)>;
    using set_observer_t = std::function<void(cbor::zero2::value &)>;
    using input_observer_t = std::function<void(const tx_input &)>;
    using output_observer_t = std::function<void(const tx_output &)>;
    using mint_observer_t = std::function<void(const script_hash &, const policy_mint_map &)>;
    using withdrawal_observer_t = std::function<void(const tx_withdrawal &)>;
    using param_update_observer_t = std::function<void(const param_update_proposal &)>;
    using cert_observer_t = std::function<void(const cert_t &cert)>;
    using signer_observer_t = std::function<void(const key_hash &vk)>;
    using redeemer_observer_t = std::function<void(const tx_redeemer &)>;
    using datum_observer_t = std::function<void(const tx_wit_datum &)>;
    using withdrawal_map = flat_map<reward_id_t, uint64_t>;

    struct block_container;

    struct block_info {
        block_hash hash {};
        uint64_t offset = 0;
        uint32_t size = 0;
        uint32_t slot = 0;
        uint32_t height = 0;
        uint32_t chk_sum = 0;
        pool_hash pool_id {};
        uint16_t header_size = 0;
        uint8_t header_offset = 0;
        uint8_t era = 0; // necessary to exclude boundary blocks (era=0) during density estimation, etc.

        static block_info from_block(const block_container &blk);

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.hash, self.offset, self.size, self.slot, self.height, self.chk_sum,
                self.header_offset, self.header_size, self.pool_id, self.era);
        }

        [[nodiscard]] uint64_t end_offset() const
        {
            return offset + size;
        }

        [[nodiscard]] cardano::point2 point2() const
        {
            return { slot, hash };
        }

        [[nodiscard]] cardano::point3 point3() const
        {
            return { slot, hash, height };
        }

        [[nodiscard]] cardano::point point() const
        {
            return { hash, slot, height, end_offset() };
        }

        bool operator==(const block_info &o) const
        {
            return slot == o.slot && hash == o.hash && offset == o.offset;
        }
    };

    struct block_container {
        // prohibit copying and moving
        // since the nested value refers to the parent by a const reference
        block_container() =delete;
        block_container(const block_container &) =delete;

        block_container(const uint64_t offset, cbor::zero2::value &v, const config &cfg=cardano::config::get()):
            block_container { offset, v.array(), v, cfg }
        {
        }

        //block_container(block_container &&o);
        block_container(uint64_t offset, const block_info &meta, const config &cfg=cardano::config::get());
        ~block_container();
        const block_base &base() const;

        const block_base &operator*() const
        {
            return base();
        }

        const block_base *operator->() const
        {
            return &base();
        }

        const buffer &raw() const
        {
            if (!_raw) [[unlikely]]
                throw error("block_container::raw() not implemented for mock blocks!");
            return *_raw;
        }

        uint64_t child_offset(const buffer v) const
        {
            if (!_raw) [[unlikely]]
                throw error("block_container::child_offset() not implemented for mock blocks!");
            if (v.data() < _raw->data() || v.data() + v.size() > _raw->data() + _raw->size()) [[unlikely]]
                throw error("internal error: only CBOR values from within the same block are supported!");
            return offset() + (v.data() - _raw->data());
        }

        uint64_t offset() const
        {
            return base().offset();
        }

        uint8_t era() const
        {
            return _era;
        }

        uint64_t end_offset() const
        {
            if (!_raw) [[unlikely]]
                throw error("block_container::end_offset() not implemented for mock blocks!");
            return offset() + _raw->size();
        }

        uint32_t size() const
        {
            return numeric_cast<uint32_t>(raw().size());
        }
    private:
        using storage_type = byte_array<1120>;

        const uint8_t _era;
        storage_type _val alignas(64);
        std::optional<buffer> _raw;

        static storage_type _make(uint8_t era, uint64_t offset, cbor::zero2::value &block_tuple, cbor::zero2::value &block, const config &cfg);

        block_container(const uint64_t offset, cbor::zero2::array_reader &it, cbor::zero2::value &block_tuple, const config &cfg=cardano::config::get()):
            _era { numeric_cast<uint8_t>(it.read().uint()) },
            _val { _make(_era, offset, block_tuple, it.read(), cfg) },
            _raw { block_tuple.data_raw() }
        {
        }
    };

    struct tx_base {
        static double slot_relative_stake(const tail_relative_stake_map &tail_relative_stake, const uint64_t slot)
        {
            if (tail_relative_stake.empty())
                return 0.0;
            if (const auto it = tail_relative_stake.lower_bound(point { .slot=slot }); it != tail_relative_stake.end())
                return it->second;
            return 1.0;
        }

        tx_base(const block_base &blk, const uint64_t blk_offset, const size_t idx, const bool invalid):
            _blk { blk },
            _blk_offset { numeric_cast<uint32_t>(blk_offset) },
            _idx { tx_idx_cast(idx) },
            _invalid { invalid }
        {
        }

        virtual ~tx_base() =default;

        [[nodiscard]] const block_base &block() const
        {
            return _blk;
        }

        [[nodiscard]] size_t index() const
        {
            return _idx;
        }

        [[nodiscard]] bool invalid() const
        {
            return _invalid;
        }

        void foreach_cert(const cert_observer_t &) const;
        virtual void foreach_input(const input_observer_t &) const; // needs to be virtual since byron inputs are unordered and need special handling
        void foreach_output(const output_observer_t &) const;
        void foreach_witness(const witness_observer_t &) const;
        void foreach_witness_byron_vkey(const byron_vkey_wit_observer_t &) const;
        void foreach_witness_shelley_vkey(const shelley_vkey_observer_t &) const;
        void foreach_witness_shelley_bootstrap(const shelley_bootstrap_observer_t &) const;
        void foreach_script(const script_observer_t &, const plutus::context *ctx=nullptr) const;
        void foreach_datum(const datum_observer_t &observer) const;
        void foreach_redeemer(const redeemer_observer_t &observer) const;

        wit_cnt witnesses_ok(const plutus::context *ctx=nullptr) const;
        wit_cnt witnesses_ok_vkey(set<key_hash> &) const;
        wit_cnt witnesses_ok_native(const set<key_hash> &vkeys) const;
        wit_cnt witnesses_ok_plutus(const plutus::context &) const;

        virtual const cert_list &certs() const =0;
        virtual const tx_hash &hash() const =0;
        virtual const input_set &inputs() const =0;
        virtual const tx_output_list &outputs() const =0;
        virtual void parse_witnesses(cbor::zero2::value &) =0;
        virtual uint64_t fee() const =0;
        virtual buffer raw() const =0;

        virtual void foreach_set(cbor::zero2::value &set_raw, const set_observer_t &observer) const;
        virtual void foreach_referenced_input(const input_observer_t &) const {}
        virtual size_t foreach_mint(const mint_observer_t &) const { return 0; }
        virtual void foreach_withdrawal(const withdrawal_observer_t &) const {}
        virtual void foreach_param_update(const param_update_observer_t &) const {}
        virtual void foreach_collateral(const input_observer_t &) const {}
        virtual void foreach_collateral_return(const output_observer_t &) const {}
        virtual void foreach_required_signer(const signer_observer_t &) const {}

        virtual std::optional<uint64_t> validity_end() const
        {
            return {};
        }

        virtual std::optional<uint64_t> validity_start() const
        {
            return {};
        }

        virtual uint64_t donation() const
        {
            return 0;
        }

        uint64_t offset() const
        {
            return block().offset() + block().header_offset() + _blk_offset;
        }

        uint32_t size() const
        {
            return numeric_cast<uint32_t>(raw().size());
        }

        buffer witness_raw() const
        {
            if (_wits_raw) [[likely]]
                return *_wits_raw;
            throw error(fmt::format("transaction witnesses have not been parsed for TX #{}", hash()));
        }

        const tx_wit_list &witnesses() const
        {
            if (_wits_raw) [[likely]]
                return _wits;
            throw error(fmt::format("transaction witnesses have not been parsed for TX #{}", hash()));
        }

        json::object to_json(const tail_relative_stake_map &) const;
    protected:
        friend block_base;

        const block_base &_blk;
        const uint32_t _blk_offset;
        const uint16_t _idx: 15;
        uint16_t _invalid: 1;

        // delayed initialization in parse_witnesses; _wits_raw and _wits are empty until it's done
        tx_wit_list _wits {};
        std::optional<buffer> _wits_raw {};

        static uint16_t tx_idx_cast(const size_t idx)
        {
            if (idx < (1 << 15)) [[likely]]
                return idx;
            throw error(fmt::format("transaction idx is too large: {}!", idx));
        }

        void mark_invalid()
        {
            _invalid = 1;
        }

        template <typename T>
        void parse_witnesses_type(cbor::zero2::value &v)
        {
            set_t<T>::foreach_item(
                v,
                [&](auto &iv) {
                    _wits.emplace_back(T::from_cbor(iv));
                },
                [&](const auto sz) {
                    _wits.reserve(_wits.size() + sz);
                }
            );
        }

        void parse_witnesses_script(const script_type typ, cbor::zero2::value &v)
        {
            set_t<script_info>::foreach_item(
                v,
                [&](auto &iv) {
                    _wits.emplace_back(script_info::from_cbor(typ, iv));
                },
                [&](const auto sz) {
                    _wits.reserve(_wits.size() + sz);
                }
            );
        }
    };

    inline void block_base::mark_invalid_tx(const size_t idx)
    {
        txs().at(idx)->mark_invalid();
    }

    struct tx_container {
        using impl_storage = byte_array<768>;

        tx_container(const block_info &meta, uint64_t tx_abs_off, cbor::zero2::value &tx, size_t idx, const config &cfg);
        tx_container(const block_info &meta, uint64_t tx_abs_off, cbor::zero2::value &tx, cbor::zero2::value &wits, size_t idx, const config &cfg);
        ~tx_container();

        const tx_base &operator*() const;
        const tx_base *operator->() const;
    private:
        struct impl;

        impl_storage _impl;
    };
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::cardano::amount_asset>: formatter<daedalus_turbo::cardano::amount_pure> {
        template<typename FormatContext>
        auto format(const auto &a, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{} {}/{}", daedalus_turbo::cardano::amount_pure { a.coins }, a.policy_id, a.asset);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::balance_change>: formatter<daedalus_turbo::cardano::amount_pure> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            int64_t change = v;
            char sign = '+';
            if (change < 0) {
                sign = '-';
                change = -change;
            } else if (change == 0) {
                sign = ' ';
            }
            int64_t full = change / 1'000'000;
            int64_t rem = change % 1'000'000;
            return fmt::format_to(ctx.out(), "{}{}.{:06}", sign, full, rem);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::multi_balance_change>: formatter<daedalus_turbo::cardano::balance_change> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = ctx.out();
            for (const auto &[asset_name, change]: v) {
                out_it = fmt::format_to(out_it, "{} {}; ", daedalus_turbo::cardano::balance_change { change }, asset_name);
            }
            return out_it;
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::tx_base>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            const auto &slot = v.block().slot_object();
            auto out_it = fmt::format_to(ctx.out(), "tx hash: {} offset: {} size: {}\ntimestamp: {} UTC epoch: {} slot: {}\ninputs: [\n",
                v.hash(), v.offset(), v.size(), slot.timestamp(), slot.epoch(), slot);
            v.foreach_input([&](const auto &i) {
                out_it = fmt::format_to(out_it, "    {}\n", i);
            });     
            out_it = fmt::format_to(out_it, "]\noutputs: [\n");
            uint64_t sum_outputs = 0;
            v.foreach_output([&](const auto &o) {
                sum_outputs += o.coin;
                fmt::format_to(out_it, "    {}\n", o);
            });
            out_it = fmt::format_to(out_it, "]\n");
            return fmt::format_to(out_it, "total output: {}", daedalus_turbo::cardano::amount { sum_outputs });
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::nonce>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            if (v)
                return fmt::format_to(ctx.out(), "{}", *v);
            else
                return fmt::format_to(ctx.out(), "disabled");
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::wit_cnt>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "vkey: {} native: {} plutus_v1: {} plutus_v2: {} plutus_v3: {}",
                v.vkey, v.native_script, v.plutus_v1_script, v.plutus_v2_script, v.plutus_v3_script);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::redeemer_tag>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using daedalus_turbo::cardano::redeemer_tag;
            switch (v) {
                case redeemer_tag::spend: return fmt::format_to(ctx.out(), "spend");
                case redeemer_tag::mint: return fmt::format_to(ctx.out(), "mint");
                case redeemer_tag::cert: return fmt::format_to(ctx.out(), "cert");
                case redeemer_tag::reward: return fmt::format_to(ctx.out(), "reward");
                case redeemer_tag::vote: return fmt::format_to(ctx.out(), "vote");
                case redeemer_tag::propose: return fmt::format_to(ctx.out(), "propose");
                default: throw daedalus_turbo::error(fmt::format("unsupported redeemer tag value {}", static_cast<int>(v)));
            }
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::redeemer_id>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "purpose: {} ref_idx: {}", v.tag, v.ref_idx);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::multi_balance>: formatter<daedalus_turbo::cardano::multi_balance::base_type> {
    };
}

namespace daedalus_turbo::cardano {
    inline json::value amount_pure::to_json() const
    {
        return json::string { fmt::format("{}", *this) };
    }

    inline json::value amount_asset::to_json() const
    {
        return json::object {
            { "policyId", fmt::format("{}", policy_id) },
            { "name", asset },
            { "amount", coins },
        };
    }

    inline json::value balance_change::to_json() const
    {
        return json::string { fmt::format("{}", *this) };
    }

    inline json::object tx_out_data::to_json() const
    {
        auto j = json::object {
            { "address", addr().to_json() },
            { "amount", amount { coin }.to_json() }
        };
        if (!assets.empty()) {
            auto maj = json::object {};
            for (const auto &[policy_id, p_assets]: assets) {
                for (const auto &[asset, coin]: p_assets) {
                    maj.emplace(asset.to_string(policy_id), fmt::format("{}", amount_pure { coin }));
                }
            }
            j.emplace("assets", std::move(maj));
        }
        return j;
    }
}

#endif // !DAEDALUS_TURBO_CARDANO_COMMON_HPP
