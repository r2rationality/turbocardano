#pragma once
#ifndef DAEDALUS_TURBO_CARDANO_TYPES_BASE_HPP
#define DAEDALUS_TURBO_CARDANO_TYPES_BASE_HPP
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/array.hpp>
#include <dt/common/format.hpp>
#include <dt/cbor/encoder.hpp>
#include <dt/cbor/fwd.hpp>
#include <dt/container.hpp>

namespace daedalus_turbo::cardano {
    using key_hash =  byte_array<28>;
    using script_hash = byte_array<28>;
    using pool_hash =  byte_array<28>;
    using tx_hash =  byte_array<32>;
    using block_hash = byte_array<32>;
    using datum_hash = byte_array<32>;

    enum class era_t: uint8_t {
        byron, shelley, allegra, mary, alonzo, babbage, conway
    };
    extern era_t era_from_number(uint64_t era);

    struct era_encoder: cbor::encoder {
        explicit era_encoder(const era_t era):
            _era { era }
        {
        }

        era_encoder(const era_encoder &enc):
            _era { enc._era }
        {
        }

        era_t era() const
        {
            return _era;
        }
    private:
        const era_t _era;
    };

    template <typename T>
    concept constructible_from_cbor_c = requires(T, cbor::zero2::value v)
    {
        { T::from_cbor(v) };
    };

    template <typename T>
    concept integral_c = std::is_integral_v<T> && !constructible_from_cbor_c<T>;

    template <typename T>
    concept float_c = std::is_same_v<T, float> && !constructible_from_cbor_c<T>;

    template <typename T>
    concept constructible_from_buffer_c = std::is_constructible_v<T, buffer> && !constructible_from_cbor_c<T>;

    static_assert(constructible_from_buffer_c<block_hash>);

    template<typename T>
    T value_from_cbor(cbor::zero2::value &v)
    {
        return T::from_cbor(v);
    }

    template<float_c T>
    T value_from_cbor(cbor::zero2::value &v)
    {
        return v.float32();
    }

    template<integral_c T>
    T value_from_cbor(cbor::zero2::value &v)
    {
        return numeric_cast<T>(v.uint());
    }

    template<constructible_from_buffer_c T>
    T value_from_cbor(cbor::zero2::value &v)
    {
        return v.bytes();
    }

    template<typename T, typename ENC>
    void value_to_cbor(ENC &enc, const T &v) {
        if constexpr (std::is_same_v<uint64_t, T>) {
            enc.uint(v);
        } else if constexpr (std::is_same_v<uint32_t, T>) {
            enc.uint(v);
        }  else if constexpr (std::is_same_v<uint16_t, T>) {
            enc.uint(v);
        }  else if constexpr (std::is_same_v<uint8_t, T>) {
            enc.uint(v);
        } else if constexpr (std::is_convertible_v<T, buffer>) {
            enc.bytes(v);
        } else if constexpr (std::is_same_v<float, T>) {
            enc.float32(v);
        } else {
            v.to_cbor(enc);
        }
    }

    template<typename T>
    struct set_t: flat_set<T> {
        using base_type = flat_set<T>;
        using base_type::base_type;
        using item_observer_t = std::function<void(cbor::zero2::value &)>;
        using size_observer_t = std::function<void(size_t)>;

        static void foreach_item(cbor::zero2::value &v, const item_observer_t &observer, const size_observer_t &sz_observer=[](auto){})
        {
            switch (const auto typ = v.type(); typ) {
                case cbor::major_type::array: return foreach_item_array(v, std::move(observer), std::move(sz_observer));
                case cbor::major_type::tag: return foreach_item_array(v.tag().read(), std::move(observer), std::move(sz_observer));
                default: throw error(fmt::format("unsupported set value type {}", typ));
            }
        }

        static set_t<T> from_cbor(cbor::zero2::value &v)
        {
            set_t<T> res {};
            foreach_item(
                v,
                [&](auto &v) {
                    res.emplace_hint(res.end(), value_from_cbor<T>(v));
                },
                [&](const auto sz) {
                    res.reserve(sz);
                }
            );
            return res;
        }

        void to_cbor(era_encoder &enc) const
        {
            if (enc.era() == era_t::conway)
                enc.tag(258);
            enc.array_compact(base_type::size(), [&] {
                for (const auto &v: *this)
                    value_to_cbor(enc, v);
            });
        }
    private:
        static void foreach_item_array(cbor::zero2::value &v, const item_observer_t &observer, const size_observer_t &sz_observer)
        {
            if (!v.indefinite())
                sz_observer(v.special_uint());
            auto &it = v.array();
            while (!it.done()) {
                observer(it.read());
            }
        }
    };
}

namespace fmt {
    template<typename T>
    struct formatter<daedalus_turbo::cardano::set_t<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const typename daedalus_turbo::cardano::set_t<T>::base_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", v);
        }
    };
}

#endif // !DAEDALUS_TURBO_CARDANO_TYPES_BASE_HPP
