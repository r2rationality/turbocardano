/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/common/common.hpp>
#include <dt/cardano/common/native-script.hpp>
#include <dt/cardano/byron/block.hpp>
#include <dt/crypto/crc32.hpp>
#include <dt/plutus/context.hpp>

namespace daedalus_turbo::cardano {
    invalid_tx_set::invalid_tx_set(cbor::zero2::value &v):
        base_type { base_type::from_cbor(v) }, raw { v.data_raw() }
    {
    }

    block_info block_info::from_block(const cardano::block_container &blk)
    {
        return {
            blk->hash(), blk.offset(), blk.size(),
            numeric_cast<uint32_t>(blk->slot()),
            numeric_cast<uint32_t>(blk->height ()),
            crypto::crc32::digest(blk.raw()),
            blk->issuer_hash(),
            numeric_cast<uint16_t>(blk->header().size()),
            numeric_cast<uint8_t>(blk->header_offset()),
            numeric_cast<uint8_t>(blk->era())
        };
    }

    redeemer_tag redeemer_tag_from_cbor(cbor::zero2::value &v)
    {
        switch (const auto typ = v.uint(); typ) {
            case 0: return redeemer_tag::spend;
            case 1: return redeemer_tag::mint;
            case 2: return redeemer_tag::cert;
            case 3: return redeemer_tag::reward;
            case 4: return redeemer_tag::vote;
            case 5: return redeemer_tag::propose;
            default: throw error(fmt::format("unsupported redeemer tag: {}", typ));
        }
    }

    ipv4_addr ipv4_addr::from_cbor(cbor::zero2::value &v)
    {
        return { v.bytes() };
    }

    ipv6_addr ipv6_addr::from_cbor(cbor::zero2::value &v)
    {
        return { v.bytes() };
    }

