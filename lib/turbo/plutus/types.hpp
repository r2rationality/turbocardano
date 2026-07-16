#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <deque>
#include <memory_resource>
#include <turbo/cbor/encoder.hpp>
#include <turbo/common/format.hpp>
#include <turbo/common/logger.hpp>
#include <turbo/crypto/blst.hpp>
#include <turbo/math/big-int.hpp>
#include <turbo/util.hpp>
#include <variant>

namespace turbo::plutus {
    struct version {
        uint64_t major = 1;
        uint64_t minor = 1;
        uint64_t patch = 0;

        version() =default;
        version(const std::string &s);

        version(const uint64_t major_, const uint64_t minor_, const uint64_t patch_):
            major { major_ }, minor { minor_ }, patch { patch_ }
        {
        }

        bool operator>=(const version &o) const;
        bool operator==(const version &o) const;

        bool empty() const
        {
            return major == 0 && minor == 0 && patch == 0;
        }

        operator std::string() const
        {
            return fmt::format("{}.{}.{}", major, minor, patch);
        }

        bool operator>=(const std::string &s) const
        {
            const version o { s };
            return *this >= o;
        }
    };

    enum class term_tag: uint8_t {
        variable = 0,
        delay    = 1,
        lambda   = 2,
        apply    = 3,
        constant = 4,
        force    = 5,
        error    = 6,
        builtin  = 7,
        constr   = 8,
        acase    = 9
    };

    enum class type_tag: uint8_t {
        integer              = 0,
        bytestring           = 1,
        string               = 2,
        unit                 = 3,
        boolean              = 4,
        list                 = 5,
        pair                 = 6,
        application          = 7,
        data                 = 8,
        bls12_381_g1_element = 9,
        bls12_381_g2_element = 10,
        bls12_381_ml_result   = 11
    };

    enum class builtin_tag: uint8_t {
        add_integer = 0,
        subtract_integer = 1,
        multiply_integer = 2,
        divide_integer = 3,
        quotient_integer = 4,
        remainder_integer = 5,
        mod_integer = 6,
        equals_integer = 7,
        less_than_integer = 8,
        less_than_equals_integer = 9,
        append_byte_string = 10,
        cons_byte_string = 11,
        slice_byte_string = 12,
        length_of_byte_string = 13,
        index_byte_string = 14,
        equals_byte_string = 15,
        less_than_byte_string = 16,
        less_than_equals_byte_string = 17,
        sha2_256 = 18,
        sha3_256 = 19,
        blake2b_256 = 20,
        verify_ed25519_signature = 21,
        append_string = 22,
        equals_string = 23,
        encode_utf8 = 24,
        decode_utf8 = 25,
        if_then_else = 26,
        choose_unit = 27,
        trace = 28,
        fst_pair = 29,
        snd_pair = 30,
        choose_list = 31,
        mk_cons = 32,
        head_list = 33,
        tail_list = 34,
        null_list = 35,
        choose_data = 36,
        constr_data = 37,
        map_data = 38,
        list_data = 39,
        i_data = 40,
        b_data = 41,
        un_constr_data = 42,
        un_map_data = 43,
        un_list_data = 44,
        un_i_data = 45,
        un_b_data = 46,
        equals_data = 47,
        mk_pair_data = 48,
        mk_nil_data = 49,
        mk_nil_pair_data = 50,
        // Plutus v2
        serialise_data = 51,
        verify_ecdsa_secp_256k1_signature = 52,
        verify_schnorr_secp_256k1_signature = 53,
        // Plutus v3
        bls12_381_g1_add = 54,
        bls12_381_g1_neg = 55,
        bls12_381_g1_scalar_mul = 56,
        bls12_381_g1_equal = 57,
        bls12_381_g1_compress = 58,
        bls12_381_g1_uncompress = 59,
        bls12_381_g1_hash_to_group = 60,
        bls12_381_g2_add = 61,
        bls12_381_g2_neg = 62,
        bls12_381_g2_scalar_mul = 63,
        bls12_381_g2_equal = 64,
        bls12_381_g2_compress = 65,
        bls12_381_g2_uncompress = 66,
        bls12_381_g2_hash_to_group = 67,
        bls12_381_miller_loop = 68,
        bls12_381_mul_ml_result = 69,
        bls12_381_final_verify = 70,
        keccak_256 = 71,
        blake2b_224 = 72,
        integer_to_byte_string = 73,
        byte_string_to_integer = 74,
        // Future
        and_byte_string = 75,
        or_byte_string = 76,
        xor_byte_string = 77,
        complement_byte_string = 78,
        read_bit = 79,
        write_bits = 80,
        replicate_byte = 81,
        shift_byte_string = 82,
        rotate_byte_string = 83,
        count_set_bits = 84,
        find_first_set_bit = 85,
        ripemd_160 = 86,
        exp_mod_integer = 87
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::plutus::term_tag>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::term_tag &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using term = turbo::plutus::term_tag;
            switch (v) {
                case term::variable: return fmt::format_to(ctx.out(), "term::variable");
                case term::delay: return fmt::format_to(ctx.out(), "term::delay");
                case term::lambda: return fmt::format_to(ctx.out(), "term::lambda");
                case term::apply: return fmt::format_to(ctx.out(), "term::apply");
                case term::constant: return fmt::format_to(ctx.out(), "term::constant");
                case term::force: return fmt::format_to(ctx.out(), "term::force");
                case term::error: return fmt::format_to(ctx.out(), "term::error");
                case term::builtin: return fmt::format_to(ctx.out(), "term::builtin");
                case term::constr: return fmt::format_to(ctx.out(), "term::constr");
                case term::acase: return fmt::format_to(ctx.out(), "term::case");
                default: return fmt::format_to(ctx.out(), "term::unknown({})", static_cast<int>(v));
            }
        }
    };

    template<>
    struct formatter<turbo::plutus::type_tag>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::type_tag &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using type = turbo::plutus::type_tag;
            switch (v) {
                case type::integer: return fmt::format_to(ctx.out(), "integer");
                case type::bytestring: return fmt::format_to(ctx.out(), "bytestring");
                case type::string: return fmt::format_to(ctx.out(), "string");
                case type::unit: return fmt::format_to(ctx.out(), "unit");
                case type::boolean: return fmt::format_to(ctx.out(), "bool");
                case type::list: return fmt::format_to(ctx.out(), "list");
                case type::pair: return fmt::format_to(ctx.out(), "pair");
                case type::application: return fmt::format_to(ctx.out(), "apply");
                case type::data: return fmt::format_to(ctx.out(), "data");
                case type::bls12_381_g1_element: return fmt::format_to(ctx.out(), "bls12_381_g1_element");
                case type::bls12_381_g2_element: return fmt::format_to(ctx.out(), "bls12_381_g2_element");
                case type::bls12_381_ml_result: return fmt::format_to(ctx.out(), "bls12_381_ml_result");
                default: throw turbo::error(fmt::format("unknown type: {}", static_cast<int>(v)));
            }
        }
    };
}

