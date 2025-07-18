/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/plutus/types.hpp>
#include "big-int.hpp"

namespace {
    using namespace turbo;
    using namespace turbo::plutus;

    using allocator_type = std::allocator<uint64_t>;
    using allocator = plutus::allocator;
    using empty_type = boost::multiprecision::detail::empty_value<typename boost::multiprecision::backends::detail::rebind<boost::multiprecision::limb_type, allocator_type>::type>;

    using bint_backend_parent_type = boost::multiprecision::cpp_int_backend<
        0,
        0,
        boost::multiprecision::signed_magnitude,
        boost::multiprecision::checked,
        std::allocator<uint64_t>
    >;

    struct bint_backend_type: bint_backend_parent_type
    {
        using bint_backend_parent_type::bint_backend_parent_type;
    };

    struct bint_type {
        using value_type = boost::multiprecision::number<bint_backend_parent_type>;

        bint_type() =delete;

        bint_type(const bint_type &o): _ptr { o._ptr }
        {
        }

        bint_type(allocator &alloc): _ptr { alloc.make<value_type>() }
        {
        }

        bint_type(allocator &alloc, const auto &v): _ptr { alloc.make<value_type>(v) }
        {
        }

        bint_type &operator=(const bint_type &o)
        {
            _ptr = o._ptr;
            return *this;
        }

        bool operator==(const auto &o) const
        {
            return *_ptr == o;
        }

        const value_type &operator*() const
        {
            return *_ptr;
        }
    private:
        allocator::ptr_type<value_type> _ptr;
    };
}

suite big_int_suite = [] {
    "big_int"_test = [] {
        "to_cbor"_test = [] {
            {
                cbor::encoder enc {};
                cpp_int v { 0x01020304 };
                v <<= 96;
                big_int_to_cbor(enc, v);
                expect_equal(enc.cbor(), uint8_vector::from_hex("C25001020304000000000000000000000000"));
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                cbor::encoder enc {};
                cpp_int v = 10;
                big_int_to_cbor(enc, v);
                expect_equal(enc.cbor(), uint8_vector::from_hex("0A"));
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                cbor::encoder enc {};
                cpp_int v { 0x01020304 };
                v <<= 96;
                v *= -1;
                big_int_to_cbor(enc, v);
                expect_equal(enc.cbor(), uint8_vector::from_hex("C35001020303FFFFFFFFFFFFFFFFFFFFFFFF"));
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                cbor::encoder enc {};
                cpp_int v = -10;
                big_int_to_cbor(enc, v);
                expect_equal(enc.cbor(), uint8_vector::from_hex("29"));
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                // the largest value encodable as uint
                cbor::encoder enc {};
                const cpp_int v { 0xFFFFFFFFFFFFFFFFULL };
                big_int_to_cbor(enc, v);
                expect_equal(enc.cbor(), uint8_vector::from_hex("1BFFFFFFFFFFFFFFFF"));
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                // the largest value encodable as nint
                cbor::encoder enc {};
                cpp_int v { 0xFFFFFFFFFFFFFFFFULL };
                ++v;
                v *= -1;
                big_int_to_cbor(enc, v);
                expect_equal(enc.cbor(), uint8_vector::from_hex("3BFFFFFFFFFFFFFFFF"));
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                cbor::encoder enc {};
                cpp_int v = 1;
                v <<= 32;
                big_int_to_cbor(enc, v);
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                cbor::encoder enc {};
                cpp_int v = 1;
                v <<= 32;
                v *= -1;
                big_int_to_cbor(enc, v);
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                cbor::encoder enc {};
                cpp_int v = 1;
                v <<= 128;
                big_int_to_cbor(enc, v);
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                cbor::encoder enc {};
                cpp_int v = 1;
                v <<= 128;
                v *= -1;
                big_int_to_cbor(enc, v);
                expect_equal(v, big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()));
            }
            {
                cbor::encoder enc {};
                enc.tag(259);
                enc.array(0);
                expect(throws([&] { big_int_from_cbor(cbor::zero2::parse(enc.cbor()).get()); }));
            }
        };
    };
};