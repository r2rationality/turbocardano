#pragma once
#ifndef DAEDALUS_TURBO_CARDANO_TYPES_HPP
#define DAEDALUS_TURBO_CARDANO_TYPES_HPP
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <chrono>
#include <functional>
#include <dt/array.hpp>
#include <dt/bech32.hpp>
#include <dt/blake2b.hpp>
#include <dt/container.hpp>
#include <dt/ed25519.hpp>
#include <dt/json.hpp>
#include <dt/kes.hpp>
#include <dt/common/numeric-cast.hpp>
#include <dt/partitioned-map.hpp>
#include <dt/rational.hpp>
#include <dt/static-map.hpp>
#include <dt/util.hpp>
#include <dt/vrf.hpp>
#include <dt/zpp.hpp>
#include <dt/cardano/common/types/base.hpp>

namespace daedalus_turbo {
    using cardano_error = error;
    using cardano_hash_32 = blake2b_256_hash;
    using cardano_hash_28 = blake2b_224_hash;
    using cardano_vkey = ed25519::vkey;
    using cardano_vkey_span = std::span<const uint8_t, sizeof(cardano_vkey)>;
    using cardano_signature = ed25519::signature;
    using cardano_kes_signature = kes_signature<6>;
    using cardano_kes_signature_data= byte_array<cardano_kes_signature::size()>;
    using cardano_vrf_vkey = vrf_vkey;
    using cardano_vrf_result = vrf_result;
    using cardano_vrf_result_span = std::span<const uint8_t, sizeof(cardano_vrf_result)>;
    using cardano_vrf_proof = vrf_proof;
    using cardano_vrf_proof_span = std::span<const uint8_t, sizeof(cardano_vrf_proof)>;
}

namespace daedalus_turbo::cardano {
    static constexpr size_t max_data_struct_size = size_t { 8 } << 30;

    auto decode_versioned(cbor::zero2::value &v, auto proc)
    {
        auto &it = v.array();
        if (const auto ver = it.read().uint(); ver != 1) [[unlikely]]
            throw error(fmt::format("only version 1 serialization is supported but got: {}", ver));
        return proc(it.read());
    }

    using vkey = cardano_vkey;
    using vrf_nonce = vrf_nonce;
    using vrf_vkey = vrf_vkey;
    using vrf_result = vrf_result;
    using vrf_proof = vrf_proof;

    template<typename T>
    struct nil_optional_t: std::optional<T> {
        using base_type = std::optional<T>;
        using base_type::base_type;

        static nil_optional_t from_cbor(cbor::zero2::value &v)
        {
            if (v.is_null())
                return {};
            return value_from_cbor<T>(v);
        }

        void to_cbor(era_encoder &enc) const
        {
            if (base_type::has_value()) {
                value_to_cbor(enc, base_type::operator*());
            } else {
                enc.s_null();
            }
        }

        bool operator==(const nil_optional_t<T> &o) const noexcept
        {
            if (base_type::has_value() != o.has_value())
                return false;
            if (base_type::has_value())
                return base_type::value() == *o;
            return true;
        }
    };

    template<typename T>
    struct prefix_optional_t: std::optional<T> {
        using base_type = std::optional<T>;
        using base_type::base_type;

        static prefix_optional_t from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            if (it.read().uint() == 0)
                return {};
            return value_from_cbor<T>(it.read());
        }

        prefix_optional_t(prefix_optional_t &&o) noexcept: base_type { std::move(o) }
        {
        }

        prefix_optional_t(const prefix_optional_t &o): base_type { o }
        {
        }

        void to_cbor(era_encoder &enc) const
        {
            if (base_type::has_value()) {
                enc.array(2);
                enc.uint(1);
                value_to_cbor(enc, base_type::operator*());
            } else {
                enc.array(1);
                enc.uint(0);
            }
        }

        prefix_optional_t<T> &operator=(prefix_optional_t<T> &&o) noexcept
        {
            std::optional<T>::operator=(std::move(o));
            return *this;
        }

        prefix_optional_t<T> &operator=(cbor::zero2::value &v)
        {
            return *this = from_cbor(v);
        }

        prefix_optional_t<T> &operator=(const prefix_optional_t<T> &o)
        {
            std::optional<T>::operator=(o);
            return *this;
        }