namespace turbo::plutus {
    typedef turbo::error error;

    // The idea behind the faster allocation is to release all objects at once
    // and save on the incremental de-allocation and calls to destructors.
    // For that to work all internal objects must be allocated using the same allocator,
    // so that there are no memory leaks
    struct allocator {
        using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

        template<typename T>
        struct ptr_type {
            ptr_type() =default;

            ptr_type(const T *ptr): _ptr { ptr }
            {
            }

            const T *get() const
            {
                return _ptr;
            }

            const T *operator->() const
            {
                return _ptr;
            }

            const T &operator*() const
            {
                return *_ptr;
            }

            operator bool() const
            {
                return _ptr;
            }
        private:
            const T *_ptr = nullptr;
        };

        allocator(const allocator &) =delete;
        allocator &operator=(const allocator &) =delete;
        allocator &operator=(allocator &&o) =delete;

        allocator():
            _mr { std::make_unique<std::pmr::monotonic_buffer_resource>(0x800000, my_resource::get()) },
            _ptrs { _mr.get() }
        {
        }

        allocator(allocator &&o):
            _mr { std::move(o._mr) },
            _ptrs { std::move(o._ptrs), _mr.get() }
        {
        }

        ~allocator()
        {
            for (const auto &[p, dtr]: _ptrs) {
                dtr(p);
            }
        }

        template<typename T, typename... Args>
        ptr_type<T> make(Args&&... a);

        template<typename T, typename... Args>
        ptr_type<T> make_foreign(Args&&... a);

        std::pmr::memory_resource *resource()
        {
            return _mr.get();
        }
    private:
        struct any_ptr {
            void *ptr = nullptr;
            void(*dtr)(const void*);
        };

        struct my_resource: std::pmr::memory_resource {
            using my_alloc = std::allocator<std::byte>;

            static my_resource *get()
            {
                static my_resource mr {};
                return &mr;
            }

            void *do_allocate(const size_t bytes, const size_t align) override
            {
                const auto aligned_size = _aligned_bytes(bytes, align);
                return _alloc.allocate(aligned_size);
            }

            void do_deallocate(void *ptr, const size_t bytes, const size_t align) override
            {
                const auto aligned_size = _aligned_bytes(bytes, align);
                _alloc.deallocate(reinterpret_cast<std::byte *>(ptr), aligned_size);
            }

            bool do_is_equal(const memory_resource &o) const noexcept override
            {
                return this == &o;
            }
        private:
            static constexpr size_t _aligned_bytes(const size_t bytes, const size_t align) {
                return bytes & align ? bytes + align - (bytes & align) : bytes;
            }

            my_alloc _alloc {};
        };

        struct counting_resource: std::pmr::memory_resource {
            using my_alloc = std::allocator<std::byte>;

            counting_resource(memory_resource *upstream): _upstream { upstream }
            {
                if (!_upstream) [[unlikely]]
                    throw error("counting resource requires a not-null upstream memory resource!");
            }

            void *do_allocate(const size_t bytes, const size_t align) override
            {
                const auto aligned_size = _aligned_bytes(bytes, align);
                _size += aligned_size;
                ++_cnts[aligned_size].num_allocs;
                return _mbr.allocate(aligned_size);
            }

