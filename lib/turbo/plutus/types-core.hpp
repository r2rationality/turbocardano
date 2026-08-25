#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <boost/container/static_vector.hpp>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <turbo/cbor/encoder.hpp>
#include <turbo/common/format.hpp>
#include <turbo/crypto/blst.hpp>
#include <turbo/math/big-int.hpp>
#include <turbo/plutus/types-allocator.hpp>
#include <turbo/plutus/types-tags.hpp>
#include <turbo/util.hpp>
#include <variant>

namespace turbo::plutus {
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
    struct t_builtin_spine;
    struct t_constr;
    struct t_case;
    struct term_unary;

    struct term {
        term() =delete;

        term(const term &o): _storage { o._storage }
        {
        }
        term(allocator &, variable &&);
        term(allocator &, force &&);
        term(allocator &, apply &&);
        term(allocator &, failure &&);
        term(allocator &, t_delay &&);
        term(allocator &, t_lambda &&);
        term(allocator &, const constant &);
        term(allocator &, t_builtin);
        term(allocator &, t_builtin_spine &&);
        term(allocator &, t_constr &&);
        term(allocator &, t_case &&);

        static term builtin_spine(allocator &, builtin_tag, uint8_t forces, std::span<const term> args);

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
        // A fourth bit distinguishes the 16-byte-aligned builtin-spine payload
        // from an ordinary apply node without increasing the term handle size.
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
        static constexpr uintptr_t _builtin_spine_mask = 0x8;
        static constexpr uintptr_t _max_immediate = std::numeric_limits<uintptr_t>::max() >> 3;

        explicit term(const uintptr_t storage): _storage { storage }
        {
        }

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

        bool _is_builtin_spine() const noexcept
        {
            return _tag() == storage_tag::apply && (_storage & _builtin_spine_mask);
        }

        static const void *_builtin_spine_payload(const uintptr_t storage) noexcept
        {
            return reinterpret_cast<const void *>(storage & ~(_tag_mask | _builtin_spine_mask));
        }

        bool _builtin_spine_equal(const term &) const;

        uintptr_t _storage;
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

    struct alignas(16) apply {
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

    struct bint_type {
        using value_type = boost::multiprecision::number<bint_backend_parent_type>;

        bint_type() =delete;

        bint_type(const bint_type &o): _ptr { o._ptr }
        {
        }

        bint_type(allocator &alloc): _ptr { alloc.make<value_type>() }
        {
            _register_destructor_if_needed(alloc);
        }

        bint_type(allocator &alloc, const auto &v): _ptr { alloc.make<value_type>(v) }
        {
            _register_destructor_if_needed(alloc);
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
        void _register_destructor_if_needed(allocator &alloc) const
        {
            // Runtime integers are immutable after construction. An inline backend can
            // therefore never acquire external limb storage later in its lifetime.
            if (_ptr->backend().capacity() > bint_backend_parent_type::internal_limb_count) [[unlikely]]
                alloc.register_destructor(_ptr);
        }

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
                if (!b.empty())
                    memcpy(data(), b.data(), b.size());
            }

            value_type(allocator &alloc, const size_t sz): std::pmr::vector<uint8_t>(sz, alloc.resource())
            {
            }

            value_type &operator=(const buffer &buf)
            {
                resize(buf.size());
                if (!buf.empty())
                    memcpy(data(), buf.data(), buf.size());
                return *this;
            }

            value_type &operator<<(const buffer buf)
            {
                if (buf.empty())
                    return *this;
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
        using key_type = uint8_vector;
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

    struct t_builtin_spine {
        t_builtin b;
        uint8_t forces;
        std::span<const term> args;

        bool operator==(const t_builtin_spine &o) const
        {
            if (!(b == o.b) || forces != o.forces || args.size() != o.args.size())
                return false;
            for (size_t i = 0; i < args.size(); ++i) {
                if (!(args[i] == o.args[i]))
                    return false;
            }
            return true;
        }
    };

    struct alignas(16) term_builtin_spine_storage {
        builtin_tag tag;
        uint8_t forces;
        uint8_t num_args;

        std::span<const term> args() const noexcept
        {
            return { reinterpret_cast<const term *>(this + 1), num_args };
        }
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

    static_assert(sizeof(term) == sizeof(uintptr_t));
    static_assert(alignof(term_unary) >= 8);
    static_assert(alignof(apply) >= 16);
    static_assert(alignof(term_builtin_spine_storage) >= 16);
    static_assert(sizeof(term_builtin_spine_storage) % alignof(term) == 0);
    static_assert(std::is_trivially_destructible_v<term>);
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

    inline term::term(allocator &, const t_builtin v):
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

    inline term term::builtin_spine(allocator &alloc, const builtin_tag tag, const uint8_t forces,
            const std::span<const term> args)
    {
        if (args.empty() || args.size() > 6) [[unlikely]]
            throw error(fmt::format("a builtin spine requires between one and six arguments but got {}", args.size()));
        const auto bytes = sizeof(term_builtin_spine_storage) + args.size() * sizeof(term);
        auto *storage = static_cast<term_builtin_spine_storage *>(
            alloc.resource()->allocate(bytes, alignof(term_builtin_spine_storage)));
        std::construct_at(storage, term_builtin_spine_storage {
            tag, forces, static_cast<uint8_t>(args.size())
        });
        auto *storage_args = reinterpret_cast<term *>(storage + 1);
        for (size_t i = 0; i < args.size(); ++i)
            std::construct_at(storage_args + i, args[i]);
        return term { _encode(storage, storage_tag::apply) | _builtin_spine_mask };
    }

    inline term::term(allocator &alloc, t_builtin_spine &&v):
        _storage { builtin_spine(alloc, v.b.tag, v.forces, v.args)._storage }
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
                if (_is_builtin_spine()) {
                    const auto &v = *static_cast<const term_builtin_spine_storage *>(
                        _builtin_spine_payload(_storage));
                    return visitor(t_builtin_spine { t_builtin { v.tag }, v.forces, v.args() });
                }
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
        builtin_args(allocator &alloc, const value_args args):
            _ptr { args.empty() ? allocator::ptr_type<storage> {} : alloc.make<storage>(args) }
        {
        }
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
            explicit storage(const value_args args)
            {
                if (args.size() > builtin_args::max_size) [[unlikely]]
                    throw std::length_error("at most six builtin arguments are supported");
                for (const auto &arg: args)
                    this->emplace_back(arg);
            }

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

    extern bool builtin_tag_known_name(std::string_view name);
    extern builtin_tag builtin_tag_from_name(std::string_view name);
    extern bstr_type bls_g1_compress(allocator &alloc, const bls12_381_g1_element &val);
    extern bstr_type bls_g2_compress(allocator &alloc, const bls12_381_g2_element &val);
    extern blst_p1 bls_g1_decompress(buffer bytes);
    extern blst_p2 bls_g2_decompress(buffer bytes);
    extern std::string escape_utf8_string(std::string_view);
}