        bool operator==(const prefix_optional_t<T> &o) const noexcept
        {
            if (base_type::has_value() != o.has_value())
                return false;
            if (base_type::has_value())
                return base_type::value() == *o;
            return true;
        }
    };

    template<typename T>
    struct array_optional_t: std::optional<T> {
        using base_type = std::optional<T>;
        using base_type::base_type;

        static array_optional_t from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            if (it.done())
                return {};
            return value_from_cbor<T>(it.read());
        }

        array_optional_t(array_optional_t &&o) noexcept: base_type { std::move(o) }
        {
        }

        array_optional_t(const array_optional_t &o): base_type { o }
        {
        }

        void to_cbor(era_encoder &enc) const
        {
            if (base_type::has_value()) {
                enc.array(1);
                value_to_cbor(enc, base_type::operator*());
            } else {
                enc.array(0);
            }
        }

        array_optional_t<T> &operator=(array_optional_t<T> &&o) noexcept
        {
            std::optional<T>::operator=(std::move(o));
            return *this;
        }

        array_optional_t<T> &operator=(const array_optional_t<T> &o)
        {
            std::optional<T>::operator=(o);
            return *this;
        }

        bool operator==(const array_optional_t<T> &o) const noexcept
        {
            if (base_type::has_value() != o.has_value())
                return false;
            if (base_type::has_value())
                return base_type::value() == *o;
            return true;
        }
    };

    template<typename T>
    struct optional_t: std::optional<T> {
        using base_type = std::optional<T>;
        using base_type::base_type;

        void emplace_cbor(cbor::zero2::value &v)
        {
            base_type::emplace(value_from_cbor<T>(v));
        }

        bool operator==(const optional_t<T> &o) const noexcept
        {
            return this->base_type::operator==(o);
        }
    };

    template<typename M>
    M map_from_cbor(cbor::zero2::value &v)
    {
        M res {};
        auto &it = v.map();
        while (!it.done()) {
            auto &key = it.read_key();
            auto k = value_from_cbor<typename M::key_type>(key);
            auto &val = it.read_val(std::move(key));
            res.try_emplace(std::move(k), value_from_cbor<typename M::mapped_type>(val));
        }
        return res;
    }

    template<typename M, typename ENC>
    void map_to_cbor(ENC &enc, const M &m)
    {
        enc.map_compact(m.size(), [&] {
            for (const auto &[k, v]: m) {
                value_to_cbor(enc, k);
                value_to_cbor(enc, v);
            }
        });
    }

    template<typename K, typename V, typename ENC=era_encoder>
    struct map_t: flat_map<K, V> {
        using base_type = flat_map<K, V>;
        using base_type::base_type;

        static map_t from_cbor(cbor::zero2::value &v)
        {
            map_t res {};
            if (!v.indefinite()) {
                static constexpr size_t max_size = max_data_struct_size / (sizeof(K) + sizeof(V));
                static_assert(max_size > 0);
                const auto sz = v.special_uint();
                if (sz > max_size) [[unlikely]]
                    throw error(fmt::format("the requested map size is unrealistically big: {}!", sz));
                res.reserve(sz);
            }
            auto &it = v.map();
            while (!it.done()) {
                auto &key = it.read_key();
                auto k = value_from_cbor<K>(key);
                auto &val = it.read_val(std::move(key));
                res.emplace_hint(res.end(), std::move(k), value_from_cbor<V>(val));
            }
            return res;
        }

        void to_cbor(ENC &enc) const
        {
            map_to_cbor(enc, *this);
        }
    };

    template<typename T, typename ENC=era_encoder>
    struct vector_t: vector<T> {
        using base_type = vector<T>;
        using base_type::base_type;

        static vector_t from_cbor(cbor::zero2::value &v)
        {
            vector_t res {};
            if (!v.indefinite()) {
                static constexpr size_t max_size = max_data_struct_size / sizeof(T);
                static_assert(max_size > 0);
                const auto sz = v.special_uint();
                if (sz > max_size) [[unlikely]]
                    throw error(fmt::format("the requested vector size is unrealistically big: {}!", sz));
                res.reserve(sz);
            }
            auto &it = v.array();
            while (!it.done()) {
                res.emplace_back(value_from_cbor<T>(it.read()));
            }
            return res;
        }

        void to_cbor(ENC &enc) const
        {
            enc.array_compact(base_type::size(), [&] {
                for (const auto &v: *this)
                    value_to_cbor(enc, v);
            });
        }
    };

    struct shelley_delegate {
        pool_hash delegate {};
        vrf_vkey vrf {};

        static shelley_delegate from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            return { it.read().bytes(), it.read().bytes() };
        }

        bool operator==(const shelley_delegate &o) const
        {
            return delegate == o.delegate && vrf == o.vrf;
        }
    };
    using shelley_delegate_map = map<key_hash, shelley_delegate>;

    struct config;

    struct slot_range {
        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self._min, self._max);
        }

        explicit slot_range(const uint64_t slot): _min { slot}, _max { slot }
        {
        }

        explicit slot_range(const uint64_t min, const uint64_t max): _min { min }, _max { max }
        {
        }

        explicit slot_range(const slot_range &o): _min { o._min }, _max { o._max }
        {
        }

        bool operator<(const slot_range &o) const
        {
            return _min < o._min;
        }

        void update(const uint64_t slot)
        {
            if (slot < _min)
                throw error(fmt::format("block supplied not in order slot {} observed after slot {}", slot, _min));
            if (slot < _max)
                throw error(fmt::format("block supplied not in order slot {} observed after slot {}", slot, _max));
            _max = slot;
        }

        uint64_t min() const
        {
            return _min;
        }

        uint64_t max() const
        {
            return _max;
        }
    private:
        uint64_t _min;
        uint64_t _max;
    };

    struct slot {
        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self._slot);
        }

        static slot from_time(const std::chrono::time_point<std::chrono::system_clock> &tp, const cardano::config &cfg);
        static slot from_epoch(const uint64_t epoch, const uint64_t epoch_slot, const cardano::config &cfg);
        static slot from_chunk(const uint64_t chunk, const cardano::config &cfg);
        static slot from_epoch(const uint64_t epoch, const cardano::config &cfg);
        static slot from_future(const cardano::config &cfg);

        slot(const cardano::config &cfg): slot { 0, cfg }
        {
        }

        slot(const uint64_t slot_, const cardano::config &cfg): _slot { slot_ }, _cfg { cfg }
        {
        }

        slot(const slot &o): _slot { o._slot }, _cfg { o._cfg }
        {
        }

        void to_cbor(era_encoder &enc) const;

        slot &operator=(const slot &o)
        {
            _slot = o._slot;
            return *this;
        }

        slot &operator+=(const uint64_t num_slots)
        {
            _slot += num_slots;
            return *this;
        }

        operator uint64_t() const
        {
            return _slot;
        }

        json::value to_json() const
        {
            return json::object {
                { "slot", _slot },
                { "epoch", epoch() },
                { "timestamp", timestamp() }
            };
        }

        const cardano::config &config() const
        {
            return _cfg;
        }

        uint64_t epoch() const;
        uint64_t epoch_slot() const;
        uint64_t chunk_id() const;
        uint64_t unixtime() const;
        std::string timestamp() const;
        std::string utc_month() const;
    private:
        uint64_t _slot = 0;
        const cardano::config &_cfg;
    };

    struct point2 {
        uint64_t slot = 0;
        block_hash hash {};

        static point2 from_cbor(cbor::zero2::value &v);
        void to_cbor(cbor::encoder &enc) const;

        bool operator<(const point2 &o) const
        {
            return slot < o.slot;
        }

        bool operator==(const point2 &o) const
        {
            return hash == o.hash && slot == o.slot;
        }
    };

    struct point3: point2 {
        uint64_t height = 0;

        static point3 from_cbor(cbor::zero2::value &v);
        void to_cbor(cbor::encoder &enc) const;
    };

    struct point {
        block_hash hash {};
        uint64_t slot = 0;
        uint64_t height = 0;
        uint64_t end_offset = 0;

        static point from_point3(const point3 &o)
        {
            return { o.hash, o.slot, o.height, 0 };
        }

        static point from_ledger_cbor(cbor::zero2::value &v);
        static point from_cbor(cbor::zero2::value &v);

        operator point2() const
        {
            return { slot, hash };
        }

        operator point3() const
        {
            return { slot, hash, height };
        }

        bool operator<(const point &o) const
        {
            return slot < o.slot;
        }

        bool operator==(const point &o) const
        {
            return hash == o.hash && slot == o.slot;
        }
    };

    struct intersection_info_t {
        std::optional<point2> isect;
        point3 tip;
    };

    using point2_list = vector_t<point2, cbor::encoder>;
    using optional_point = std::optional<point>;

    struct optional_slot: std::optional<uint64_t> {
        using std::optional<uint64_t>::optional;

        optional_slot(const optional_point &p)
            : optional_slot { p ? optional_slot { p->slot } : optional_slot {} }
        {
        }

        optional_slot(const point &p)
            : optional_slot { p.slot }
        {
        }

        bool operator<(const optional_slot &o) const
        {
            if (has_value() && o.has_value())
                return value() < o.value();
            if (has_value() != o.has_value())
                return has_value() < o.has_value();
            return false;
        }
    };

    inline bool operator<(const optional_point &a, const optional_point &b)
    {
        if (a.has_value() && b.has_value())
            return a.value() < b.value();
        if (a.has_value() != b.has_value())
            return a.has_value() < b.has_value();
        return false;
    }

    inline bool operator<(const optional_point &a, const optional_slot &b)
    {
        if (a.has_value() && b.has_value())
            return a.value().slot < b.value();
        if (a.has_value() != b.has_value())
            return a.has_value() < b.has_value();
        return false;
    }

    struct cert_loc_t {
        uint64_t slot = 0;
        uint32_t tx_idx = 0;
        uint32_t cert_idx = 0;

        static uint64_t max_slot()
        {
            return std::numeric_limits<decltype(slot)>::max();
        }

        static uint64_t max_tx_idx()
        {
            return std::numeric_limits<decltype(tx_idx)>::max();
        }

        static uint64_t max_cert_idx()
        {
            return std::numeric_limits<decltype(cert_idx)>::max();
        }

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.slot, self.tx_idx, self.cert_idx);
        }

        cert_loc_t() =default;
        cert_loc_t(const cert_loc_t &) =default;
        cert_loc_t(uint64_t, uint64_t, uint64_t);
        cert_loc_t &operator=(const cert_loc_t &) =default;

        bool operator<(const cert_loc_t &b) const
        {
            if (slot != b.slot)
                return slot < b.slot;
            if (tx_idx != b.tx_idx)
                return tx_idx < b.tx_idx;
            return cert_idx < b.cert_idx;
        }

        bool operator==(const cert_loc_t &b) const
        {
            return slot == b.slot && tx_idx == b.tx_idx && cert_idx == b.cert_idx;
        }
    };

    struct stake_pointer: cert_loc_t {
        using cert_loc_t::cert_loc_t;

        static stake_pointer from_cbor(cbor::zero2::value &v);
        void to_cbor(era_encoder &) const;

        json::object to_json() const
        {
            return json::object {
                { "slot", slot },
                { "txIdx", tx_idx },
                { "certIdx", cert_idx }
            };
        }
    };

    struct credential_t {
        key_hash hash {};
        bool script { false };

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.hash, self.script);
        }

        static credential_t from_cbor(cbor::zero2::value &);
        static credential_t from_json(std::string_view);
        void to_cbor(era_encoder &) const;

        bool operator<(const auto &b) const
        {
            // compares the hash and the script in a single operation
            static_assert(sizeof(*this) == 29);
            return memcmp(this, &b, sizeof(*this)) < 0;
        }

        bool operator==(const credential_t &b) const
        {
            return memcmp(this, &b, sizeof(*this)) == 0 && script == b.script;
        }

        bool operator!=(const credential_t &b) const
        {
            return !(*this == b);
        }

        json::value to_json() const
        {
            return json::object {
                    { "hash", fmt::format("{}", hash) },
                    { "script", script }
            };
        }
    };
    using stake_ident = credential_t;

    struct pay_ident {
        enum class ident_type: uint8_t {
          SHELLEY_KEY, SHELLEY_SCRIPT, BYRON_KEY
        };

        key_hash hash {};
        ident_type type {};

        bool operator<(const auto &b) const {
            static_assert(sizeof(*this) == 29);
            return memcmp(this, &b, sizeof(*this)) < 0;
        }

        bool operator==(const auto &b) const {
            return memcmp(this, &b, sizeof(*this)) == 0;
        }

        bool operator!=(const auto &b) const {
            return !(*this == b);
        }

        const char *type_name() const
        {
            switch (type) {
                case ident_type::SHELLEY_KEY:
                    return "shelley-key";
                case ident_type::SHELLEY_SCRIPT:
                    return "shelley-script";
                case ident_type::BYRON_KEY:
                    return "byron-key";
                default:
                    std::unreachable();
            }
        }

        json::value to_json() const
        {
            return json::object {
                { "hash", fmt::format("{}", hash) },
                { "type", type_name() }
            };
        }
    };

    using stake_ident_hybrid = std::variant<stake_ident, stake_pointer>;


    extern uint8_vector byron_crc_protected(const buffer &encoded_addr);
    extern key_hash byron_addr_root_hash(size_t typ, buffer vk, buffer attrs_cbor);
    extern std::tuple<uint8_t, size_t> from_haskell_char(std::string_view sv);
    extern uint8_vector from_haskell(std::string_view sv);
    extern uint8_vector byron_avvm_addr(std::string_view redeem_vk);
    extern tx_hash byron_avvm_tx_hash(std::string_view redeem_vk_base64u);

    enum class script_type: uint8_t {
        native = 0,
        plutus_v1 = 1,
        plutus_v2 = 2,
        plutus_v3 = 3
    };

    inline script_type script_type_from_str(const std::string_view s)
    {
        if (s == "v1")
            return script_type::plutus_v1;
        if (s == "v2")
            return script_type::plutus_v2;
        if (s == "v3")
            return script_type::plutus_v3;
        if (s == "native")
            return script_type::native;
        throw error(fmt::format("unsupported script type: {}", s));
    }

    inline script_type script_type_from_cbor(cbor::zero2::value &v)
    {
        switch (const auto s_typ = v.uint(); s_typ) {
            case 0: return script_type::native;
            case 1: return script_type::plutus_v1;
            case 2: return script_type::plutus_v2;
            case 3: return script_type::plutus_v3;
            default: throw error(fmt::format("unsupported script_type: {}", s_typ));
        }
    }

    struct script_info {
        static script_info from_cbor(const script_type typ, cbor::zero2::value &);
        static script_info from_cbor(cbor::zero2::value &);
        static script_info from_cbor(buffer bytes);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self._data, self._hash);
        }

        script_info(const script_type type, const buffer script): _data { _canonical(type, script) }
        {
        }

        script_info(script_info &&o): _data { std::move(o._data) }
        {
        }

        script_info(const script_info &o): script_info { o.type(), o.script() }
        {
        }

        script_info &operator=(script_info &&o)
        {
            _data = std::move(o._data);
            _hash = o._hash;
            return *this;
        }

        script_info &operator=(const script_info &o)
        {
            _hash = o._hash;
            _data = o._data;
            return *this;
        }

        void to_cbor(era_encoder &) const;

        bool operator==(const script_info &o) const
        {
            return _data == o._data;
        }

        [[nodiscard]] const script_hash &hash() const
        {
            if (!_hash) {
                _hash.emplace(blake2b<script_hash>(_data));
            }
            return *_hash;
        }

        [[nodiscard]] script_type type() const
        {
            return static_cast<script_type>(_data.at(0));
        }

        [[nodiscard]] buffer script() const
        {
            return static_cast<buffer>(_data).subbuf(1);
        }

        buffer span() const
        {
            return _data;
        }
    private:
        friend ::zpp::bits::access;

        static uint8_vector _canonical(const script_type type, const buffer script)
        {
            uint8_vector bytes {};
            bytes.reserve(script.size() + 1);
            bytes << static_cast<uint8_t>(type) << script;
            return bytes;
        }

        script_info(): _data { uint8_vector::from_hex("00") }
        {
        }

        uint8_vector _data;
        mutable std::optional<script_hash> _hash {};
    };
    using script_info_map = map<script_hash, script_info>;

    struct byron_addr {
        static byron_addr from_bytes(buffer);
        bool vkey_ok(buffer vk, uint8_t typ) const;
        key_hash bootstrap_root(cbor::zero2::value &) const;
        bool bootstrap_ok(cbor::zero2::value &) const;

        const key_hash &root() const
        {
            return _root;
        }

        uint8_t type() const
        {
            return _type;
        }

        buffer attrs() const
        {
            return _attrs;
        }

        buffer bytes() const
        {
            return _bytes;
        }
    private:
        key_hash _root;
        uint8_vector _attrs;
        uint8_t _type;
        uint8_vector _bytes;

        byron_addr(cbor::zero2::array_reader &, cbor::zero2::value &);
    };

    struct address {
        address(const address &o):
            _storage { o._storage ? std::make_unique<uint8_vector>(*o._storage) : nullptr },
            _bytes { _storage ? static_cast<buffer>(*_storage) : o._bytes }
        {
        }

        address(address &&o):
            _storage { std::move(o._storage) },
            _bytes { _storage ? static_cast<buffer>(*_storage) : o._bytes }
        {
        }

        address(const buffer bytes): _bytes { bytes }
        {
            if (_bytes.size() < 2)
                throw cardano_error("cardano address must have at least two bytes!");
            switch (type()) {
                case 0b1110: // reward key
                case 0b1111: // reward script
                case 0b0110: // enterprise key
                case 0b0111: // enterprise script
                    // there are cases when mainnet addresses contain extra data which is ignored by Cardano Node
                    if (_bytes.size() > 29) [[unlikely]]
                        _bytes = _bytes.subbuf(0, 29);
                    if (_bytes.size() < 29) [[unlikely]]
                        throw cardano_error(fmt::format("cardano reward addresses must have at least 29 bytes: {}!", _bytes));
                    break;

                case 0b0000: // base address: keyhash28,keyhash28
                case 0b0001: // base address: scripthash28,keyhash28
                case 0b0010: // base address: keyhash28,scripthash28
                case 0b0011: // base address: scripthash28,scripthash28
                    if (_bytes.size() > 57) [[unlikely]]
                        _bytes = _bytes.subbuf(0, 57);
                    if (_bytes.size() < 57) [[unlikely]]
                        throw cardano_error(fmt::format("shelley base address must have at least 57 bytes: {}!", _bytes));
                    break;

                case 0b1000: // byron
                    // do nothing
                    break;

                case 0b0100: // keyhash28, pointer
                case 0b0101: // scripthash28, pointer
                {
                    auto ptr_buf = _bytes.subbuf(29);
                    uint64_t slot = 0;
                    const auto sz1 = _read_var_uint_be(slot, ptr_buf);
                    uint64_t tx_idx = 0;
                    const auto sz2 = _read_var_uint_be(tx_idx, ptr_buf.subbuf(sz1));
                    uint64_t cert_idx = 0;
                    const auto sz3 = _read_var_uint_be(cert_idx, ptr_buf.subbuf(sz1 + sz2));
                    if (slot > cert_loc_t::max_slot() || tx_idx > cert_loc_t::max_tx_idx() || cert_idx > cert_loc_t::max_cert_idx()) {
                        slot = 0;
                        tx_idx = 0;
                        cert_idx = 0;
                    }
                    stake_pointer ptr { slot, tx_idx, cert_idx };
                    ptr_buf = ptr_buf.subbuf(0, sz1 + sz2 + sz3);
                    uint8_vector ptr_enc {};
                    ptr_enc << _encode_var_uint_be(ptr.slot) << _encode_var_uint_be(ptr.tx_idx) << _encode_var_uint_be(ptr.cert_idx);
                    if (ptr_enc == ptr_buf) [[likely]] {
                        _bytes = _bytes.subbuf(0, 29 + ptr_buf.size());
                    } else {
                        _storage = std::make_unique<uint8_vector>();
                        _storage->reserve(29 + ptr_enc.size());
                        *_storage = _bytes.subbuf(0, 29);
                        *_storage << ptr_enc;
                        _bytes = *_storage;
                    }
                    break;
                }

                default:
                    throw cardano_error(fmt::format("unsupported address type: {}!", type()));
            }
        }

        void to_cbor(era_encoder &enc) const;

        uint8_t network() const
        {
            if (!is_byron()) [[likely]]
                return _bytes[0] & 0xF;
            throw error("network id is supported only for shelley+ addresses at the moment!");
        }

        uint8_t type() const
        {
            return (_bytes[0] >> 4) & 0xF;
        }

        buffer data() const
        {
            if (!is_byron()) [[likely]]
                return _bytes.subbuf(1, _bytes.size() - 1);
            return _bytes;
        }

        buffer bytes() const
        {
            return _bytes;
        }

        const pay_ident pay_id() const;

        const stake_pointer pointer() const
        {

            if (data().size() < 28 + 3)
                throw error(fmt::format("pointer data is too small - expect 31+ bytes but got: {}", data().size()));
            const auto ptr = data().subspan(28, data().size() - 28);
            uint64_t slot, tx_idx, cert_idx;
            const auto sz1 = _read_var_uint_be(slot, ptr);
            const auto sz2 = _read_var_uint_be(tx_idx, ptr.subspan(sz1, ptr.size() - sz1));
            _read_var_uint_be(cert_idx, ptr.subspan(sz1 + sz2, ptr.size() - sz1 - sz2));
            return { slot, tx_idx, cert_idx };
        };

        const stake_ident stake_id() const
        {
            switch (type()) {
                case 0b1110: // reward key
                case 0b1111: // reward script
                    return stake_ident { data().subbuf(0, 28), (type() & 0x1) > 0 };

                case 0b0000: // base address: keyhash28,keyhash28
                case 0b0001: // base address: scripthash28,keyhash28
                case 0b0010: // base address: keyhash28,scripthash28
                case 0b0011: // base address: scripthash28,scripthash28
                    return stake_ident { data().subbuf(28, 28), (type() & 0x2) > 0 };

                default:
                    throw cardano_error(fmt::format("address::stake_id unsupported for address type: {}!", type()));
            }
        }

        std::optional<stake_ident> stake_id_opt() const
        {
            switch (type()) {
                case 0b1110: // reward key
                case 0b1111: // reward script
                    return stake_ident { data().subbuf(0, 28), (type() & 0x1) > 0 };

                case 0b0000: // base address: keyhash28,keyhash28
                case 0b0001: // base address: scripthash28,keyhash28
                case 0b0010: // base address: keyhash28,scripthash28
                case 0b0011: // base address: scripthash28,scripthash28
                    return stake_ident { data().subbuf(28, 28), (type() & 0x2) > 0 };

                default:
                    return {};
            }
        }

        const stake_ident_hybrid stake_id_hybrid() const
        {
            if (has_stake_id())
                return stake_id();
            if (has_pointer())
                return pointer();
            throw error(fmt::format("address {} has neither a stake_id not a pointer reference!", _bytes));
        }

        bool has_stake_id() const
        {
            switch (type()) {
                case 0b1110: // reward key
                case 0b1111: // reward script
                case 0b0000: // base address: keyhash28,keyhash28
                case 0b0001: // base address: scripthash28,keyhash28
                case 0b0010: // base address: keyhash28,scripthash28
                case 0b0011: // base address: scripthash28,scripthash28
                    return true;

                default:
                    return false;
            }
        }

        bool has_pay_id() const
        {
            switch (type()) {
                case 0b0110: // enterprise key
                case 0b0111: // enterprise script
                case 0b0000: // base address: keyhash28,keyhash28
                case 0b0001: // base address: scripthash28,keyhash28
                case 0b0010: // base address: keyhash28,scripthash28
                case 0b0011: // base address: scripthash28,scripthash28
                case 0b0100: // pointer key
                case 0b0101: // pointer script
                    return true;

                default:
                    return false;
            }
        }

        bool has_pointer() const
        {
            switch (type()) {
                case 0b0100: // pointer key
                case 0b0101: // pointer script
                    return true;
                    break;

                default:
                    return false;
            }
        }

        bool has_stake_id_hybrid() const
        {
            return has_stake_id() || has_pointer();
        }

        bool is_byron() const
        {
            return type() == 0b1000;
        }

        byron_addr byron() const;

        json::value to_json() const
        {
            const char *type_str = nullptr;
            switch (type()) {
                case 0b1110: // reward key
                    type_str = "shelley-reward-key";
                    break;

                case 0b1111: // reward script
                    type_str = "shelley-reward-script";
                        break;

                case 0b0110: // enterprise key
                    type_str = "shelley-enterprise-key";
                    break;

                case 0b0111: // enterprise script
                    type_str = "shelley-enterprise-script";
                    break;

                case 0b0000: // base address: keyhash28,keyhash28
                case 0b0001: // base address: scripthash28,keyhash28
                case 0b0010: // base address: keyhash28,scripthash28
                case 0b0011: // base address: scripthash28,scripthash28
                    type_str = "shelley-base";
                    break;

                case 0b1000: // byron
                    type_str = "byron";
                    break;

                case 0b0100: // pointer key
                    type_str = "shelley-pointer-key";
                    break;

                case 0b0101: // pointer script
                    type_str = "shelley-pointer-script";
                    break;
            }
            if (!type_str)
                throw cardano_error(fmt::format("unsupported address type: {}!", type()));
            json::object res {
                { "type", type_str },
                { "data", fmt::format("{}", _bytes) }
            };
            if (has_stake_id())
                res.emplace("stakeId", stake_id().to_json());
            if (has_pay_id())
                res.emplace("payId", pay_id().to_json());
            if (has_pointer())
                res.emplace("stakePointer", pointer().to_json());
            return res;
        }

        bool operator==(const address &o) const
        {
            return _bytes == o._bytes;
        }
    private:
        std::unique_ptr<uint8_vector> _storage {};
        buffer _bytes;

        static size_t _read_var_uint_be(uint64_t &x, const buffer &buf)
        {
            if (buf.size() == 0)
                throw cardano_error("can't read a variable integer from an empty buffer!");
            x = 0;
            uint64_t val = static_cast<uint64_t>(buf[0]);
            size_t num_read = 1;
            for (;;) {
                x |= val & 0x7F;
                if (val & 0x80) {
                    if (buf.size() < num_read + 1)
                        throw error(fmt::format("the buffer is too small: {}!", buf.size()));
                    val = static_cast<uint64_t>(buf[num_read]);
                    ++num_read;
                } else {
                    break;
                }
                x <<= 7;
            }
            return num_read;
        }

        static uint8_vector _encode_var_uint_be(uint64_t x)
        {
            uint8_vector out {};
            uint8_t next_byte = 0;
            for (;;) {
                next_byte |= x & 0x7F;
                out.emplace_back(next_byte);
                x >>= 7;
                if (!x)
                    break;
                next_byte = 0x80;
            }
            std::ranges::reverse(out);
            return out;
        }
    };

    struct address_buf: uint8_vector {
        address_buf(const std::string_view addr_sv)
        {
            static const std::string_view prefix1 { "#" };
            static const std::string_view prefix2 { "0x" };
            if (addr_sv.starts_with(prefix1)) {
                uint8_vector::operator=(from_hex(addr_sv.substr(prefix1.size())));
            } else if (addr_sv.starts_with(prefix2)) {
                uint8_vector::operator=(from_hex(addr_sv.substr(prefix2.size())));
            } else {
                const bech32 addr_bech32(addr_sv);
                resize(addr_bech32.size());
                memcpy(data(), addr_bech32.data(), addr_bech32.size());
            }
        }
    };

    struct asset_name_t {
        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self._data, self._size);
        }

        asset_name_t() =default;
        asset_name_t(buffer bytes);
        std::string to_string(const script_hash &policy_id) const;
        void to_cbor(era_encoder &enc) const;

        bool operator==(const asset_name_t &o) const
        {
            return span() == o.span();
        }

        const uint8_t *data() const
        {
            return _data.data();
        }

        size_t size() const
        {
            return _size;
        }

        buffer span() const
        {
            return { _data.data(), _size };
        }

        bool operator<(const auto &o) const noexcept
        {
            const auto min_sz = std::min(size(), o.size());
            // std::memcmp is guaranteed to return 0 if min_sz is 0
            if (const int cmp = std::memcmp(data(), o.data(), min_sz); cmp != 0)
                return cmp < 0;
            return size() < o.size();
        }
    private:
        byte_array<32> _data {};
        uint8_t _size = 0;
    };

    using policy_asset_map = map_t<asset_name_t, uint64_t>;

    struct multi_asset_map: map_t<script_hash, policy_asset_map> {
        using base_type = map_t<script_hash, policy_asset_map>;
        using base_type::base_type;

        json::object to_json(size_t offset=0, size_t max_items=1000) const;
    };

    using policy_mint_map = flat_map<asset_name_t, int64_t>;
    struct multi_mint_map: flat_map<script_hash, policy_mint_map> {
        using flat_map<script_hash, policy_mint_map>::flat_map;
    };

    struct amount {
        uint64_t coins { 0 };

        static amount from_cbor(cbor::zero2::value &v)
        {
            return { v.uint() };
        }

        operator uint64_t() const
        {
            return coins;
        }

        inline json::value to_json() const;

        amount &operator=(const uint64_t coin)
        {
            coins = coin;
            return *this;
        }
    };

    struct amount_pure: amount {
        inline json::value to_json() const;
    };

    struct tx_out_idx {
        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self._out_idx);
        }

        tx_out_idx(): _out_idx { 0 } {}

        tx_out_idx(size_t out_idx)
        {
            if (out_idx >= (1U << 16)) [[unlikely]]
                throw error(fmt::format("tx out idx is too big: {}!", out_idx));
            _out_idx = out_idx;
        }

        tx_out_idx &operator=(size_t out_idx)
        {
            if (out_idx >= (1U << 16)) [[unlikely]]
                throw error(fmt::format("tx out idx is too big: {}!", out_idx));
            _out_idx = out_idx;
            return *this;
        }

        operator std::size_t() const
        {
            return _out_idx;
        }
    private:
        uint16_t _out_idx;
    };

    struct tx_out_ref {
        tx_hash hash {};
        tx_out_idx idx {};

        static tx_out_ref from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            return { it.read().bytes(), it.read().uint() };
        }

        std::strong_ordering operator<=>(const auto &o) const
        {
            const int cmp = memcmp(hash.data(), o.hash.data(), hash.size());
            if (cmp > 0)
                return std::strong_ordering::greater;
            if (cmp < 0)
                return std::strong_ordering::less;
            return idx <=> o.idx;
        }
        bool operator==(const tx_out_ref &) const =default;
        bool operator<(const tx_out_ref &) const =default;

        json::object to_json() const
        {
            return json::object {
                { "hash", fmt::format("{}", hash) },
                { "outIdx", static_cast<size_t>(idx) }
            };
        }
    };
    using tx_out_ref_list = vector<tx_out_ref>;
    using tx_input = tx_out_ref;
    using input_set = flat_set<tx_input>;

    struct output_value_t {
        uint64_t coin = 0;
        multi_asset_map assets {};

        static output_value_t from_cbor(cbor::zero2::value &v);
    };

    using datum_option_value_t = std::variant<datum_hash, uint8_vector>;

    struct datum_option_t {
        using value_type = datum_option_value_t;

        value_type val;

        static datum_option_t from_cbor(cbor::zero2::value &v);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.val);
        }

        bool operator==(const datum_option_t &o) const
        {
            return val == o.val;
        }
    };

    struct tx_out_data {
        uint8_vector address_raw;
        uint64_t coin = 0;
        multi_asset_map assets {};
        std::optional<datum_option_t> datum {};
        std::optional<script_info> script_ref {};

        static tx_out_data from_cbor(cbor::zero2::value &);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.coin, self.address_raw, self.assets, self.datum, self.script_ref);
        }

        void to_cbor(era_encoder &) const;

        cardano::address addr() const
        {
            return { static_cast<buffer>(address_raw) };
        }

        bool empty() const noexcept
        {
            return address_raw.empty();
        }

        operator bool() const noexcept
        {
            return !empty();
        }

        bool operator==(const auto &o) const
        {
            return coin == o.coin && address_raw == o.address_raw && assets == o.assets && datum == o.datum && script_ref == o.script_ref;
        }

        inline json::object to_json() const;
    private:
        static tx_out_data from_shelley_cbor(cbor::zero2::value &);
        static tx_out_data from_babbage_cbor(cbor::zero2::value &);
    };
    using tx_output = tx_out_data;
    using tx_output_list = vector_t<tx_output>;
    using tx_out_data_list = vector<tx_out_data>;
    using txo_map = partitioned_map<tx_out_ref, tx_out_data>;

    struct protocol_version {
        uint64_t major = 1;
        uint64_t minor = 0;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.major, self.minor);
        }

        static protocol_version from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;

        bool operator==(const auto &b) const
        {
            return major == b.major && minor == b.minor;
        }

        bool operator<(const auto &o) const
        {
            if (major != o.major)
                return major < o.major;
            return minor < o.minor;
        }

        bool aggregated_rewards() const
        {
            return major > 2;
        }

        bool forgo_reward_prefilter() const
        {
            return major > 6;
        }

        bool keep_pointers() const
        {
            return major < 9;
        }

        bool bootstrap_phase() const
        {
            return major == 9;
        }

        uint64_t era() const {
            switch (major) {
                case 0: return 0;
                case 1: return 1;
                case 2: return 2;
                case 3: return 3;
                case 4: return 4;
                case 5:
                case 6:
                    return 5;
                case 7:
                case 8:
                    return 6;
                case 9:
                case 10:
                    return 7;
                default: throw error(fmt::format("unsupported protocol major version: {}", major));
            }
        }
    };

    using nonce = prefix_optional_t<vrf_nonce>;

    struct plutus_cost_model: static_map<std::string, int64_t> {
        using static_map::static_map;

        using storage_type = static_map;
        using diff_type = map<std::string, std::pair<std::optional<int64_t>, std::optional<int64_t>>>;

        static plutus_cost_model from_cbor(const vector<std::string> &names, cbor::zero2::value &data);
        static plutus_cost_model from_json(const plutus_cost_model &orig, const json::value &data);

        diff_type diff(const plutus_cost_model &o) const;
        void to_cbor(era_encoder &) const;
        void update(const plutus_cost_model &src);
    };

    struct ex_units {
        uint64_t mem = 0;
        uint64_t steps = 0;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.mem, self.steps);
        }

        static ex_units from_cbor(cbor::zero2::value &);
        static ex_units from_json(const json::value &);
        void to_cbor(era_encoder &) const;

        bool operator==(const ex_units &o) const
        {
            return mem == o.mem && steps == o.steps;
        }

        bool operator>(const ex_units &o) const
        {
            return mem > o.mem || steps > o.steps;
        }
    };

    struct ex_unit_prices {
        rational_u64 mem {};
        rational_u64 steps {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.mem, self.steps);
        }

        static ex_unit_prices from_cbor(cbor::zero2::value &v);
        static ex_unit_prices from_json(const json::value &v);
        void to_cbor(era_encoder &) const;

        bool operator==(const ex_unit_prices &o) const
        {
            return mem == o.mem && steps == o.steps;
        }
    };

    struct plutus_cost_models {
        std::optional<plutus_cost_model> v1 {};
        std::optional<plutus_cost_model> v2 {};
        std::optional<plutus_cost_model> v3 {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.v1, self.v2, self.v3);
        }

        static plutus_cost_models from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;

        bool operator==(const plutus_cost_models &o) const noexcept
        {
            return v1 == o.v1 && v2 == o.v2 && v3 == o.v3;
        }
    };

    struct pool_voting_thresholds_t {
        rational_u64 motion_of_no_confidence { 51, 100 };
        rational_u64 committee_normal { 51, 100 };
        rational_u64 committee_no_confidence { 51, 100 };
        rational_u64 hard_fork_initiation { 51, 100 };
        rational_u64 security_voting_threshold { 51, 100 };

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.motion_of_no_confidence, self.committee_normal,
                self.committee_no_confidence, self.hard_fork_initiation, self.security_voting_threshold);
        }

        static pool_voting_thresholds_t from_cbor(cbor::zero2::value &);
        static pool_voting_thresholds_t from_json(const json::value &);
        void to_cbor(era_encoder &) const;
    };

    struct drep_voting_thresholds_t {
        rational_u64 motion_no_confidence { 67, 100 };
        rational_u64 committee_normal { 67, 100 };
        rational_u64 committee_no_confidence { 67, 100 };
        rational_u64 update_constitution { 75, 100 };
        rational_u64 hard_fork_initiation { 67, 100 };
        rational_u64 pp_network_group { 67, 100 };
        rational_u64 pp_economic_group { 67, 100 };
        rational_u64 pp_technical_group { 67, 100 };
        rational_u64 pp_governance_group { 75, 100 };
        rational_u64 treasury_withdrawal { 67, 100 };

        static const drep_voting_thresholds_t &zero()
        {
            static drep_voting_thresholds_t t {
                {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1},
                {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}
            };
            return t;
        }

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.motion_no_confidence, self.committee_normal, self.committee_no_confidence,
                self.update_constitution, self.hard_fork_initiation,
                self.pp_network_group, self.pp_economic_group, self.pp_technical_group,
                self.pp_governance_group, self.treasury_withdrawal
            );
        }

        static drep_voting_thresholds_t from_cbor(cbor::zero2::value &);
        static drep_voting_thresholds_t from_json(const json::value &);
        void to_cbor(era_encoder &) const;
    };

    struct param_update {
        block_hash hash {};
        optional_t<uint64_t> min_fee_a {};
        optional_t<uint64_t> min_fee_b {};
        optional_t<uint32_t> max_block_body_size {};
        optional_t<uint32_t> max_transaction_size {};
        optional_t<uint32_t> max_block_header_size {};
        optional_t<uint64_t> key_deposit {};
        optional_t<uint64_t> pool_deposit {};
        optional_t<uint32_t> e_max {};
        optional_t<uint64_t> n_opt {};
        optional_t<rational_u64> pool_pledge_influence {};
        optional_t<rational_u64> expansion_rate {};
        optional_t<rational_u64> treasury_growth_rate {};
        optional_t<rational_u64> decentralization {};
        optional_t<nonce> extra_entropy {};
        optional_t<protocol_version> protocol_ver {};
        optional_t<uint64_t> min_utxo_value {};
        optional_t<uint64_t> min_pool_cost {};
        optional_t<uint64_t> lovelace_per_utxo_byte {};
        optional_t<cardano::ex_unit_prices> ex_unit_prices {};
        optional_t<ex_units> max_tx_ex_units {};
        optional_t<ex_units> max_block_ex_units {};
        optional_t<uint64_t> max_value_size {};
        optional_t<uint64_t> max_collateral_pct {};
        optional_t<uint64_t> max_collateral_inputs {};
        optional_t<cardano::plutus_cost_models> plutus_cost_models {};

        static param_update from_cbor(cbor::zero2::value &);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(
                self.hash,
                self.min_fee_a, self.min_fee_b,
                self.max_block_body_size, self.max_transaction_size, self.max_block_header_size,
                self.key_deposit, self.pool_deposit, self.e_max,
                self.n_opt, self.pool_pledge_influence,
                self.expansion_rate, self.treasury_growth_rate, self.decentralization,
                self.extra_entropy, self.protocol_ver,
                self.min_utxo_value,
                self.min_pool_cost, self.lovelace_per_utxo_byte,
                self.ex_unit_prices, self.max_tx_ex_units, self.max_block_ex_units,
                self.max_value_size,
                self.max_collateral_pct, self.max_collateral_inputs,
                self.plutus_cost_models
            );
        }

        void hash_from_cbor(const buffer bytes)
        {
            blake2b(hash, bytes);
        }

        bool operator==(const auto &o) const
        {
            return hash == o.hash;
        }

        void rehash();
    };

    using signer_set = set<key_hash>;

    struct param_update_t;

    struct protocol_params {
        uint64_t min_fee_a = 0;
        uint64_t min_fee_b = 0;
        uint32_t max_block_body_size = 2'000'000;
        uint32_t max_transaction_size = 4096;
        uint32_t max_block_header_size = 65535;
        uint64_t key_deposit = 2'000'000;
        uint64_t pool_deposit = 500'000'000;
        uint32_t e_max = 0;
        uint64_t n_opt = 150;
        rational_u64 pool_pledge_influence { 3, 10 };
        rational_u64 expansion_rate { 3, 1000 };
        rational_u64 treasury_growth_rate { 1, 5 };
        rational_u64 decentralization { 1, 1 };
        rational_u64 decentralizationThreshold { 4, 5 };
        nonce extra_entropy {};
        protocol_version protocol_ver {};
        uint64_t min_utxo_value = 0;
        uint64_t min_pool_cost = 0;
        uint64_t lovelace_per_utxo_byte = 0;
	    cardano::ex_unit_prices ex_unit_prices {};
        ex_units max_tx_ex_units {};
        ex_units max_block_ex_units {};
        uint64_t max_value_size = 0;
        uint64_t max_collateral_pct = 0;
        uint64_t max_collateral_inputs = 0;
	    cardano::plutus_cost_models plutus_cost_models {};
        pool_voting_thresholds_t pool_voting_thresholds {};
        drep_voting_thresholds_t drep_voting_thresholds {};
        uint16_t committee_min_size = 7;
        uint32_t committee_max_term_length = 146;
        uint32_t gov_action_lifetime = 6;
        uint64_t gov_action_deposit = 100'000'000'000;
        uint64_t drep_deposit = 500'000'000;
        uint32_t drep_activity = 20;
        rational_u64 min_fee_ref_script_cost_per_byte { 15, 1 };

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(
                self.min_fee_a, self.min_fee_b,
                self.max_block_body_size, self.max_transaction_size, self.max_block_header_size,
                self.key_deposit, self.pool_deposit, self.e_max,
                self.n_opt, self.pool_pledge_influence,
                self.expansion_rate, self.treasury_growth_rate, self.decentralization,
                self.decentralizationThreshold, self.extra_entropy, self.protocol_ver,
                self.min_utxo_value,
                self.min_pool_cost, self.lovelace_per_utxo_byte,
                self.ex_unit_prices, self.max_tx_ex_units, self.max_block_ex_units,
                self.max_value_size,
                self.max_collateral_pct, self.max_collateral_inputs,
                self.plutus_cost_models
            );
        }

        bool operator==(const auto &o) const
        {
            return min_fee_a == o.min_fee_a && min_fee_b == o.min_fee_b
                && max_block_body_size == o.max_block_body_size && max_transaction_size == o.max_transaction_size && max_block_header_size == o.max_block_header_size
                && key_deposit == o.key_deposit && pool_deposit == o.pool_deposit && e_max == o.e_max
                && n_opt == o.n_opt && pool_pledge_influence == o.pool_pledge_influence
                && expansion_rate == o.expansion_rate && treasury_growth_rate == o.treasury_growth_rate && decentralization == o.decentralization
                && decentralizationThreshold == o.decentralizationThreshold && extra_entropy == o.extra_entropy && protocol_ver == o.protocol_ver
                && min_utxo_value == o.min_utxo_value
                && min_pool_cost == o.min_pool_cost
                && lovelace_per_utxo_byte == o.lovelace_per_utxo_byte
                && ex_unit_prices == o.ex_unit_prices
                && max_tx_ex_units == o.max_tx_ex_units
                && max_block_ex_units == o.max_block_ex_units
                && max_value_size == o.max_value_size
                && max_collateral_pct == o.max_collateral_pct
                && max_collateral_inputs == o.max_collateral_inputs
                && plutus_cost_models == o.plutus_cost_models;
        }

        void clear()
        {
            *this = {};
        }

        std::string apply(const param_update &);
        std::string apply(const param_update_t &);
    };

    struct param_update_proposal {
        key_hash key_id {};
        param_update update {};
        std::optional<uint64_t> epoch {};

        static param_update_proposal from_cbor(const buffer &pool_id, cbor::zero2::value &v);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.key_id, self.epoch, self.update);
        }
    };
    using param_update_proposal_list = vector_t<param_update_proposal>;

    struct param_update_vote {
        key_hash key_id {};
        block_hash proposal_id {};
        bool vote = false;
        ed25519::signature sig {};
    };

    struct drep_t {
        struct abstain_t {
            bool operator==(const abstain_t &) const
            {
                return true;
            }
        };
        struct no_confidence_t {
            bool operator==(const no_confidence_t &) const
            {
                return true;
            }
        };
        using value_type = std::variant<credential_t, abstain_t, no_confidence_t>;

        value_type val { abstain_t {} };

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.val);
        }

        static drep_t from_cbor(cbor::zero2::value &v);
        void to_cbor(era_encoder &) const;

        bool operator<(const drep_t &o) const noexcept
        {
            if (val.index() != o.val.index())
                return val.index() < o.val.index();
            if (std::holds_alternative<credential_t>(val)) {
                const auto &cred = std::get<credential_t>(val);
                const auto &o_cred = std::get<credential_t>(o.val);
                if (cred.script != o_cred.script)
                    return cred.script < o_cred.script;
                return cred.hash < o_cred.hash;
            }
            return false;
        }
    };

    struct vrf_cert {
        vrf_result result {};
        vrf_proof proof {};

        static vrf_cert from_cbor(cbor::zero2::value &v);
    };

    struct operational_cert {
        kes_vkey hot_key {};
        uint64_t seq_no = 0;
        uint64_t period = 0;
        ed25519::signature sig {};

        static operational_cert from_cbor(cbor::zero2::value &v);
    };

    struct ipv4_addr: byte_array<4> {
        using base_type = byte_array<4>;
        using byte_array<4>::byte_array;

        static ipv4_addr from_cbor(cbor::zero2::value &v);
        void to_cbor(era_encoder &) const;

        bool operator==(const ipv4_addr &o) const noexcept
        {
            return memcmp(data(), o.data(), size()) == 0;
        }
    };

    struct ipv6_addr: byte_array<16> {
        using byte_array<16>::byte_array;

        static ipv6_addr from_cbor(cbor::zero2::value &v);
        void to_cbor(era_encoder &) const;

        bool operator==(const ipv6_addr &o) const noexcept
        {
            return memcmp(data(), o.data(), size()) == 0;
        }
    };

    struct relay_addr {
        nil_optional_t<uint16_t> port {};
        nil_optional_t<ipv4_addr> ipv4 {};
        nil_optional_t<ipv6_addr> ipv6 {};

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.ipv6, self.ipv4, self.port);
        }

        static relay_addr from_cbor(cbor::zero2::array_reader &);
        void to_cbor(era_encoder &) const;

        bool operator==(const relay_addr &o) const noexcept
        {
            return ipv4 == o.ipv4 && ipv6 == o.ipv6 && port == o.port;
        }
    };

    struct relay_host {
        nil_optional_t<uint16_t> port {};
        std::string host {};

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.host, self.port);
        }

        static relay_host from_cbor(cbor::zero2::array_reader &);
        void to_cbor(era_encoder &) const;

        bool operator==(const relay_host &o) const
        {
            return host == o.host && port == o.port;
        }
    };

    struct relay_dns {
        std::string name {};

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.name);
        }

        static relay_dns from_cbor(cbor::zero2::array_reader &);
        void to_cbor(era_encoder &) const;

        bool operator==(const relay_dns &o) const
        {
            return name == o.name;
        }
    };

    struct relay_info {
        using value_type = std::variant<relay_addr, relay_host, relay_dns>;
        value_type val;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.val);
        }

        static relay_info from_cbor(cbor::zero2::value &v);
        void to_cbor(era_encoder &) const;
        bool operator==(const relay_info &o) const;
    };

    using relay_list = vector_t<relay_info>;

    struct pool_metadata {
        std::string url {};
        cardano_hash_32 hash {};

        static pool_metadata from_cbor(cbor::zero2::value &v);
        void to_cbor(era_encoder &) const;

        bool operator==(const pool_metadata &o) const noexcept
        {
            return url == o.url && hash == o.hash;
        }
    };

    struct stake_keyhash_t: byte_array<28> {
        using base_type = byte_array<28>;

        static stake_keyhash_t from_cbor(cbor::zero2::value &v);

        stake_keyhash_t() =default;

        stake_keyhash_t(const buffer bytes): byte_array { bytes }
        {
        }

        buffer hash() const
        {
            return *this;
        }

        bool script() const
        {
            return false;
        }

        operator stake_ident() const
        {
            return { hash(), script() };
        }

        bool operator==(const stake_keyhash_t &o) const noexcept
        {
            return memcmp(data(), o.data(), size()) == 0;
        }

        bool operator==(const credential_t &id) const noexcept
        {
            // use the bitwise and to elimninate an unnecessary branch
            return static_cast<int>(hash() == static_cast<buffer>(id.hash)) & static_cast<int>(id.script == script());
        }
    };

    struct reward_id_t: byte_array<29> {
        using base_type = byte_array<29>;
        static reward_id_t from_cbor(cbor::zero2::value &v);

        reward_id_t() =default;

        reward_id_t(const buffer bytes): base_type { bytes }
        {
            if (const auto typ = at(0) >> 4; typ != 0xE && typ != 0xF) [[unlikely]]
                throw error(fmt::format("unsupported reward id type: 0x{:X}", typ));
        }

        buffer hash() const
        {
            return std::span(*this).subspan(1, 28);
        }

        bool script() const
        {
            return (at(0) & 0xF0) == 0xF0;
        }

        operator stake_ident() const
        {
            return { hash(), script() };
        }

        uint8_t network_id() const
        {
            return at(0) & 0xF;
        }

        bool operator==(const reward_id_t &o) const noexcept
        {
            return memcmp(data(), o.data(), size()) == 0;
        }

        bool operator==(const credential_t &id) const noexcept
        {
            // use the bitwise and to eliminate an unnecessary branch
            return static_cast<int>(static_cast<buffer>(id.hash) == hash()) & static_cast<int>(id.script == script());
        }
    };

    struct pool_params {
        cardano::vrf_vkey vrf_vkey {};
        uint64_t pledge = 0;
        uint64_t cost = 0;
        rational_u64 margin {};
        reward_id_t reward_id {};
        set_t<stake_keyhash_t> owners {};
        relay_list relays {};
        nil_optional_t<pool_metadata> metadata {};

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.vrf_vkey, self.pledge, self.cost, self.margin, self.reward_id, self.owners, self.relays, self.metadata);
        }

        static pool_params from_cbor(cbor::zero2::value &v);
        static pool_params from_cbor(cbor::zero2::array_reader &);
        void to_cbor(era_encoder &, const pool_hash &) const;

        bool operator==(const pool_params &o) const
        {
            return reward_id == o.reward_id && owners == o.owners && pledge == o.pledge
                && cost == o.cost && margin == o.margin
                && vrf_vkey == o.vrf_vkey && relays == o.relays
                && metadata == o.metadata;
        }
    };

    struct epoch {
        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self._epoch);
        }

        static void check(uint64_t epoch)
        {
            if (epoch >= (1U << 16))
                throw error(fmt::format("epoch number is too big: {}!", epoch));
        }

        epoch() =default;

        epoch(uint64_t epoch): _epoch(static_cast<uint16_t>(epoch))
        {
            check(epoch);
        }

        epoch &operator=(uint64_t epoch)
        {
            check(epoch);
            _epoch = static_cast<uint16_t>(epoch);
            return *this;
        }

        operator uint64_t() const
        {
            return _epoch;
        }
    private:
        uint16_t _epoch = 0;
    };

    struct parsed_block;
    using parsed_block_ptr_t = std::unique_ptr<parsed_block>;
    using parsed_block_list = std::vector<parsed_block_ptr_t>;
}