            void do_deallocate(void *ptr, const size_t bytes, const size_t align) override
            {
                const auto aligned_size = _aligned_bytes(bytes, align);
                if (aligned_size > _size) [[unlikely]]
                    throw error("trying to deallocate more than has been allocated!");
                _size -= aligned_size;
                ++_cnts[aligned_size].num_deallocs;
                _mbr.deallocate(ptr, aligned_size);
            }

            bool do_is_equal(const memory_resource &o) const noexcept override
            {
                return this == &o;
            }

            void log_stats(const std::string_view context) const
            {
                logger::debug("{}: memory usage: {} bytes", context, _size);
            }
        private:
            static constexpr size_t _aligned_bytes(const size_t bytes, const size_t align) {
                return bytes & align ? bytes + align - (bytes & align) : bytes;
            }

            struct info_t {
                size_t num_allocs = 0;
                size_t num_deallocs = 0;
            };

            memory_resource *_upstream;
            std::pmr::monotonic_buffer_resource _mbr { _upstream };
            size_t _size = 0;
            std::map<size_t, info_t> _cnts {};
        };

        std::unique_ptr<std::pmr::memory_resource> _mr;
        std::pmr::vector<any_ptr> _ptrs;
    };

    struct str_type {
        using value_type = std::pmr::string;

        str_type() =delete;

        str_type(const str_type &o): _ptr { o._ptr }
        {
        }

        str_type(allocator &alloc, value_type &&s): _ptr { alloc.make<value_type>(std::move(s)) }
        {
        }

        str_type(allocator &alloc, std::string_view s): _ptr { alloc.make<value_type>(s, alloc.resource()) }
        {
        }

        bool operator==(const str_type &o) const
        {
            return *_ptr == *o._ptr;
        }

        const value_type *operator->() const
        {
            return _ptr.get();
        }

        const value_type &operator*() const
        {
            return *_ptr;
        }
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    struct term_value;

    struct term {
        using value_type = term_value;

        term() =delete;

        term(const term &o): _ptr { o._ptr }
        {
        }

        term(allocator &alloc, value_type &&v): _ptr { alloc.make<value_type>(std::move(v)) }
        {
        }

        term &operator=(const term &o)
        {
            _ptr = o._ptr;
            return *this;
        }

        bool operator==(const term &o) const;

        const value_type &operator*() const
        {
            return *_ptr;
        }
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    template<typename T>
    struct list_type: std::pmr::vector<T> {
        using base_type = std::pmr::vector<T>;

        list_type() =delete;

        list_type(allocator &alloc): base_type { alloc.resource() }
        {
        }

        list_type(allocator &alloc, std::initializer_list<T> il): base_type { il, alloc.resource() }
        {
        }

        list_type(allocator &alloc, list_type<T> &&l): base_type { std::move(l), alloc.resource() }
        {
        }
    };

    template<typename T>
    struct map_type: std::pmr::vector<T> {
        using base_type = std::pmr::vector<T>;

        map_type() =delete;

        map_type(allocator &alloc): base_type { alloc.resource() }
        {
        }

        map_type(allocator &alloc, std::initializer_list<T> il): base_type { il, alloc.resource() }
        {
        }

        map_type(allocator &alloc, map_type<T> &&l): base_type { std::move(l), alloc.resource() }
        {
        }
    };

    struct term_list {
        using value_type = list_type<term>;

        term_list() =delete;

        term_list(allocator &alloc, std::initializer_list<term> il): _ptr { alloc.make<value_type>(alloc, il) }
        {
        }

        term_list(allocator &alloc, value_type &&v): _ptr { alloc.make<value_type>(alloc, std::move(v)) }
        {
        }

        term_list(const term_list &o): _ptr { o._ptr }
        {
        }

        bool operator==(const term_list &o) const
        {
            return *_ptr == *o._ptr;
            /*if (_ptr->size() != o._ptr->size())
                return false;
            for (size_t i = 0; i < _ptr->size(); ++i) {
                if ((*_ptr)[i] != (*o._ptr)[i])
                    return false;
            }
            return true;*/
        }

        const value_type *operator->() const
        {
            return _ptr.get();
        }

        const value_type &operator*() const
        {
            return *_ptr;
        }
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    struct variable {
        size_t idx;

        bool operator==(const variable &o) const
        {
            return idx == o.idx;
        }
    };

    struct force {
        term expr;

        bool operator==(const force &o) const;
    };

    struct apply {
        term func;
        term arg;

        bool operator==(const apply &o) const;
    };

    struct failure {
        bool operator==(const failure &) const
        {
            return true;
        }
    };

    struct t_delay {
        term expr;

        bool operator==(const t_delay &o) const;
    };

    struct t_lambda {
        size_t var_idx;
        term expr;

        bool operator==(const t_lambda &o) const;
    };

    struct constant;

    struct constant_type {
        using list_type = list_type<constant_type>;
        struct value_type {
            type_tag typ;
            list_type nested;
        };

        static constant_type make_pair(allocator &alloc, constant_type &&fst, constant_type &&snd)
        {
            list_type n { alloc };
            n.emplace_back(std::move(fst));
            n.emplace_back(std::move(snd));
            return { alloc, type_tag::pair, std::move(n) };
        }

        static constant_type from_val(allocator &alloc, const constant &);

        constant_type() =delete;

        constant_type(const constant_type &o): _ptr { o._ptr }
        {
        }

