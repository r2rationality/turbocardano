#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <boost/container/static_vector.hpp>
#include <deque>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <span>
#include <stdexcept>
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
        bls12_381_ml_result   = 11,
        array                 = 12,
        value                 = 13
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
        exp_mod_integer = 87,
        drop_list = 88,
        length_of_array = 89,
        list_to_array = 90,
        index_array = 91,
        bls12_381_g1_multi_scalar_mul = 92,
        bls12_381_g2_multi_scalar_mul = 93,
        insert_coin = 94,
        lookup_coin = 95,
        union_value = 96,
        value_contains = 97,
        value_data = 98,
        un_value_data = 99,
        scale_value = 100
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
                case type::array: return fmt::format_to(ctx.out(), "array");
                case type::value: return fmt::format_to(ctx.out(), "value");
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
                const auto mask = align - 1;
                return (bytes + mask) & ~mask;
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
                const auto mask = align - 1;
                return (bytes + mask) & ~mask;
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

    struct variable;
    struct force;
    struct apply;
    struct failure;
    struct t_delay;
    struct t_lambda;
    struct constant;
    struct t_builtin;
    struct t_constr;
    struct t_case;
    struct term_unary;
    struct term_value;

    struct term {
        using value_type = term_value;

        term() =delete;

        term(const term &o): _storage { o._storage }
        {
        }
        term(allocator &, value_type &&);
        term(allocator &, variable &&);
        term(allocator &, force &&);
        term(allocator &, apply &&);
        term(allocator &, failure &&);
        term(allocator &, t_delay &&);
        term(allocator &, t_lambda &&);
        term(allocator &, const constant &);
        term(allocator &, t_builtin &&);
        term(allocator &, t_constr &&);
        term(allocator &, t_case &&);

        term &operator=(const term &o)
        {
            _storage = o._storage;
            return *this;
        }

        bool operator==(const term &o) const;

        template<typename Visitor>
        decltype(auto) visit(Visitor &&visitor) const;
    private:
        // Three low pointer bits select the storage form. Small leaves are
        // encoded directly; structural nodes point at their exact arena payload.
        enum class storage_tag: uintptr_t {
            variable,
            unary,
            apply,
            constant,
            failure,
            builtin,
            constr,
            acase
        };

        static constexpr uintptr_t _tag_mask = 0x7;
        static constexpr uintptr_t _max_immediate = std::numeric_limits<uintptr_t>::max() >> 3;

        static uintptr_t _encode(const void *ptr, storage_tag tag) noexcept
        {
            return reinterpret_cast<uintptr_t>(ptr) | static_cast<uintptr_t>(tag);
        }

        static uintptr_t _encode_immediate(const uintptr_t val, storage_tag tag) noexcept
        {
            return (val << 3) | static_cast<uintptr_t>(tag);
        }

        storage_tag _tag() const noexcept
        {
            return static_cast<storage_tag>(_storage & _tag_mask);
        }

        uintptr_t _immediate() const noexcept
        {
            return _storage >> 3;
        }

        const void *_payload() const noexcept
        {
            return reinterpret_cast<const void *>(_storage & ~_tag_mask);
        }

        template<typename T>
        const T &_payload_as() const noexcept
        {
            return *static_cast<const T *>(_payload());
        }

        uintptr_t _storage;
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
        // Zero-based De Bruijn index: 0 refers to the nearest enclosing binder.
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
            n.reserve(2);
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
        bls12_381_g1_element(allocator &alloc, const blst_p1 &val):
            _ptr { alloc.make<blst_p1>(val) }
        {
        }

        const blst_p1 &get() const
        {
            return *_ptr;
        }

        bool operator==(const bls12_381_g1_element &o) const
        {
            return blst_p1_is_equal(&get(), &o.get());
        }
    private:
        allocator::ptr_type<blst_p1> _ptr;
    };

    struct bls12_381_g2_element {
        bls12_381_g2_element(allocator &alloc, const blst_p2 &val):
            _ptr { alloc.make<blst_p2>(val) }
        {
        }

        const blst_p2 &get() const
        {
            return *_ptr;
        }

        bool operator==(const bls12_381_g2_element &o) const
        {
            return blst_p2_is_equal(&get(), &o.get());
        }
    private:
        allocator::ptr_type<blst_p2> _ptr;
    };

    struct bls12_381_ml_result {
        bls12_381_ml_result(allocator &alloc, const blst_fp12 &val):
            _ptr { alloc.make<blst_fp12>(val) }
        {
        }

        const blst_fp12 &get() const
        {
            return *_ptr;
        }

        bool operator==(const bls12_381_ml_result &o) const
        {
            return memcmp(&get(), &o.get(), sizeof(blst_fp12)) == 0;
        }
    private:
        allocator::ptr_type<blst_fp12> _ptr;
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

    struct constant_list_flat;
    struct constant_list_slice;
    struct constant_list_cons;

    // A pointer-sized immutable list handle. Parsed lists use flat storage;
    // tail/drop create slices, and mkCons creates persistent cons cells.
    struct constant_list {
        using list_type = list_type<constant>;

        struct const_iterator {
            using iterator_category = std::forward_iterator_tag;
            using iterator_concept = std::forward_iterator_tag;
            using value_type = constant;
            using difference_type = std::ptrdiff_t;
            using pointer = const constant *;
            using reference = const constant &;

            const_iterator() =default;
            reference operator*() const;
            pointer operator->() const;
            const_iterator &operator++();
            const_iterator operator++(int);
            bool operator==(const const_iterator &) const;

        private:
            friend struct constant_list;

            const_iterator(const constant_list &, bool at_end);

            uintptr_t _storage = 0;
            size_t _tail_pos = 0;
            size_t _remaining = 0;
        };

        static constant_list make_one(allocator &alloc, constant &&);

        constant_list() =delete;
        constant_list(allocator &alloc, list_type &&);
        constant_list(allocator &alloc, std::initializer_list<constant>);
        constant_list(allocator &alloc, const constant_type &t);
        constant_list(allocator &alloc, const constant_type &t, std::initializer_list<constant>);
        constant_list(allocator &alloc, const constant_type &t, list_type &&);

        constant_list(const constant_list &) =default;
        constant_list &operator=(const constant_list &) =default;

        bool operator==(const constant_list &o) const;
        constant_list prepend(allocator &, const constant &) const;
        constant_list drop(allocator &, size_t) const;
        const constant_type &typ() const;
        bool empty() const;
        size_t size() const;
        const constant &front() const;
        const constant &back() const;
        const constant &at(size_t) const;
        const constant &operator[](size_t pos) const
        {
            return at(pos);
        }
        const_iterator begin() const;
        const_iterator end() const;

        template<typename Observer>
        void for_each(Observer &&) const;

        void copy_to(list_type &) const;
    private:
        enum class storage_tag: uintptr_t {
            flat,
            slice,
            cons
        };

        static constexpr uintptr_t _tag_mask = 0x3;

        explicit constant_list(uintptr_t storage): _storage { storage }
        {
        }

        static uintptr_t _encode(const void *ptr, const storage_tag tag) noexcept
        {
            return reinterpret_cast<uintptr_t>(ptr) | static_cast<uintptr_t>(tag);
        }

        static storage_tag _tag(const uintptr_t storage) noexcept
        {
            return static_cast<storage_tag>(storage & _tag_mask);
        }

        static const void *_payload(const uintptr_t storage) noexcept
        {
            return reinterpret_cast<const void *>(storage & ~_tag_mask);
        }

        template<typename T>
        static const T &_payload_as(const uintptr_t storage) noexcept
        {
            return *static_cast<const T *>(_payload(storage));
        }

        static std::span<const constant> _next_segment(uintptr_t &);

        uintptr_t _storage;
    };

    static_assert(sizeof(constant_list) == sizeof(uintptr_t));

    // Arrays and lists carry the same element type and constant storage, but
    // remain distinct universe types.  Reuse the list storage implementation
    // while retaining a distinct variant alternative.
    struct constant_array {
        constant_array(allocator &alloc, const constant_type &typ, constant_list::list_type &&vals):
            _storage { alloc, typ, std::move(vals) }
        {
        }

        constant_array(const constant_array &) =default;

        bool operator==(const constant_array &o) const
        {
            return _storage == o._storage;
        }

        const constant_type &typ() const
        {
            return _storage.typ();
        }

        bool empty() const
        {
            return _storage.empty();
        }

        size_t size() const
        {
            return _storage.size();
        }

        const constant &at(const size_t pos) const
        {
            return _storage.at(pos);
        }

        constant_list::const_iterator begin() const
        {
            return _storage.begin();
        }

        constant_list::const_iterator end() const
        {
            return _storage.end();
        }

        template<typename Observer>
        void for_each(Observer &&observer) const
        {
            _storage.for_each(std::forward<Observer>(observer));
        }

        void copy_to(constant_list::list_type &out) const
        {
            _storage.copy_to(out);
        }
    private:
        constant_list _storage;
    };

    struct asset_value {
        using key_type = std::vector<uint8_t>;
        using quantity_type = cpp_int;
        using inner_type = std::map<key_type, quantity_type>;
        using map_type = std::map<key_type, inner_type>;
        using input_inner_type = std::vector<std::pair<key_type, quantity_type>>;
        using input_type = std::vector<std::pair<key_type, input_inner_type>>;

        static constexpr size_t max_key_size = 32;
        static constexpr size_t max_data_size = 40'000;

        asset_value() =delete;
        asset_value(const asset_value &o): _ptr { o._ptr }
        {
        }
        asset_value(allocator &, map_type &&);

        static asset_value from_list(allocator &, input_type &&);
        static asset_value empty(allocator &alloc)
        {
            return { alloc, map_type {} };
        }

        size_t total_size() const;
        size_t max_inner_size() const;
        size_t negative_amounts() const;
        bool operator==(const asset_value &o) const
        {
            return _ptr->map == o._ptr->map;
        }
        const map_type &operator*() const
        {
            return _ptr->map;
        }
        const map_type *operator->() const
        {
            return &_ptr->map;
        }
    private:
        struct storage {
            map_type map;
            size_t total_size = 0;
            size_t max_inner_size = 0;
            size_t negative_amounts = 0;

            explicit storage(map_type &&);
        };
        allocator::ptr_type<storage> _ptr;
    };

    struct constant {
        using value_type = std::variant<bint_type, bstr_type, str_type, bool, constant_list, constant_array, constant_pair, asset_value,
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

        const constant_array &as_array() const
        {
            return std::get<constant_array>(*_ptr);
        }

        const asset_value &as_value() const
        {
            return std::get<asset_value>(*_ptr);
        }

        const value_type &operator*() const
        {
            return *_ptr;
        }
    private:
        explicit constant(const value_type *ptr): _ptr { ptr }
        {
        }

        friend struct term;
        friend struct value;
        allocator::ptr_type<value_type> _ptr;
    };

    struct constant_list_flat {
        constant_type typ;
        constant_list::list_type vals;
    };

    struct constant_list_slice {
        const constant_list_flat *root;
        size_t offset;
        size_t size;
    };

    struct constant_list_cons {
        constant head;
        uintptr_t tail;
        constant_type typ;
        size_t size;
    };

    static_assert(alignof(constant_list_flat) >= 4);
    static_assert(alignof(constant_list_slice) >= 4);
    static_assert(alignof(constant_list_cons) >= 4);

    template<typename Observer>
    void constant_list::for_each(Observer &&observer) const
    {
        auto storage = _storage;
        while (storage) {
            for (const auto &val: _next_segment(storage))
                observer(val);
        }
    }

    // this type is needed only for a prettier formatting; see the formatter definitions below
    struct constant_list_values_only {
        const constant_list &vals;
    };

    struct builtin_one_arg;
    struct builtin_two_arg;
    struct builtin_three_arg;
    struct builtin_four_arg;
    struct builtin_six_arg;
    using builtin_any = std::variant<builtin_one_arg, builtin_two_arg, builtin_three_arg, builtin_four_arg, builtin_six_arg>;

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

    struct term_unary {
        // delay, force, and lambda have the same payload shape and share one
        // pointer tag; the wire/AST tag preserves their distinct semantics.
        term_tag tag;
        term expr;
    };

    struct term_value: std::variant<variable, t_delay, force, t_lambda, apply, constant, failure, t_builtin, t_constr, t_case> {
        using std::variant<variable, t_delay, force, t_lambda, apply, constant, failure, t_builtin, t_constr, t_case>::variant;
    };

    static_assert(sizeof(term) == sizeof(uintptr_t));
    static_assert(alignof(term_unary) >= 8);
    static_assert(alignof(apply) >= 8);
    static_assert(alignof(constant::value_type) >= 8);
    static_assert(alignof(t_constr) >= 8);
    static_assert(alignof(t_case) >= 8);

    inline term::term(allocator &, variable &&v): _storage {}
    {
        if (v.idx > _max_immediate) [[unlikely]]
            throw error(fmt::format("variable index {} exceeds the compact term limit {}", v.idx, _max_immediate));
        _storage = _encode_immediate(v.idx, storage_tag::variable);
    }

    inline term::term(allocator &alloc, force &&v):
        _storage { _encode(alloc.make<term_unary>(term_tag::force, std::move(v.expr)).get(), storage_tag::unary) }
    {
    }

    inline term::term(allocator &alloc, apply &&v):
        _storage { _encode(alloc.make<turbo::plutus::apply>(std::move(v)).get(), storage_tag::apply) }
    {
    }

    inline term::term(allocator &, failure &&):
        _storage { _encode_immediate(0, storage_tag::failure) }
    {
    }

    inline term::term(allocator &alloc, t_delay &&v):
        _storage { _encode(alloc.make<term_unary>(term_tag::delay, std::move(v.expr)).get(), storage_tag::unary) }
    {
    }

    inline term::term(allocator &alloc, t_lambda &&v):
        _storage { _encode(alloc.make<term_unary>(term_tag::lambda, std::move(v.expr)).get(), storage_tag::unary) }
    {
    }

    inline term::term(allocator &, const constant &v):
        _storage { _encode(v._ptr.get(), storage_tag::constant) }
    {
    }

    inline term::term(allocator &, t_builtin &&v):
        _storage { _encode_immediate(static_cast<uintptr_t>(v.tag), storage_tag::builtin) }
    {
    }

    inline term::term(allocator &alloc, t_constr &&v):
        _storage { _encode(alloc.make<t_constr>(std::move(v)).get(), storage_tag::constr) }
    {
    }

    inline term::term(allocator &alloc, t_case &&v):
        _storage { _encode(alloc.make<t_case>(std::move(v)).get(), storage_tag::acase) }
    {
    }

    template<typename Visitor>
    decltype(auto) term::visit(Visitor &&visitor) const
    {
        switch (_tag()) {
            case storage_tag::variable:
                return visitor(variable { static_cast<size_t>(_immediate()) });
            case storage_tag::unary: {
                const auto &v = _payload_as<term_unary>();
                switch (v.tag) {
                    case term_tag::delay: return visitor(t_delay { v.expr });
                    case term_tag::force: return visitor(force { v.expr });
                    case term_tag::lambda: return visitor(t_lambda { v.expr });
                    default: throw error(fmt::format("invalid unary term tag: {}", v.tag));
                }
            }
            case storage_tag::apply:
                return visitor(_payload_as<turbo::plutus::apply>());
            case storage_tag::constant:
                return visitor(constant { static_cast<const constant::value_type *>(_payload()) });
            case storage_tag::failure:
                return visitor(failure {});
            case storage_tag::builtin:
                return visitor(t_builtin { static_cast<builtin_tag>(_immediate()) });
            case storage_tag::constr:
                return visitor(_payload_as<t_constr>());
            case storage_tag::acase:
                return visitor(_payload_as<t_case>());
        }
        throw error("invalid term storage tag");
    }

    struct term_format_ref {
        const term &val;
        size_t depth;
    };

    struct v_builtin;
    struct v_constr;
    struct v_delay;
    struct v_lambda;

    struct value {
        using value_type = std::variant<constant, v_delay, v_lambda, v_builtin, v_constr>;

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
        value(allocator &, v_delay &&);
        value(allocator &, v_lambda &&);
        value(allocator &, v_builtin &&);
        value(allocator &, v_constr &&);
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

        constant as_const() const;
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
        const constant_array &as_array() const;
        const asset_value &as_asset_value() const;
        bool operator==(const value &o) const;

        template<typename Visitor>
        decltype(auto) visit(Visitor &&visitor) const;
    private:
        enum class storage_tag: uintptr_t {
            constant,
            delay,
            lambda,
            builtin,
            constr
        };

        static constexpr uintptr_t _tag_mask = 0x7;

        static uintptr_t _encode(const void *ptr, storage_tag tag) noexcept
        {
            return reinterpret_cast<uintptr_t>(ptr) | static_cast<uintptr_t>(tag);
        }

        storage_tag _tag() const noexcept
        {
            return static_cast<storage_tag>(_storage & _tag_mask);
        }

        const void *_payload() const noexcept
        {
            return reinterpret_cast<const void *>(_storage & ~_tag_mask);
        }

        template<typename T>
        const T &_payload_as() const noexcept
        {
            return *static_cast<const T *>(_payload());
        }

        uintptr_t _storage;
    };

    struct value_args {
        using const_iterator = const value *;

        value_args() =default;
        value_args(const value *data, const size_t size): _data { data }, _size { size }
        {
        }

        size_t size() const noexcept
        {
            return _size;
        }

        bool empty() const noexcept
        {
            return !_size;
        }

        const value *data() const noexcept
        {
            return _data;
        }

        const_iterator begin() const noexcept
        {
            return _data;
        }

        const_iterator end() const noexcept
        {
            return _data ? _data + _size : _data;
        }

        const value &at(const size_t idx) const
        {
            if (idx >= _size) [[unlikely]]
                throw std::out_of_range("value argument index is out of range");
            return _data[idx];
        }

        const value &front() const
        {
            return at(0);
        }

        const value &operator[](const size_t idx) const noexcept
        {
            return _data[idx];
        }

    private:
        const value *_data = nullptr;
        size_t _size = 0;
    };

    struct builtin_args {
        static constexpr size_t max_size = 6;

        builtin_args() =default;
        builtin_args(allocator &alloc, const builtin_args &args, const value &arg):
            _ptr { alloc.make<storage>(args.values(), arg) }
        {
        }

        size_t size() const noexcept
        {
            return _ptr ? _ptr->size() : 0;
        }

        value_args values() const noexcept
        {
            return _ptr ? value_args { _ptr->data(), _ptr->size() } : value_args {};
        }

        operator value_args() const noexcept
        {
            return values();
        }

        const value &at(const size_t idx) const
        {
            return values().at(idx);
        }

        const value &front() const
        {
            return values().front();
        }

        bool operator==(const builtin_args &o) const
        {
            const auto a = values();
            const auto b = o.values();
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (!(a[i] == b[i]))
                    return false;
            }
            return true;
        }
    private:
        struct storage: boost::container::static_vector<value, builtin_args::max_size> {
            storage(const value_args args, const value &arg)
            {
                if (args.size() >= builtin_args::max_size) [[unlikely]]
                    throw std::length_error("at most six builtin arguments are supported");
                for (const auto &prev_arg: args)
                    this->emplace_back(prev_arg);
                this->emplace_back(arg);
            }
        };

        allocator::ptr_type<storage> _ptr {};
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
        operator value_args() const noexcept
        {
            return { _ptr->data(), _ptr->size() };
        }
    private:
        allocator::ptr_type<value_type> _ptr;
    };

    struct environment {
        struct node {
            using ptr_type = allocator::ptr_type<node>;
            // The tail is position 0 (the nearest binding); parents move outward.
            const ptr_type parent;
            const value val;

            bool operator==(const node &o) const
            {
                return val == o.val
                    && ((!parent && !o.parent) || (parent && o.parent && *parent == *o.parent));
            }
        };

        environment() =default;
        ~environment() =default;

        environment(allocator &alloc, const environment &parent, const value &val):
            _tail { alloc.make<node>(parent._tail, val) }
        {
        }

        environment(const environment &o): _tail { o._tail }
        {

        }

        const node *get() const
        {
            return _tail.get();
        }

        size_t size() const noexcept
        {
            size_t size = 0;
            for (auto node = _tail; node; node = node->parent)
                ++size;
            return size;
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
        builtin_args args;
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
        const term body;

        bool operator==(const v_lambda &o) const;
    };

    static_assert(sizeof(value) == sizeof(uintptr_t));
    static_assert(alignof(constant::value_type) >= 8);
    static_assert(alignof(v_delay) >= 8);
    static_assert(alignof(v_lambda) >= 8);
    static_assert(alignof(v_builtin) >= 8);
    static_assert(alignof(v_constr) >= 8);

    template<typename Visitor>
    decltype(auto) value::visit(Visitor &&visitor) const
    {
        switch (_tag()) {
            case storage_tag::constant: return visitor(as_const());
            case storage_tag::delay: return visitor(_payload_as<v_delay>());
            case storage_tag::lambda: return visitor(_payload_as<v_lambda>());
            case storage_tag::builtin: return visitor(_payload_as<v_builtin>());
            case storage_tag::constr: return visitor(_payload_as<v_constr>());
        }
        throw error("invalid runtime value tag");
    }

#if defined(__GNUC__) && !defined(__clang__)
    // GCC 13 reacts oddly to the libc++ implementation of std::pmr::string
#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    template<typename T, typename... Args>
    allocator::ptr_type<T> allocator::make(Args &&...a)
    {
        T *p = new (_mr->allocate(sizeof(T), alignof(T))) T { std::forward<Args>(a)... };
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
        T *p = new (_mr->allocate(sizeof(T), alignof(T))) T { std::forward<Args>(a)... };
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
    extern blst_p1 bls_g1_decompress(buffer bytes);
    extern blst_p2 bls_g2_decompress(buffer bytes);
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
            blst_p1_compress(reinterpret_cast<byte *>(comp.data()), &v.get());
            return fmt::format_to(ctx.out(), "0x{}", comp);
        }
    };

    template<>
    struct formatter<turbo::plutus::bls12_381_g2_element>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::bls12_381_g2_element &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            turbo::byte_array<96> comp {};
            blst_p2_compress(reinterpret_cast<byte *>(comp.data()), &v.get());
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

    template<bool nested, typename OutputIt>
    OutputIt format_constant_value_to(OutputIt out, const turbo::plutus::constant::value_type &vv)
    {
        using namespace turbo;
        using namespace turbo::plutus;
        return std::visit([&out](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return fmt::format_to(out, "()");
            } else if constexpr (std::is_same_v<T, bool>) {
                return fmt::format_to(out, "{}", v ? "True" : "False");
            } else if constexpr (std::is_same_v<T, bstr_type>) {
                return fmt::format_to(out, "#{}", buffer_lowercase { static_cast<buffer>(*v) });
            } else if constexpr (std::is_same_v<T, plutus::data>) {
                if constexpr (nested)
                    return fmt::format_to(out, "{}", v);
                else
                    return fmt::format_to(out, "({})", v);
            } else if constexpr (std::is_same_v<T, str_type>) {
                return fmt::format_to(out, "\"{}\"", escape_utf8_string(*v));
            } else if constexpr (std::is_same_v<T, constant_pair>) {
                auto next = fmt::format_to(out, "(");
                next = format_constant_value_to<true>(next, *v->first);
                next = fmt::format_to(next, ", ");
                next = format_constant_value_to<true>(next, *v->second);
                return fmt::format_to(next, ")");
            } else if constexpr (std::is_same_v<T, constant_list> || std::is_same_v<T, constant_array>) {
                auto next = fmt::format_to(out, "[");
                for (auto it = v.begin(); it != v.end(); ++it) {
                    next = format_constant_value_to<true>(next, **it);
                    if (std::next(it) != v.end())
                        next = fmt::format_to(next, ", ");
                }
                return fmt::format_to(next, "]");
            } else if constexpr (std::is_same_v<T, asset_value>) {
                auto next = fmt::format_to(out, "[");
                for (auto currency_it = v->begin(); currency_it != v->end(); ++currency_it) {
                    next = fmt::format_to(next, "(#{}", buffer_lowercase {
                        buffer { currency_it->first.data(), currency_it->first.size() } });
                    next = fmt::format_to(next, ", [");
                    for (auto token_it = currency_it->second.begin(); token_it != currency_it->second.end(); ++token_it) {
                        next = fmt::format_to(next, "(#{}", buffer_lowercase {
                            buffer { token_it->first.data(), token_it->first.size() } });
                        next = fmt::format_to(next, ", {})", token_it->second);
                        if (std::next(token_it) != currency_it->second.end())
                            next = fmt::format_to(next, ", ");
                    }
                    next = fmt::format_to(next, "])");
                    if (std::next(currency_it) != v->end())
                        next = fmt::format_to(next, ", ");
                }
                return fmt::format_to(next, "]");
            } else {
                return fmt::format_to(out, "{}", v);
            }
        }, vv);
    }

    template<>
    struct formatter<turbo::plutus::constant::value_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &vv, FormatContext &ctx) const -> decltype(ctx.out()) {
            return format_constant_value_to<false>(ctx.out(), vv);
        }
    };

    template<>
    struct formatter<turbo::plutus::constant_list_values_only>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[");
            for (auto it = v.vals.begin(); it != v.vals.end(); ++it) {
                const std::string_view sep { std::next(it) == v.vals.end() ? "" : ", " };
                out_it = format_constant_value_to<true>(out_it, **it);
                out_it = fmt::format_to(out_it, "{}", sep);
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
            if (v->typ == type_tag::list || v->typ == type_tag::array) {
                if (v->nested.size() != 1) [[unlikely]]
                    throw error(fmt::format("the nested type list for a sequence must have just one element but has {}", v->nested.size()));
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
            return fmt::format_to(ctx.out(), "(delay {})", v.expr);
        }
    };

    template<>
    struct formatter<turbo::plutus::apply>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::apply &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "[{} {}]", v.func, v.arg);
        }
    };

    template<>
    struct formatter<turbo::plutus::force>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::force &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(force {})", v.expr);
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
    struct formatter<turbo::plutus::term_format_ref>: formatter<int> {
        template<typename OutputIt>
        static OutputIt format_term(OutputIt out_it, const turbo::plutus::term &v, const size_t depth)
        {
            return v.visit([&](const auto &payload) {
                return format_value(out_it, payload, depth);
            });
        }

        template<typename OutputIt>
        static OutputIt format_terms(OutputIt out_it, const turbo::plutus::term_list &vals, const size_t depth)
        {
            for (auto it = vals->begin(); it != vals->end(); ++it) {
                out_it = format_term(out_it, *it, depth);
                if (std::next(it) != vals->end())
                    out_it = fmt::format_to(out_it, " ");
            }
            return out_it;
        }

        template<typename OutputIt, typename Value>
        static OutputIt format_value(OutputIt out_it, const Value &v, const size_t depth)
        {
            using namespace turbo::plutus;
            using val_type = std::decay_t<Value>;
            if constexpr (std::is_same_v<val_type, variable>) {
                const auto name_idx = v.idx < depth ? depth - 1 - v.idx : v.idx;
                return fmt::format_to(out_it, "v{}", name_idx);
            } else if constexpr (std::is_same_v<val_type, t_delay>) {
                out_it = fmt::format_to(out_it, "(delay ");
                out_it = format_term(out_it, v.expr, depth);
                return fmt::format_to(out_it, ")");
            } else if constexpr (std::is_same_v<val_type, force>) {
                out_it = fmt::format_to(out_it, "(force ");
                out_it = format_term(out_it, v.expr, depth);
                return fmt::format_to(out_it, ")");
            } else if constexpr (std::is_same_v<val_type, t_lambda>) {
                out_it = fmt::format_to(out_it, "(lam v{} ", depth);
                out_it = format_term(out_it, v.expr, depth + 1);
                return fmt::format_to(out_it, ")");
            } else if constexpr (std::is_same_v<val_type, apply>) {
                out_it = fmt::format_to(out_it, "[");
                out_it = format_term(out_it, v.func, depth);
                out_it = fmt::format_to(out_it, " ");
                out_it = format_term(out_it, v.arg, depth);
                return fmt::format_to(out_it, "]");
            } else if constexpr (std::is_same_v<val_type, t_constr>) {
                out_it = fmt::format_to(out_it, "(constr {} ", v.tag);
                out_it = format_terms(out_it, v.args, depth);
                return fmt::format_to(out_it, ")");
            } else if constexpr (std::is_same_v<val_type, t_case>) {
                out_it = fmt::format_to(out_it, "(case ");
                out_it = format_term(out_it, v.arg, depth);
                out_it = fmt::format_to(out_it, " ");
                out_it = format_terms(out_it, v.cases, depth);
                return fmt::format_to(out_it, ")");
            } else {
                return fmt::format_to(out_it, "{}", v);
            }
        }

        template<typename FormatContext>
        auto format(const turbo::plutus::term_format_ref &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return format_term(ctx.out(), v.val, v.depth);
        }
    };

    template<>
    struct formatter<turbo::plutus::t_lambda>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::t_lambda &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "(lam v0 {})", turbo::plutus::term_format_ref { v.expr, 1 });
        }
    };

    template<>
    struct formatter<turbo::plutus::term::value_type>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::term::value_type &vv, FormatContext &ctx) const -> decltype(ctx.out()) {
            return std::visit([&](const auto &payload) {
                return formatter<turbo::plutus::term_format_ref>::format_value(ctx.out(), payload, 0);
            }, vv);
        }
    };

    template<>
    struct formatter<turbo::plutus::term>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::term &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", turbo::plutus::term_format_ref { v, 0 });
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
    struct formatter<turbo::plutus::builtin_args>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::builtin_args &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[");
            const auto args = v.values();
            for (size_t i = 0; i < args.size(); ++i)
                out_it = fmt::format_to(out_it, "{}{}", args[i], i + 1 < args.size() ? ", " : "");
            return fmt::format_to(out_it, "]");
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
        template<typename OutputIt>
        static OutputIt format_nodes(OutputIt out_it, const turbo::plutus::environment::node &v)
        {
            size_t idx = 0;
            for (auto node = &v; node; node = node->parent.get(), ++idx) {
                if (idx)
                    out_it = fmt::format_to(out_it, ", ");
                out_it = fmt::format_to(out_it, "v{}={}", idx, node->val);
            }
            return out_it;
        }

        template<typename FormatContext>
        auto format(const turbo::plutus::environment::node &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return format_nodes(ctx.out(), v);
        }
    };

    template<>
    struct formatter<turbo::plutus::environment>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::environment &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            auto out_it = fmt::format_to(ctx.out(), "env [");
            if (const auto *node = v.get(); node)
                out_it = formatter<environment::node>::format_nodes(out_it, *node);
            return fmt::format_to(out_it, "]");
        }
    };

    template<>
    struct formatter<turbo::plutus::value>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::value &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return v.visit([&ctx](const auto &payload) {
                return fmt::format_to(ctx.out(), "{}", payload);
            });
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
            return fmt::format_to(ctx.out(), "(delay {})", v.expr);
        }
    };

    template<>
    struct formatter<turbo::plutus::v_lambda>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::v_lambda &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(lam v0 {})", term_format_ref { v.body, 1 });
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