namespace fmt {
    template<>
    struct formatter<daedalus_turbo::cardano::epoch>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<uint64_t>(v));
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::asset_name_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::cardano::asset_name_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", daedalus_turbo::buffer_readable { v.span() });
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::protocol_version>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}.{}", v.major, v.minor);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::slot>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}/{}/{}", v.epoch(), v.epoch_slot(), static_cast<uint64_t>(v));
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::tx_out_idx>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto a, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", (size_t)a);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::stake_ident>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &id, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "stake-{} #{}", id.script ? "script" : "key", id.hash);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::pay_ident>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &id, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "pay-{} #{}", id.type_name(), id.hash);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::amount_pure> {
        constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
            return ctx.begin();
        }

        template<typename FormatContext>
        auto format(const auto &a, FormatContext &ctx) const -> decltype(ctx.out()) {
            uint64_t full = a.coins / 1'000'000;
            uint64_t rem = a.coins % 1'000'000;
            return fmt::format_to(ctx.out(), "{}.{:06}", full, rem);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::amount>: formatter<daedalus_turbo::cardano::amount_pure> {
        template<typename FormatContext>
        auto format(const auto &a, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{} ADA", daedalus_turbo::cardano::amount_pure { a.coins });
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::point2>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "({}, {})", v.slot, v.hash);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::point3>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "({}, {}, {})", v.slot, v.hash, v.height);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::point>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "({}, {}, {}, {})", v.hash, v.slot, v.height, v.end_offset);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::tx_out_ref>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}#{}", v.hash, v.idx);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::datum_option_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            switch (const auto typ = v.val.index(); typ) {
                case 0: return fmt::format_to(ctx.out(), "{}", std::get<daedalus_turbo::cardano::datum_hash>(v.val));
                case 1: return fmt::format_to(ctx.out(), "{}", std::get<daedalus_turbo::uint8_vector>(v.val));
                default: throw daedalus_turbo::error(fmt::format("unsupported variant index: {}", typ));
            }
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::tx_out_data>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            if (!v.address_raw.empty()) [[likely]] {
                return fmt::format_to(ctx.out(), "address: #{} ({}) coin: {} assets: {} datum: {} script_ref: {}",
                    v.address_raw, daedalus_turbo::cardano::address { v.address_raw }, daedalus_turbo::cardano::amount { v.coin }, v.assets, v.datum, v.script_ref);
            }
            return fmt::format_to(ctx.out(), "tx_out_data::empty");
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::stake_pointer>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "stake-pointer(slot: {} tx_idx: {} cert_idx: {})", v.slot, v.tx_idx, v.cert_idx);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::stake_ident_hybrid>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            switch (v.index()) {
                case 0:
                    return fmt::format_to(ctx.out(), "{}", std::get<daedalus_turbo::cardano::stake_ident>(v));

                case 1:
                    return fmt::format_to(ctx.out(), "{}", std::get<daedalus_turbo::cardano::stake_pointer>(v));

                default:
                    throw daedalus_turbo::error(fmt::format("unsupported stake_ident_hybrid index: {}", v.index()));
            }
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::shelley_delegate>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "delegate: {} vrf: {}", v.delegate, v.vrf);
        }
    };

    enum class script_type: uint8_t {
        native = 0,
        plutus_v1 = 1,
        plutus_v2 = 2,
        plutus_v3 = 3
    };

    template<>
    struct formatter<daedalus_turbo::cardano::script_type>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::cardano::script_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using daedalus_turbo::cardano::script_type;
            switch (v) {
                case script_type::native: return fmt::format_to(ctx.out(), "native");
                case script_type::plutus_v1: return fmt::format_to(ctx.out(), "plutus_v1");
                case script_type::plutus_v2: return fmt::format_to(ctx.out(), "plutus_v2");
                case script_type::plutus_v3: return fmt::format_to(ctx.out(), "plutus_v3");
                default: throw daedalus_turbo::cardano_error(fmt::format("unsupported address type: {}!", static_cast<int>(v)));
            }
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::byron_addr>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "byron: (root: {} type: {} attrs: {})", v.root(), v.type(), v.attrs());
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::address>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::cardano::address &addr, FormatContext &ctx) const -> decltype(ctx.out()) {
            using daedalus_turbo::buffer;
            switch (addr.type()) {
                case 0b1000:
                    return fmt::format_to(ctx.out(), "byron/{}", addr.bytes());

                case 0b1110:
                case 0b1111:
                    return fmt::format_to(ctx.out(), "shelley-reward{}/{}-{}", addr.network() == 1 ? "" : fmt::format("{}", addr.network()),
                                (addr.type() & 1) ? "script" : "key", addr.data());

                case 0b0000: // base address: keyhash28,keyhash28
                case 0b0001: // base address: scripthash28,keyhash28
                case 0b0010: // base address: keyhash28,scripthash28
                case 0b0011: // base address: scripthash28,scripthash28
                    return fmt::format_to(ctx.out(), "shelley-base{}/pay_{}:{}-stake_{}:{}", addr.network() == 1 ? "" : fmt::format("{}", addr.network()),
                        (addr.type() & 1) > 0 ? "script" : "key", buffer { addr.data().data(), 28 },
                        (addr.type() & 2) > 0 ? "script" : "key", buffer { addr.data().data() + 28, 28 });

                case 0b0110:
                case 0b0111:
                    return fmt::format_to(ctx.out(), "shelley-enterprise{}/{}-{}", addr.network() == 1 ? "" : fmt::format("{}", addr.network()),
                        (addr.type() & 1) ? "script" : "key", addr.data());

                case 0b0100:
                case 0b0101:
                    return fmt::format_to(ctx.out(), "shelley-pointer{}/pay_{}:{}-stake_ptr:{}", addr.network() == 1 ? "" : fmt::format("{}", addr.network()),
                                (addr.type() & 1) ? "script" : "key", addr.data().subspan(0, 28), addr.pointer());

                default:
                    throw daedalus_turbo::cardano_error(fmt::format("unsupported address type: {}!", addr.type()));
            }
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::protocol_params>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "min_fee_a: {} "
                "min_fee_b: {} max_block_body_size: {} max_transaction_size: {} max_block_header_size: {} key_deposit: {} "
                "pool_deposit: {} e_max: {} n_opt: {} pool_pledge_influence: {} expansion_rate: {} "
                "treasury_growth_rate: {} decentralization: {} decentralizationThreshold: {} extra_entropy: {} protocol_ver: {} "
                "min_utxo_value: {} "
                "min_pool_cost: {} lovelace_per_utxo_byte: {} "
                "ex_unit_prices: {} max_tx_ex_units: {} max_block_ex_units: {} "
                "max_value_size: {} "
                "max_collateral_pct: {} max_collateral_inputs: {} "
                "plutus_cost_models: {}",
                v.min_fee_a,
                v.min_fee_b, v.max_block_body_size, v.max_transaction_size, v.max_block_header_size, v.key_deposit,
                v.pool_deposit, v.e_max, v.n_opt, v.pool_pledge_influence, v.expansion_rate,
                v.treasury_growth_rate, v.decentralization, v.decentralizationThreshold, v.extra_entropy, v.protocol_ver,
                v.min_utxo_value,
                v.min_pool_cost, v.lovelace_per_utxo_byte,
                v.ex_unit_prices, v.max_tx_ex_units, v.max_block_ex_units,
                v.max_value_size,
                v.max_collateral_pct, v.max_collateral_inputs,
                v.plutus_cost_models
            );
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::ex_unit_prices>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "mem: {} steps: {}", v.mem, v.steps);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::ex_units>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "mem: {} steps: {}", v.mem, v.steps);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::plutus_cost_models>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "v1: ({}) v2: ({}) v3: ({})", v.v1, v.v2, v.v3);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::param_update>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "param_update [ hash: {} ", v.hash);
            out_it = _format_to(out_it, v.min_fee_a, "min_fee_a");
            out_it = _format_to(out_it, v.min_fee_b, "min_fee_b");
            out_it = _format_to(out_it, v.max_block_body_size, "max_block_body_size");
            out_it = _format_to(out_it, v.max_transaction_size, "max_transaction_size");
            out_it = _format_to(out_it, v.max_block_header_size, "max_block_header_size");
            out_it = _format_to(out_it, v.key_deposit, "key_deposit");
            out_it = _format_to(out_it, v.pool_deposit, "pool_deposit");
            out_it = _format_to(out_it, v.e_max, "e_max");
            out_it = _format_to(out_it, v.n_opt, "n_opt");
            out_it = _format_to(out_it, v.pool_pledge_influence, "pool_pledge_influence");
            out_it = _format_to(out_it, v.expansion_rate, "expansion_rate");
            out_it = _format_to(out_it, v.treasury_growth_rate, "treasury_growth_rate");
            out_it = _format_to(out_it, v.decentralization, "decentralization");
            out_it = _format_to(out_it, v.extra_entropy, "extra_entropy");
            out_it = _format_to(out_it, v.protocol_ver, "protocol_ver");
            out_it = _format_to(out_it, v.min_utxo_value, "min_utxo_value");
            out_it = _format_to(out_it, v.min_pool_cost, "min_pool_cost");
            out_it = _format_to(out_it, v.lovelace_per_utxo_byte, "lovelace_per_utxo_byte");
            out_it = _format_to(out_it, v.ex_unit_prices, "ex_unit_prices");
            out_it = _format_to(out_it, v.max_tx_ex_units, "max_tx_ex_units");
            out_it = _format_to(out_it, v.max_block_ex_units, "max_block_ex_units");
            out_it = _format_to(out_it, v.max_value_size, "max_value_size");
            out_it = _format_to(out_it, v.max_collateral_pct, "max_collateral_pct");
            out_it = _format_to(out_it, v.max_collateral_inputs, "max_collateral_inputs");
            out_it = _format_to(out_it, v.plutus_cost_models, "plutus_cost_models");
            return fmt::format_to(out_it, "]");
        }
    private:
        template<typename Y>
        static auto _format_to(auto out_it, const std::optional<Y> &v, const std::string_view name)
        {
            if (v)
                out_it = fmt::format_to(out_it, "{}: {} ", name, *v);
            return out_it;
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::param_update_proposal>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "key_id: {} epoch: {} params: {}", v.key_id, v.epoch, v.update);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::param_update_vote>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "key_id: {} proposal_id: {} vote: {}", v.key_id, v.proposal_id, v.vote);
        }
    };

    template<>
        struct formatter<daedalus_turbo::cardano::drep_voting_thresholds_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::cardano::drep_voting_thresholds_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(
                ctx.out(),
                "motion_no_confidence: {} committee_normal: {} committee_no_confidence: {} update_constitution: {} hard_fork_initiation: {} "
                "pp_network_group: {} pp_economic_group: {} pp_technical_group: {} pp_governance_group: {} treasury_withdrawal: {}",
                v.motion_no_confidence, v.committee_normal, v.committee_no_confidence, v.update_constitution, v.hard_fork_initiation,
                v.pp_network_group, v.pp_economic_group, v.pp_technical_group, v.pp_governance_group, v.treasury_withdrawal
            );
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::pool_voting_thresholds_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::cardano::pool_voting_thresholds_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(
                ctx.out(),
                "motion_of_no_confidence: {} committee_normal: {} committee_no_confidence: {} hard_fork_initiation: {} security_voting_threshold: {}",
                v.motion_of_no_confidence, v.committee_normal, v.committee_no_confidence, v.hard_fork_initiation, v.security_voting_threshold);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::slot_range>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "[{}, {}]", v.min(), v.max());
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::drep_t>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return std::visit([&](const auto &cred) {
                using T = std::decay_t<decltype(cred)>;
                if constexpr (std::is_same_v<T, daedalus_turbo::cardano::credential_t>)
                    return fmt::format_to(ctx.out(), "{}", cred);
                if constexpr (std::is_same_v<T, daedalus_turbo::cardano::drep_t::abstain_t>)
                    return fmt::format_to(ctx.out(), "abstain");
                if constexpr (std::is_same_v<T, daedalus_turbo::cardano::drep_t::no_confidence_t>)
                    return fmt::format_to(ctx.out(), "no-confidence");
                throw daedalus_turbo::error(fmt::format("unsupported drep.type: {}", typeid(v).name()));
                return fmt::format_to(ctx.out(), "error");
            }, v.val);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::plutus_cost_model>: formatter<daedalus_turbo::cardano::plutus_cost_model::storage_type> {
    };

    template<>
    struct formatter<daedalus_turbo::cardano::optional_slot>: formatter<std::optional<uint64_t>> {
    };

    template<typename T>
    struct formatter<daedalus_turbo::cardano::optional_t<T>>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            if (v)
                return fmt::format_to(ctx.out(), "{}", *v);
            return fmt::format_to(ctx.out(), "std::nullopt_t");
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::relay_info>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            switch (v.val.index()) {
                case 0: {
                    const auto &ra = std::get<daedalus_turbo::cardano::relay_addr>(v.val);
                    return fmt::format_to(ctx.out(), "port: {} ipv4: {} ipv6: {}", ra.port, ra.ipv4, ra.ipv6);
                }
                case 1: {
                    const auto &rh = std::get<daedalus_turbo::cardano::relay_host>(v.val);
                    return fmt::format_to(ctx.out(), "port: {} host: {}", rh.port, rh.host);
                }
                case 2: {
                    const auto &rd = std::get<daedalus_turbo::cardano::relay_dns>(v.val);
                    return fmt::format_to(ctx.out(), "dns: {}", rd.name);
                }
                default: return fmt::format_to(ctx.out(), "an unsupported relay_info value with index: {}", v.val.index());
            }
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::pool_metadata>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "url: {} hash: {}", v.url, v.hash);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::pool_params>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "vrf: {} pledge: {} cost: {} margin: {} reward: {} owners: {} relays: {} metadata: {}",
                v.vrf_vkey, v.pledge, v.cost, v.margin, v.reward_id, v.owners, v.relays, v.metadata);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::script_info>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "script {}: #{}", v.type(), v.script());
        }
    };

    template<typename K, typename V>
    struct formatter<daedalus_turbo::cardano::map_t<K, V>>: formatter<int> {
        template<typename FormatContext>
        auto format(const typename daedalus_turbo::cardano::map_t<K, V>::base_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::multi_asset_map>: formatter<int> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::cardano::multi_asset_map::base_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::reward_id_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::cardano::reward_id_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "reward_id hash: {} script: {}", v.hash(), v.script());
        }
    };

    template<typename T>
    struct formatter<daedalus_turbo::cardano::vector_t<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const typename daedalus_turbo::cardano::vector_t<T>::base_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };

    template<typename T>
    struct formatter<daedalus_turbo::cardano::nil_optional_t<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const typename daedalus_turbo::cardano::nil_optional_t<T>::base_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };

    template<typename T>
    struct formatter<daedalus_turbo::cardano::array_optional_t<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const typename daedalus_turbo::cardano::array_optional_t<T>::base_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };

    template<typename T>
    struct formatter<daedalus_turbo::cardano::prefix_optional_t<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const typename daedalus_turbo::cardano::prefix_optional_t<T>::base_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };

    template<>
    struct formatter<daedalus_turbo::cardano::stake_keyhash_t>: formatter<int> {
        template<typename FormatContext>
        auto format(const daedalus_turbo::cardano::stake_keyhash_t::base_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };
}