        constant_type(constant_type &&o): _ptr { std::move(o._ptr) }
        {
        }

        constant_type(allocator &alloc, value_type &&v): _ptr { alloc.make<value_type>(std::move(v) ) }
        {
        }

        constant_type(allocator &alloc, const type_tag t): constant_type { alloc, value_type { t, list_type { alloc } } }
        {
        }

        constant_type(allocator &alloc, const type_tag t, list_type &&n):
            constant_type { alloc, value_type { t, std::move(n) } }
        {
        }

        constant_type(allocator &alloc, const type_tag t, std::initializer_list<constant_type> il):
            constant_type { alloc, value_type { t, { alloc, il } } }
        {
        }

        constant_type &operator=(constant_type &&o)
        {
            _ptr = std::move(o._ptr);
            return *this;
        }

        constant_type &operator=(const constant_type &o)
        {
            _ptr = o._ptr;
            return *this;
        }

        bool operator==(const constant_type &o) const
        {
            return _ptr->typ == o._ptr->typ && _ptr->nested == o._ptr->nested;
        }

        const value_type *operator->() const
        {
            return _ptr.get();
        }
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    struct bls12_381_g1_element {
        blst_p1 val {};

        bool operator==(const bls12_381_g1_element &o) const
        {
            return blst_p1_is_equal(&val, &o.val);
        }
    };

    struct bls12_381_g2_element {
        blst_p2 val {};

        bool operator==(const bls12_381_g2_element &o) const
        {
            return blst_p2_is_equal(&val, &o.val);
        }
    };

    struct bls12_381_ml_result {
        blst_fp12 val {};

        bool operator==(const bls12_381_ml_result &o) const
        {
            return memcmp(&val, &o.val, sizeof(val)) == 0;
        }
    };

    struct data;

    struct data_pair {
        using value_type = std::pair<data, data>;

        data_pair(allocator &alloc, const data &fst, const data &snd):
            _ptr { alloc.make<value_type>(fst, snd) }
        {
        }

        bool operator==(const data_pair &o) const;
        const value_type &operator*() const;
        const value_type *operator->() const;
    private:
        allocator::ptr_type<value_type> _ptr;
    };

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
        //using value_type = boost::multiprecision::checked_int1024_t;

        bint_type() =delete;

        bint_type(const bint_type &o): _ptr { o._ptr }
        {
        }

        bint_type(allocator &alloc): _ptr { alloc.make_foreign<value_type>() }
        {
        }

        bint_type(allocator &alloc, const auto &v): _ptr { alloc.make_foreign<value_type>(v) }
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

        bool operator==(const bint_type &o) const
        {
            return *_ptr == *o;
        }

        const value_type &operator*() const
        {
            return *_ptr;
        }
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    struct data_constr {
        using list_type = list_type<data>;
        using value_type = std::pair<bint_type, list_type>;

        data_constr(allocator &alloc, uint64_t t, std::initializer_list<data> il);
        data_constr(allocator &alloc, uint64_t t, list_type &&l);
        data_constr(allocator &alloc, const bint_type &t, std::initializer_list<data> il);
        data_constr(allocator &alloc, const bint_type &t, list_type &&l);

        bool operator==(const data_constr &o) const;
        const value_type &operator*() const;
        const value_type *operator->() const;
    private:
        allocator::ptr_type<value_type> _ptr {};
    };

    struct bstr_type
    {
        struct value_type: std::pmr::vector<uint8_t> {
            using base_type = std::pmr::vector<uint8_t>;

            //value_type(const value_type &) =delete;

            value_type(allocator &alloc, value_type &&o):
                std::pmr::vector<uint8_t> { std::move(o), alloc.resource() }
            {
            }

            value_type(allocator &alloc, base_type &&o):
                std::pmr::vector<uint8_t> { std::move(o), alloc.resource() }
            {
            }

            value_type(allocator &alloc):
                std::pmr::vector<uint8_t> { alloc.resource() }
            {
            }

            value_type(allocator &alloc, const buffer b):
                std::pmr::vector<uint8_t>(b.size(), alloc.resource())
            {
                memcpy(data(), b.data(), b.size());
            }

            value_type(allocator &alloc, const size_t sz): std::pmr::vector<uint8_t>(sz, alloc.resource())
            {
            }

            value_type &operator=(const buffer &buf)
            {
                resize(buf.size());
                memcpy(data(), buf.data(), buf.size());
                return *this;
            }

            value_type &operator<<(const buffer buf)
            {
                size_t end_off = size();
                resize(end_off + buf.size());
                memcpy(data() + end_off, buf.data(), buf.size());
                return *this;
            }

            value_type &operator<<(const uint8_t k)
            {
                reserve(size() + 1);
                emplace_back(k);
                return *this;
            }

            operator buffer() const
            {
                return { data(), size() };
            }

            std::string_view str() const
            {
                return std::string_view { reinterpret_cast<const char *>(data()), size() };
            }
        };

        static bstr_type from_hex(allocator &alloc, const std::string_view hex)
        {
            if (hex.size() % 2 != 0)
                throw error(fmt::format("hex string must have an even number of characters but got {}!", hex.size()));
            bstr_type::value_type data { alloc, hex.size() / 2 };
            init_from_hex(data, hex);
            return { alloc, std::move(data) };
        }