    pool_params pool_params::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        it.skip(1);
        return from_cbor(it);
    }

    pool_params pool_params::from_cbor(cbor::zero2::array_reader &it)
    {
        // assumes the pool hash has already been consumed!
        return pool_params {
            it.read().bytes(),
            it.read().uint(),
            it.read().uint(),
            decltype(margin)::from_cbor(it.read()),
            it.read().bytes(),
            decltype(owners)::from_cbor(it.read()),
            decltype(relays)::from_cbor(it.read()),
            decltype(metadata)::from_cbor(it.read())
        };
    }

    void pool_params::to_cbor(era_encoder &enc, const pool_hash &pool_id) const
    {
        enc.array(9);
        enc.bytes(pool_id);
        enc.bytes(vrf_vkey);
        enc.uint(pledge);
        enc.uint(cost);
        margin.to_cbor(enc);
        enc.bytes(reward_id);
        owners.to_cbor(enc);
        relays.to_cbor(enc);
        metadata.to_cbor(enc);
    }

    tx_wit_byron_vkey tx_wit_byron_vkey::from_cbor(cbor::zero2::value &v)
    {
        auto pv = cbor::zero2::parse(v.tag().read().bytes());
        auto &it = pv.get().array();
        return { it.read().bytes(), it.read().bytes() };
    }

    tx_wit_byron_redeemer tx_wit_byron_redeemer::from_cbor(cbor::zero2::value &v)
    {
        auto pv = cbor::zero2::parse(v.tag().read().bytes());
        auto &it = pv.get().array();
        return { it.read().bytes(), it.read().bytes() };
    }

    tx_wit_datum tx_wit_datum::from_cbor(cbor::zero2::value &v)
    {
        return { blake2b<datum_hash>(v.data_raw()), v.data_raw() };
    }

    tx_wit_shelley_vkey tx_wit_shelley_vkey::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().bytes(), it.read().bytes() };
    }

    tx_wit_shelley_bootstrap tx_wit_shelley_bootstrap::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { it.read().bytes(), it.read().bytes(), it.read().bytes(), it.read().bytes() };
    }

    tx_redeemer tx_redeemer::from_cbor(cbor::zero2::map_reader &m_it)
    {
        auto &key = m_it.read_key();
        auto &k_it = key.array();
        const auto tag = redeemer_tag_from_cbor(k_it.read());
        const auto ref_idx = k_it.read().uint();
        auto &val = m_it.read_val(std::move(key));
        auto &v_it = val.array();
        return { tag, numeric_cast<uint16_t>(ref_idx), v_it.read().data_raw(), ex_units::from_cbor(v_it.read()) };
    }

    tx_redeemer tx_redeemer::from_cbor(cbor::zero2::array_reader &a_it)
    {
        auto &v = a_it.read();
        auto &it = v.array();
        return {
            redeemer_tag_from_cbor(it.read()),
            numeric_cast<uint16_t>(it.read().uint()),
            it.read().data_raw(),
            ex_units::from_cbor(it.read())
        };
    }

    void tx_base::foreach_witness_byron_vkey(const byron_vkey_wit_observer_t &observer) const
    {
        for (const auto &w: _wits) {
            std::visit([&](const auto &bwit) {
                using T = std::decay_t<decltype(bwit)>;
                if constexpr (std::is_same_v<T, tx_wit_byron_vkey> || std::is_same_v<T, tx_wit_byron_redeemer>) {
                    observer(bwit);
                }
            }, w);
        }
    }

    void tx_base::foreach_witness_shelley_bootstrap(const shelley_bootstrap_observer_t &observer) const
    {
        for (const auto &w: _wits) {
            if (std::holds_alternative<tx_wit_shelley_bootstrap>(w))
                observer(std::get<tx_wit_shelley_bootstrap>(w));
        }
    }

    void tx_base::foreach_set(cbor::zero2::value &set_raw, const set_observer_t &observer) const
    {
        auto &it = set_raw.array();
        while (!it.done()) {
            auto &v = it.read();
            observer(v);
        }
    }

    void tx_base::foreach_cert(const cert_observer_t &observer) const
    {
        for (const auto &c: certs())
            observer(c);
    }

    void tx_base::foreach_input(const input_observer_t &observer) const
    {
        for (const auto &txi: inputs())
            observer(txi);
    }

    void tx_base::foreach_output(const output_observer_t &observer) const
    {
        for (const auto &txo: outputs())
            observer(txo);
    }

    void tx_base::foreach_witness(const witness_observer_t &observer) const
    {
        for (const auto &wit: witnesses())
            observer(wit);
    }

    void tx_base::foreach_witness_shelley_vkey(const shelley_vkey_observer_t &observer) const
    {
        foreach_witness([&](const auto &wit) {
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, tx_wit_shelley_vkey>) {
                    observer(v);
                }
            }, wit);
        });
    }

    void tx_base::foreach_script(const script_observer_t &observer, const plutus::context *ctx) const
    {
        foreach_witness([&](const auto &wit) {
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, script_info>) {
                    observer(v);
                }
            }, wit);
        });
        if (ctx) {
            for (const auto &txo: ctx->inputs()) {
                if (txo.data.script_ref)
                    observer(*txo.data.script_ref);
            }
            for (const auto &txo: ctx->ref_inputs()) {
                if (txo.data.script_ref)
                    observer(*txo.data.script_ref);
            }
        }
    }

    void tx_base::foreach_datum(const datum_observer_t &observer) const
    {
        foreach_witness([&](const auto &wit) {
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, tx_wit_datum>) {
                    observer(v);
                }
            }, wit);
        });
    }

    void tx_base::foreach_redeemer(const redeemer_observer_t &observer) const
    {
        foreach_witness([&](const auto &wit) {
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, tx_redeemer>) {
                    observer(v);
                }
            }, wit);
        });
    }

    wit_cnt tx_base::witnesses_ok_vkey(set<key_hash> &valid_vkeys) const
    {
        const auto &tx_hash = hash();
        wit_cnt cnts {};
        foreach_witness([&](const auto &w) {
            std::visit([&](const auto &wv) {
                using T = std::decay_t<decltype(wv)>;
                if constexpr (std::is_same_v<T, tx_wit_byron_vkey>) {
                    const auto pm = block().header().protocol_magic_raw();
                    uint8_vector msg {};
                    msg.reserve(64);
                    msg << 0x01; // signing tag
                    msg << pm;   // protocol magic
                    msg << 0x58; // CBOR bytestring
                    msg << 0x20; // hash size
                    msg << tx_hash;
                    const auto vk_short = static_cast<buffer>(wv.vkey).subbuf(0, 32);
                    if (!ed25519::verify(wv.sig, vk_short, msg)) [[unlikely]]
                        throw error(fmt::format("byron tx witness type 0 failed for tx {}", tx_hash));
                    valid_vkeys.emplace(blake2b<key_hash>(vk_short));
                    ++cnts.vkey;
                } else if constexpr (std::is_same_v<T, tx_wit_byron_redeemer>) {
                    const auto pm = block().header().protocol_magic_raw();
                    uint8_vector msg {};
                    msg.reserve(64);
                    msg << 0x02; // signing tag
                    msg << pm;   // protocol magic
                    msg << 0x58; // CBOR bytestring
                    msg << 0x20; // hash size
                    msg << tx_hash;
                    if (!ed25519::verify(wv.sig, wv.vkey, msg)) [[unlikely]]
                        throw error(fmt::format("byron tx witness type 2 failed for tx {}", tx_hash));
                    valid_vkeys.emplace(blake2b<key_hash>(wv.vkey));
                    ++cnts.vkey;
                } else if constexpr (std::is_same_v<T, tx_wit_shelley_vkey>) {
                    if (!ed25519::verify(wv.sig, wv.vkey, hash())) [[unlikely]]
                        throw error(fmt::format("shelley vkey witness failed at slot {}: vkey: {}, sig: {} tx_hash: {}", block().slot(), wv.vkey, wv.sig, hash()));
                    valid_vkeys.emplace(blake2b<key_hash>(wv.vkey));
                    ++cnts.vkey;
                } else if constexpr (std::is_same_v<T, tx_wit_shelley_bootstrap>) {
                    if (!ed25519::verify(wv.sig, wv.vkey, hash())) [[unlikely]]
                        throw error(fmt::format("shelley bootstrap witness failed at slot {}: vkey: {}, sig: {} tx_hash: {}", block().slot(), wv.vkey, wv.sig, hash()));
                    valid_vkeys.emplace(blake2b<key_hash>(wv.vkey));
                    ++cnts.vkey;
                }
            }, w);
        });
        return cnts;
    }

    wit_cnt tx_base::witnesses_ok_native(const set<key_hash> &vkeys) const
    {
        wit_cnt cnts {};
        foreach_script([&](const auto &si) {
            if (si.type() == script_type::native) {
                auto w_data = cbor::zero2::parse(si.script());
                if (const auto err = native_script::validate(w_data.get(), block().slot(), vkeys); err) [[unlikely]]
                    throw cardano_error(fmt::format("native script for tx {} failed: {} script: {}", hash(), *err, w_data.get().to_string()));
                ++cnts.native_script;
            }
        });
        return cnts;
    }

    wit_cnt tx_base::witnesses_ok_plutus(const plutus::context &ctx) const
    {
        wit_cnt cnt {};
        for (const auto &[rid, rinfo]: ctx.redeemers()) {
            auto ps = ctx.prepare_script(rinfo);
            ctx.eval_script(ps);
            cnt += ps.typ;
        }
        return cnt;
    }

    wit_cnt tx_base::witnesses_ok(const plutus::context *ctx) const
    {
        wit_cnt cnt {};
        set<key_hash> valid_vkeys {};
        cnt += witnesses_ok_vkey(valid_vkeys);
        cnt += witnesses_ok_native(valid_vkeys);
        if (ctx)
            cnt += witnesses_ok_plutus(*ctx);
        return cnt;
    }

    json::object tx_base::to_json(const tail_relative_stake_map &tail_relative_stake) const
    {
        json::array inputs {};
        foreach_input([&](const auto &tx_in) {
            inputs.emplace_back(tx_in.to_json());
        });
        json::array outputs {};
        foreach_output([&](const auto &tx_out) {
            outputs.emplace_back(tx_out.to_json());
        });
        return json::object {
                { "hash", fmt::format("{}", hash()) },
                { "offset", offset() },
                { "size", size() },
                { "slot", block().slot_object().to_json() },
                { "fee", fmt::format("{}", fee()) },
                { "inputs", std::move(inputs) },
                { "outputs", std::move(outputs) },
                { "relativeStake", slot_relative_stake(tail_relative_stake, block().slot()) }
        };
    }

    json::object multi_balance::to_json(const size_t offset, const size_t max_items) const
    {
        const auto end_offset = std::min(offset + max_items, size());
        json::object j {};
        size_t i = 0;
        for (const auto &[asset_name, amount]: *this) {
            if (i >= offset)
                j.emplace(asset_name, amount);
            if (++i >= end_offset)
                break;
        }
        return j;
    }

    size_t block_base::tx_count() const
    {
        return txs().size();
    }

    void block_base::foreach_tx(const tx_observer_t &observer) const
    {
        for (const auto &t: txs()) {
            if (!t->invalid()) [[likely]]
                observer(*t);
        }
    }

    void block_base::foreach_invalid_tx(const tx_observer_t &observer) const
    {
        for (const auto &tx_idx: invalid_txs()) {
                observer(*txs().at(tx_idx));
        }
    }

    bool kes_signature::verify() const
    {
        byte_array<sizeof(cardano_vkey) + 2 * 8> ocert_data {};
        if (vkey_hot.size() != sizeof(cardano_vkey))
            throw error("vkey size mismatch!");
        memcpy(ocert_data.data(), vkey_hot.data(), sizeof(cardano_vkey));
        const uint64_t ctr = host_to_net<uint64_t>(counter);
        memcpy(ocert_data.data() + sizeof(cardano_vkey), &ctr, 8);
        const uint64_t kp = host_to_net<uint64_t>(period);
        memcpy(ocert_data.data() + sizeof(cardano_vkey) + 8, &kp, 8);
        if (!ed25519::verify(vkey_sig, vkey_cold, ocert_data)) [[unlikely]] {
            logger::debug("an operational certificate has failed verification for issuer: {}", vkey_cold);
            return false;
        }
        const uint64_t block_period = slot / 129600;
        if (period > block_period)
            throw error(fmt::format("KES period {} is greater than the current period {}", period, block_period));
        const uint64_t t = block_period - period;
        const cardano_kes_signature kes_sig { sig };
        if (!kes_sig.verify(t, vkey_hot.first<32>(), header_body)) [[unlikely]] {
            logger::debug("a KES signature has failed verification for issuer: {}", vkey_cold);
            return false;
        }
        return true;
    }

    struct tx_container::impl {
        impl(const block_info &meta, const uint64_t tx_abs_off, cbor::zero2::value &v, size_t idx, const cardano::config &cfg):
            _blk { meta.offset, meta, cfg },
            _val { _make(_blk, tx_abs_off, v, idx) }
        {
        }

        impl(const block_info &meta, const uint64_t tx_abs_off, cbor::zero2::value &tx, cbor::zero2::value &wit, const size_t idx, const cardano::config &cfg):
            impl { meta, tx_abs_off, tx, idx, cfg }
        {
            std::visit([&](auto &tx_v) {
                tx_v.parse_witnesses(wit);
            }, _val);
        }

        const tx_base &base() const
        {
            return std::visit([&](auto &tx_v) -> const tx_base & {
                return tx_v;
            }, _val);
        }
    private:
        using value_type = std::variant<byron::tx, shelley::tx, mary::tx, alonzo::tx, babbage::tx, conway::tx>;

        mocks::block _blk;
        value_type _val;

        static value_type _make(const block_base &blk, const uint64_t tx_abs_off, cbor::zero2::value &tx, const size_t idx)
        {
            const auto blk_off = tx_abs_off - blk.offset() - blk.header_offset();
            switch (blk.era()) {
                case 1: return byron::tx { blk, blk_off, tx, idx };
                case 2: return shelley::tx { blk, blk_off, tx, idx };
                case 3:
                case 4: return mary::tx { blk, blk_off, tx, idx};
                case 5: return alonzo::tx { blk, blk_off, tx, idx };
                case 6: return babbage::tx { blk, blk_off, tx, idx };
                case 7: return conway::tx { blk, blk_off, tx, idx };
                default: throw cardano_error(fmt::format("unsupported era {}!", blk.era()));
            }
        }
    };

    tx_container::tx_container(const block_info &meta, const uint64_t tx_abs_off, cbor::zero2::value &tx, const size_t idx, const config &cfg)
    {
        static_assert(sizeof(impl_storage) >= sizeof(impl));
        new (reinterpret_cast<impl*>(_impl.data())) impl { meta, tx_abs_off, tx, idx, cfg };
    }

    tx_container::tx_container(const block_info &meta, const uint64_t tx_abs_off, cbor::zero2::value &tx, cbor::zero2::value &wits, const size_t idx, const config &cfg)
    {
        static_assert(sizeof(impl_storage) >= sizeof(impl));
        new (reinterpret_cast<impl*>(_impl.data())) impl { meta, tx_abs_off, tx, wits, idx, cfg };
    }

    tx_container::~tx_container()
    {
        reinterpret_cast<impl*>(_impl.data())->~impl();
    }

    const tx_base &tx_container::operator*() const
    {
        return reinterpret_cast<const impl*>(_impl.data())->base();
    }

    const tx_base *tx_container::operator->() const
    {
        return &reinterpret_cast<const impl*>(_impl.data())->base();
    }
}