namespace std {
    template<>
    struct hash<daedalus_turbo::cardano::tx_out_ref> {
        size_t operator()(const auto &o) const noexcept
        {
            return *reinterpret_cast<const size_t *>(o.hash.data());
        }
    };

    template<>
    struct hash<daedalus_turbo::cardano::stake_pointer> {
        size_t operator()(const auto &stake_ptr) const noexcept
        {
            return (static_cast<uint64_t>(stake_ptr.slot) << 16) | ((stake_ptr.tx_idx & 0xFF) << 8) | (stake_ptr.cert_idx & 0xFF);
        }
    };

    template<>
    struct hash<daedalus_turbo::cardano::stake_ident> {
        size_t operator()(const auto &stake_id) const noexcept
        {
            return daedalus_turbo::buffer { stake_id.hash.data(), 8 }.to<size_t>();
        }
    };

    template<>
    struct hash<daedalus_turbo::cardano::stake_ident_hybrid> {
        size_t operator()(const auto &id) const noexcept
        {
            if (holds_alternative<daedalus_turbo::cardano::stake_ident>(id))
                return hash<daedalus_turbo::cardano::stake_ident> {} (get<daedalus_turbo::cardano::stake_ident>(id));
            if (holds_alternative<daedalus_turbo::cardano::stake_pointer>(id))
                return hash<daedalus_turbo::cardano::stake_pointer> {} (get<daedalus_turbo::cardano::stake_pointer>(id));
            return 0;
        }
    };

    template<>
    struct hash<daedalus_turbo::cardano::param_update> {
        size_t operator()(const auto &v) const noexcept
        {
            return daedalus_turbo::buffer { v.hash.data(), 8 }.to<size_t>();
        }
    };
}

namespace daedalus_turbo::cardano {
    inline json::value amount::to_json() const
    {
        return json::string { fmt::format("{}", *this) };
    }
}

#endif // !DAEDALUS_TURBO_CARDANO_TYPES_HPP