        bstr_type() =delete;

        bstr_type(const bstr_type &o): _ptr { o._ptr }
        {
        }

        bstr_type(allocator &alloc, const buffer b): _ptr { alloc.make<value_type>(alloc, b ) }
        {
        }

        bstr_type(allocator &alloc, value_type &&v): _ptr { alloc.make<value_type>(alloc, std::move(v) ) }
        {
        }

        bool operator==(const bstr_type &o) const
        {
            return *_ptr == *o._ptr;
        }

        const value_type *operator->() const
        {
            return _ptr.get();
        }

        const value_type &operator*() const
        {
            return *_ptr;
        }
    private:
        allocator::ptr_type<value_type> _ptr {};
    };

    struct data {
        using map_type = map_type<data_pair>;

        using list_type = list_type<data>;
        using int_type = bint_type;
        using bstr_type = bstr_type;
        using value_type = std::variant<data_constr, map_type, list_type, int_type, bstr_type>;

        static data from_cbor(allocator &alloc, buffer);
        static data bstr(allocator &alloc, const bstr_type &);
        static data bstr(allocator &alloc, buffer);
        static data bint(allocator &alloc, uint64_t);
        static data bint(allocator &alloc, const cpp_int &);
        static data bint(allocator &alloc, const int_type &);
        static data constr(allocator &alloc, uint64_t, list_type &&);
        static data constr(allocator &alloc, uint64_t, std::initializer_list<data>);
        static data constr(allocator &alloc, const int_type &, list_type &&);
        static data constr(allocator &alloc, const int_type &i, std::initializer_list<data>);
        static data list(allocator &alloc, list_type &&);
        static data list(allocator &alloc, std::initializer_list<data>);
        static data map(allocator &alloc, std::initializer_list<data_pair>);
        static data map(allocator &alloc, map_type &&);

        data() =delete;

        data(const data &o): _ptr { o._ptr }
        {
        }

        data(allocator &alloc, value_type &&v): _ptr { alloc.make<value_type>(std::move(v)) }
        {
        }

        data &operator=(const data &o)
        {
            _ptr = o._ptr;
            return *this;
        }

        bool operator==(const data &o) const
        {
            return *_ptr == *o._ptr;
        }

        const value_type &operator*() const
        {
            return *_ptr;
        }

        void to_cbor(cbor::encoder &) const;
        bstr_type as_cbor(allocator &alloc) const;
        std::string as_string(size_t shift=0) const;
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    struct constant_pair {
        using value_type = std::pair<constant, constant>;

        constant_pair() =delete;

        constant_pair(const constant_pair &o): _ptr { o._ptr }
        {
        }

        constant_pair(constant_pair &&o): _ptr { std::move(o._ptr) }
        {
        }

        constant_pair(allocator &alloc, constant &&fst, constant &&snd): _ptr { alloc.make<value_type>(std::move(fst), std::move(snd)) }
        {
        }

        constant_pair(allocator &alloc, const constant &fst, const constant &snd);

        constant_pair &operator=(const constant_pair &o)
        {
            _ptr = o._ptr;
            return *this;
        }

        bool operator==(const constant_pair &o) const;

        const value_type &operator*() const;

        const value_type *operator->() const
        {
            return _ptr.get();
        }
    private:
         allocator::ptr_type<value_type> _ptr;
    };

    struct constant_list {
        using list_type = list_type<constant>;
        struct value_type {
            constant_type typ;
            list_type vals;
        };

        static constant_list make_one(allocator &alloc, constant &&);

        constant_list() =delete;
        constant_list(allocator &alloc, list_type &&);
        constant_list(allocator &alloc, std::initializer_list<constant>);
        constant_list(allocator &alloc, const constant_type &t);
        constant_list(allocator &alloc, const constant_type &t, std::initializer_list<constant>);
        constant_list(allocator &alloc, const constant_type &t, list_type &&);

        constant_list(allocator &alloc, value_type &&v): _ptr { alloc.make<value_type>(std::move(v)) }
        {
        }

        constant_list(const constant_list &o): _ptr { o._ptr }
        {
        }

        constant_list &operator=(const constant_list &o)
        {
            _ptr = o._ptr;
            return *this;
        }

