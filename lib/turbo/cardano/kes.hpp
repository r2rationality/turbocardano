#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <array>
#include <boost/date_time/period.hpp>
#include <span>
#include <turbo/crypto/blake2b.hpp>
#include <turbo/crypto/ed25519.hpp>
#include <turbo/util.hpp>

namespace turbo::cardano {
    using kes_vkey = crypto::ed25519::vkey;
    using kes_vkey_span = std::span<const uint8_t, sizeof(kes_vkey)>;

    template <size_t DEPTH=6>
    struct kes_signature {
        static constexpr size_t period_max = 1 << DEPTH;
        static constexpr size_t period_split_point = 1 << (DEPTH - 1);

        static constexpr size_t size()
        {
            return sizeof(crypto::ed25519::signature) + DEPTH * 2 * sizeof(crypto::ed25519::vkey);
        }

        explicit kes_signature(const buffer &bytes)
            : _lhs_vk { bytes.subspan(kes_signature<DEPTH - 1>::size(), sizeof(_lhs_vk)) },
                _rhs_vk { bytes.subspan(kes_signature<DEPTH - 1>::size() + sizeof(_lhs_vk), sizeof(_rhs_vk)) },
                _signature { bytes.subspan(0, kes_signature<DEPTH - 1>::size()) }
        {
        }

        [[nodiscard]] bool verify(size_t period, const kes_vkey_span &vkey, const buffer &msg) const
        {
            const auto computed_vkey = crypto::blake2b::digest(buffer { _lhs_vk.data(), sizeof(_lhs_vk) + sizeof(_rhs_vk) });
            if (span_memcmp(computed_vkey, vkey) != 0) [[unlikely]]
                return false;
            if (period >= period_max) [[unlikely]]
                throw error(fmt::format("KES period out of range: {}!", period));
            if (period < period_split_point)
                return _signature.verify(period, _lhs_vk, msg);
            return _signature.verify(period - period_split_point, _rhs_vk, msg);
        }
    private:
        crypto::blake2b::hash_32 _lhs_vk {};
        crypto::blake2b::hash_32 _rhs_vk {};
        kes_signature<DEPTH - 1> _signature {};
    };

    template <>
    struct kes_signature<0> {
        static constexpr size_t size()
        {
            return sizeof(_signature);
        }

        explicit kes_signature(const buffer &bytes)
            : _signature { bytes }
        {
        }

        [[nodiscard]] bool verify(size_t period, const kes_vkey_span &vkey, const buffer &msg) const
        {
            if (period != 0)
                throw error(fmt::format("period value must be 0 but got: {}", period));
            return crypto::ed25519::verify(_signature, vkey, msg);
        }
    private:
        crypto::ed25519::signature _signature {};
    };

    namespace kes {
        typedef turbo::error error;

        struct split_seed {
            crypto::ed25519::seed left;
            crypto::ed25519::seed right;

            explicit split_seed(const buffer &sd) {
                if (sd.size() != sizeof(crypto::ed25519::seed))
                    throw error(fmt::format("seed buffer must be of of {} bytes but got {}!", sizeof(crypto::ed25519::seed), sd.size()));
                uint8_vector tmp {};
                tmp << std::string_view { "\x01" } << sd;
                crypto::blake2b::digest(left, tmp);
                tmp.clear();
                tmp << std::string_view { "\x02" } << sd;
                crypto::blake2b::digest(right, tmp);
            }
        };

        template<size_t DEPTH>
        using signature = kes_signature<DEPTH>;

        template <size_t DEPTH>
        struct secret {
            static constexpr size_t period_end = 1 << DEPTH;
            static constexpr size_t period_split_point = 1 << (DEPTH - 1);
            static constexpr size_t signature_size = sizeof(crypto::ed25519::signature) + DEPTH * 2 * sizeof(crypto::ed25519::vkey);

            using signature = byte_array<signature_size>;

            explicit secret(const buffer &bytes)
                : _seed { bytes }, _left { _seed.left }, _right { _seed.right }
            {
                uint8_vector tmp {};
                tmp << _left.vkey() << _right.vkey();
                crypto::blake2b::digest(_vk, tmp);
            }

            void update()
            {
                if (_period + 1 >= period_end)
                    throw error(fmt::format("KES secret of level {} cannot grow >= {} while the current period is {}", DEPTH, period_end, _period));
                ++_period;
            }

            void sign(const std::span<uint8_t> &signature, const buffer &msg) const
            {
                if (_period < period_split_point) {
                    //_left.sign(signature, msg, _period);
                    _left.sign(signature, msg);
                } else {
                    //_right.sign(signature, msg, _period - period_split_point);
                    _right.sign(signature, msg);
                }
                span_memcpy(signature.subspan(_left.signature_size, sizeof(crypto::ed25519::vkey)), _left.vkey());
                span_memcpy(signature.subspan(_left.signature_size + sizeof(crypto::ed25519::vkey), sizeof(crypto::ed25519::vkey)), _right.vkey());
            }

            [[nodiscard]] const crypto::ed25519::vkey &vkey() const
            {
                return _vk;
            }
        private:
            split_seed _seed;
            secret<DEPTH - 1> _left, _right;
            crypto::ed25519::vkey _vk;
            uint32_t _period = 0;
        };

        template <>
        struct secret<0> {
            static constexpr size_t signature_size = sizeof(crypto::ed25519::signature);
            using signature = byte_array<signature_size>;

            explicit secret(const buffer &seed)
            {
                crypto::ed25519::create_from_seed(_sk, _vk, seed);
            }

            void update()
            {
                throw error("level 0 KES secret cannot be udpdated!");
            }

            [[nodiscard]] size_t period() const
            {
                return 0;
            }

            void sign(const std::span<uint8_t> &signature, const buffer &msg) const
            {
                crypto::ed25519::sign(signature.subspan(0, sizeof(crypto::ed25519::signature)), msg, _sk);
            }

            [[nodiscard]] const crypto::ed25519::vkey &vkey() const
            {
                return _vk;
            }
        private:
            crypto::ed25519::skey _sk;
            crypto::ed25519::vkey _vk;
        };
    }
}