        bool operator==(const constant_list &o) const;
        const value_type *operator->() const;
        const value_type &operator*() const;
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    struct constant {
        using value_type = std::variant<bint_type, bstr_type, str_type, bool, constant_list, constant_pair,
            data, bls12_381_g1_element, bls12_381_g2_element, bls12_381_ml_result, std::monostate>;

        constant() =delete;

        constant(allocator &alloc, value_type &&v):
            _ptr { alloc.make<value_type>(std::move(v)) }
        {
        }

        constant(const constant &o): _ptr { o._ptr }
        {
        }

        constant &operator=(const constant &o)
        {
            _ptr = o._ptr;
            return *this;
        }

        const bint_type &as_int() const
        {
            return std::get<bint_type>(*_ptr);
        }

        bool as_bool() const
        {
            return std::get<bool>(*_ptr);
        }

        const bstr_type &as_bstr() const
        {
            return std::get<bstr_type>(*_ptr);
        }

        const str_type &as_str() const
        {
            return std::get<str_type>(*_ptr);
        }

        const data &as_data() const
        {
            return std::get<data>(*_ptr);
        }

        const constant_pair::value_type &as_pair() const
        {
            return *std::get<constant_pair>(*_ptr);
        }

        bool operator==(const constant &o) const
        {
            return *_ptr == *o._ptr;
        }

        const constant_list &as_list() const
        {
            return std::get<constant_list>(*_ptr);
        }

        const value_type &operator*() const
        {
            return *_ptr;
        }
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    // this type is needed only for a prettier formatting; see the formatter definitions below
    struct constant_list_values_only {
        const constant_list::list_type &vals;
    };

    struct builtin_one_arg;
    struct builtin_two_arg;
    struct builtin_three_arg;
    struct builtin_six_arg;
    using builtin_any = std::variant<builtin_one_arg, builtin_two_arg, builtin_three_arg, builtin_six_arg>;

    struct t_builtin {
        builtin_tag tag {};

        static t_builtin from_name(std::string_view);

        bool operator==(const t_builtin &o) const
        {
            return tag == o.tag;
        }

        size_t num_args() const;
        std::string_view name() const;
        size_t polymorphic_args() const;
    };

    struct t_constr {
        uint64_t tag;
        term_list args;

        bool operator==(const t_constr &o) const;
    };

    struct t_case {
        term arg;
        term_list cases;

        bool operator==(const t_case &o) const;
    };

    struct term_value: std::variant<variable, t_delay, force, t_lambda, apply, constant, failure, t_builtin, t_constr, t_case> {
        using std::variant<variable, t_delay, force, t_lambda, apply, constant, failure, t_builtin, t_constr, t_case>::variant;
    };

    struct v_builtin;
    struct v_constr;
    struct v_delay;
    struct v_lambda;

    struct value {
        using value_type = std::variant<constant, v_delay, v_lambda, v_builtin, v_constr>;
        using ptr_type = allocator::ptr_type<value_type>;

        static value make_list(allocator &, const constant_type &);
        static value make_list(allocator &, std::initializer_list<constant>);
        static value make_list(allocator &, constant_list::list_type &&);
        static value make_list(allocator &, const constant_type &, constant_list::list_type &&);
        static value make_list(allocator &, const constant_type &, std::initializer_list<constant>);
        static value make_pair(allocator &, constant &&, constant &&);
        static value unit(allocator &);
        static value boolean(allocator &, bool); // a factory method to disambiguate with value(int64_t) which is more frequent

        value() =delete;
        value(const value &);
        value(allocator &, value_type &&);
        value(allocator &, const constant &);
        value(allocator &, const bint_type &);
        value(allocator &, const cpp_int &);
        value(allocator &, int64_t);
        value(allocator &, data &&);
        value(allocator &, str_type &&);
        value(allocator &, std::string_view);
        value(allocator &, bstr_type &&);
        value(allocator &, const bstr_type &);
        value(allocator &, buffer);
        value(allocator &, const blst_p1 &);
        value(allocator &, const blst_p2 &);
        value(allocator &, const blst_fp12 &);

        value &operator=(const value &);

        const constant &as_const() const;
        const v_constr &as_constr() const;
        void as_unit() const;
        bool as_bool() const;
        const bint_type &as_int() const;
        const str_type &as_str() const;
        const bstr_type &as_bstr() const;
        const bls12_381_g1_element &as_bls_g1() const;
        const bls12_381_g2_element &as_bls_g2() const;
        const bls12_381_ml_result &as_bls_ml_res() const;
        const data &as_data() const;
        const constant_pair::value_type &as_pair() const;
        const constant_list &as_list() const;
        bool operator==(const value &o) const;
        const value_type &operator*() const;
        const value_type *operator->() const;
    private:
        ptr_type _ptr;
    };

    struct value_list {
        using value_type = list_type<value>;

        value_list() =delete;
        value_list(allocator &alloc);
        value_list(allocator &alloc, std::initializer_list<value>);
        value_list(allocator &alloc, value_type &&v);
        bool operator==(const value_list &) const;
        const value_type &operator*() const;
        const value_type *operator->() const;
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    struct environment {
        struct node {
            using ptr_type = allocator::ptr_type<node>;
            const ptr_type parent;
            const size_t var_idx;
            const value val;

            bool operator==(const node &o) const
            {
                return var_idx == o.var_idx && val == o.val
                    && ((!parent && !o.parent) || (parent && o.parent && *parent == *o.parent));
            }
        };

        environment() =default;
        ~environment() =default;

        environment(allocator &alloc, const environment &parent, const size_t var_idx, const value &val):
            _tail { alloc.make<node>(parent._tail, var_idx, val) }
        {
        }

        environment(const environment &o): _tail { o._tail }
        {

        }

        const node *get() const
        {
            return _tail.get();
        }

        bool operator==(const environment &o) const
        {
            return (!_tail && !o._tail) || (_tail && o._tail && *_tail == *o._tail);
        }
    private:
        const node::ptr_type _tail;
    };

    struct v_builtin {
        const t_builtin b;
        value_list args;
        size_t forces = 0;

        bool operator==(const v_builtin &o) const;
    };

    struct v_constr {
        const size_t tag;
        const value_list args;

        bool operator==(const v_constr &o) const;
    };

    struct v_delay {
        const environment env;
        const term expr;

        bool operator==(const v_delay &o) const;
    };

    struct v_lambda {
        const environment env;
        const size_t var_idx;
        const term body;

        bool operator==(const v_lambda &o) const;
    };

#if defined(__GNUC__) && !defined(__clang__)
    // GCC 13 reacts oddly to the libc++ implementation of std::pmr::string
#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    template<typename T, typename... Args>
    allocator::ptr_type<T> allocator::make(Args &&...a)
    {
        T *p = new (_mr->allocate(sizeof(T), 0x10)) T { std::forward<Args>(a)... };
        return p;
    }
#if defined(__GNUC__) && !defined(__clang__)
#       pragma GCC diagnostic pop
#endif

#if defined(__GNUC__) && !defined(__clang__)
    // GCC 13 reacts oddly to the libc++ implementation of std::pmr::string
#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    template<typename T, typename... Args>
    allocator::ptr_type<T> allocator::make_foreign(Args &&...a)
    {
        T *p = new (_mr->allocate(sizeof(T), 0x10)) T { std::forward<Args>(a)... };
        try {
            _ptrs.emplace_back(p, [](const void* x) { static_cast<const T*>(x)->~T(); });
        } catch (...) {
            // Call the destructor to release the memory allocated with alternative allocators.
            // There is no need to deallocate in _mr since all its buffers will be released at the end of its lifetime.
            p->~T();
            throw;
        }
        return p;
    }
#if defined(__GNUC__) && !defined(__clang__)
#       pragma GCC diagnostic pop
#endif

    extern bool builtin_tag_known_name(std::string_view name);
    extern builtin_tag builtin_tag_from_name(std::string_view name);
    extern bstr_type bls_g1_compress(allocator &alloc, const bls12_381_g1_element &val);
    extern bstr_type bls_g2_compress(allocator &alloc, const bls12_381_g2_element &val);
    extern bls12_381_g1_element bls_g1_decompress(buffer bytes);
    extern bls12_381_g2_element bls_g2_decompress(buffer bytes);
    extern std::string escape_utf8_string(std::string_view);
}

namespace fmt {
    template<>
        struct formatter<turbo::plutus::bint_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bint_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", *v);
        }
    };

    template<>
        struct formatter<turbo::plutus::str_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::str_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", *v);
        }
    };

    template<>
        struct formatter<turbo::plutus::bstr_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bstr_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<turbo::buffer>(*v));
        }
    };

    template<>
    struct formatter<turbo::plutus::builtin_tag>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::builtin_tag &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", turbo::plutus::t_builtin { v }.name());
        }
    };

    template<>
    struct formatter<turbo::plutus::bls12_381_g1_element>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bls12_381_g1_element &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            turbo::byte_array<48> comp {};
            blst_p1_compress(reinterpret_cast<byte *>(comp.data()), &v.val);
            return fmt::format_to(ctx.out(), "0x{}", comp);
        }
    };

    template<>
    struct formatter<turbo::plutus::bls12_381_g2_element>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bls12_381_g2_element &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            turbo::byte_array<96> comp {};
            blst_p2_compress(reinterpret_cast<byte *>(comp.data()), &v.val);
            return fmt::format_to(ctx.out(), "0x{}", comp);
        }
    };

    template<>
    struct formatter<turbo::plutus::bls12_381_ml_result>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bls12_381_ml_result &, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "opaque");
        }
    };

    template<>
    struct formatter<turbo::plutus::data>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &vv, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
#ifdef NDEBUG
            return fmt::format_to(ctx.out(), "{}", vv.as_string(0));
#else
            return fmt::format_to(ctx.out(), "{}", vv.as_string(4));
#endif
        }
    };

    template<>
    struct formatter<turbo::plutus::constant::value_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &vv, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo;
            using namespace turbo::plutus;
            return std::visit([&ctx](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return fmt::format_to(ctx.out(), "()");
                } else if constexpr (std::is_same_v<T, bool>) {
                    return fmt::format_to(ctx.out(), "{}", v ? "True" : "False");
                } else if constexpr (std::is_same_v<T, bstr_type>) {
                    return fmt::format_to(ctx.out(), "#{}", buffer_lowercase { static_cast<buffer>(*v) });
                } else if constexpr (std::is_same_v<T, plutus::data>) {
                    return fmt::format_to(ctx.out(), "({})", v);
                } else if constexpr (std::is_same_v<T, str_type>) {
                    return fmt::format_to(ctx.out(), "\"{}\"", escape_utf8_string(*v));
                } else if constexpr (std::is_same_v<T, constant_pair>) {
                    return fmt::format_to(ctx.out(), "({}, {})", *v->first, *v->second);
                } else if constexpr (std::is_same_v<T, constant_list>) {
                    return fmt::format_to(ctx.out(), "{}", constant_list_values_only { v->vals });
                } else {
                    return fmt::format_to(ctx.out(), "{}", v);
                }
            }, vv);
        }
    };

    template<>
    struct formatter<turbo::plutus::constant_list_values_only>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[");
            for (auto it = v.vals.begin(); it != v.vals.end(); ++it) {
                const std::string_view sep { std::next(it) == v.vals.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}{}", **it, sep);
            }
            return fmt::format_to(out_it, "]");
        }
    };

    template<>
    struct formatter<turbo::plutus::constant_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            if (v->nested.empty())
                return fmt::format_to(ctx.out(), "{}", v->typ);
            if (v->typ == type_tag::list) {
                if (v->nested.size() != 1) [[unlikely]]
                    throw error(fmt::format("the nested type list for a list must have just one element but has {}", v->nested.size()));
                return fmt::format_to(ctx.out(), "({} {})", v->typ, v->nested.front());
            }
            if (v->typ == type_tag::pair) {
                if (v->nested.size() != 2) [[unlikely]]
                    throw error(fmt::format("the nested type list for a pair must have two elements but has {}", v->nested.size()));
                return fmt::format_to(ctx.out(), "({} {} {})", v->typ, v->nested.front(), v->nested.back());
            }
            throw error(fmt::format("unsupported constant_type: {}!", v->typ));
        }
    };

    template<>
    struct formatter<turbo::plutus::constant>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::constant &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            turbo::plutus::allocator alloc {};
            return fmt::format_to(ctx.out(), "(con {} {})", turbo::plutus::constant_type::from_val(alloc, v), *v);
        }
    };

    template<>
    struct formatter<turbo::plutus::variable>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::variable &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "v{}", v.idx);
        }
    };

    template<>
    struct formatter<turbo::plutus::t_delay>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_delay &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(delay {})", *v.expr);
        }
    };

    template<>
    struct formatter<turbo::plutus::t_lambda>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_lambda &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(lam v{} {})", v.var_idx, *v.expr);
        }
    };

    template<>
    struct formatter<turbo::plutus::apply>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::apply &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "[{} {}]", *v.func, *v.arg);
        }
    };

    template<>
    struct formatter<turbo::plutus::force>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::force &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(force {})", *v.expr);
        }
    };

    template<>
    struct formatter<turbo::plutus::failure>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::failure &, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(error)");
        }
    };

    template<>
    struct formatter<turbo::plutus::t_builtin>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_builtin &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(builtin {})", v.name());
        }
    };

    template<>
    struct formatter<turbo::plutus::t_constr>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_constr &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(constr {} {})", v.tag, v.args);
        }
    };

    template<>
    struct formatter<turbo::plutus::t_case>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_case &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(case {} {})", v.arg, v.cases);
        }
    };

    template<>
    struct formatter<turbo::plutus::term::value_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::term::value_type &vv, FormatContext &ctx) const -> decltype(ctx.out()) {
            return std::visit([&ctx](const auto &v) {
                return fmt::format_to(ctx.out(), "{}", v);
            }, vv);
        }
    };

    template<>
    struct formatter<turbo::plutus::term>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::term &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", *v);
        }
    };

    template<>
    struct formatter<turbo::plutus::version>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::version &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<std::string>(v));
        }
    };

    template<typename T>
    struct formatter<turbo::plutus::list_type<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::list_type<T> &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<std::pmr::vector<T>>(v));
        }
    };

    template<>
    struct formatter<turbo::plutus::value_list::value_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::value_list::value_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<turbo::plutus::value_list::value_type::base_type>(v));
        }
    };

    template<>
    struct formatter<turbo::plutus::value_list>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::value_list &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", *v);
        }
    };

    template<>
    struct formatter<turbo::plutus::term_list>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::term_list &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = ctx.out();
            for (auto it = v->begin(); it != v->end(); ++it)
                out_it = fmt::format_to(out_it, "{}{}", *it, std::next(it) != v->end() ? " " : "");
            return out_it;
        }
    };

    template<>
    struct formatter<turbo::plutus::environment::node>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::environment::node &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            auto out_it = fmt::format_to(ctx.out(), "v{}={}", v.var_idx, v.val);
            if (v.parent)
                out_it = fmt::format_to(out_it, ", {}", *v.parent);
            return out_it;
        }
    };

    template<>
    struct formatter<turbo::plutus::environment>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::environment &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            if (const auto *node = v.get(); node)
                fmt::format_to(ctx.out(), "env [{}]", *node);
            return fmt::format_to(ctx.out(), "env []");
        }
    };

    template<>
    struct formatter<turbo::plutus::value>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::value &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "{}", *v);
        }
    };

    template<>
    struct formatter<turbo::plutus::v_builtin>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::v_builtin &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(builtin {} {})", v.b.name(), v.args);
        }
    };

    template<>
    struct formatter<turbo::plutus::v_constr>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::v_constr &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(constr {} {})", v.tag, v.args);
        }
    };

    template<>
    struct formatter<turbo::plutus::v_delay>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::v_delay &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(delay {})", *v.expr);
        }
    };

    template<>
    struct formatter<turbo::plutus::v_lambda>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::v_lambda &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(lam v{} {})", v.var_idx, *v.body);
        }
    };

    template<>
    struct formatter<turbo::plutus::value::value_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::value::value_type &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return std::visit([&ctx](const auto &vv) { return fmt::format_to(ctx.out(), "{}", vv); }, v);
        }
    };
}
