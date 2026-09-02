/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cbor/zero2.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus {
    namespace {
        struct validation_result {};

        template<bool materialize>
        using decode_result = std::conditional_t<materialize, data, validation_result>;

        template<bool materialize>
        using list_result = std::conditional_t<materialize, data::list_type, validation_result>;

        template<bool materialize>
        decode_result<materialize> decode_data(allocator *, cbor::zero2::value &);

        static constexpr size_t bounded_bytes_chunk_size = 64;

        template<typename FUNC>
        size_t decode_bounded_bytes(cbor::zero2::value &v, FUNC &&func)
        {
            return v.for_each_bytes_chunk([&](const buffer chunk) {
                if (chunk.size() > bounded_bytes_chunk_size) [[unlikely]]
                    throw error(fmt::format(
                        "Plutus bounded-bytes chunks may not exceed {} bytes but got {}",
                        bounded_bytes_chunk_size, chunk.size()));
                func(chunk);
            });
        }

        size_t validate_bounded_bytes(cbor::zero2::value &v)
        {
            return decode_bounded_bytes(v, [](const buffer) {});
        }

        template<typename BYTES>
        size_t materialize_bounded_bytes(cbor::zero2::value &v, BYTES &bytes)
        {
            bytes.clear();
            return decode_bounded_bytes(v, [&](const buffer chunk) {
                bytes.insert(bytes.end(), chunk.begin(), chunk.end());
            });
        }

        template<bool materialize>
        list_result<materialize> decode_list(allocator *alloc, cbor::zero2::value &v)
        {
            if constexpr (materialize) {
                data::list_type result { *alloc };
                if (!v.indefinite()) [[likely]]
                    result.reserve(v.special_uint());
                auto &it = v.array();
                while (!it.done()) {
                    auto item = decode_data<true>(alloc, it.read());
                    result.emplace_back(std::move(item));
                }
                return result;
            } else {
                auto &it = v.array();
                while (!it.done())
                    static_cast<void>(decode_data<false>(nullptr, it.read()));
                return {};
            }
        }

        cpp_int decode_big_int(cbor::zero2::value &v, const bool negative)
        {
            cpp_int result = 0;
            decode_bounded_bytes(v, [&](const buffer chunk) {
                for (const auto byte: chunk) {
                    result *= 256;
                    result += byte;
                }
            });
            return negative ? -(result + 1) : result;
        }

        template<bool materialize>
        decode_result<materialize> decode_data(allocator *alloc, cbor::zero2::value &v)
        {
            switch (const auto typ = v.type(); typ) {
                case cbor::major_type::tag: {
                    auto &tag = v.tag();
                    auto id = tag.id();
                    switch (id) {
                        case 2: {
                            auto &value = tag.read();
                            if constexpr (materialize) {
                                auto integer = decode_big_int(value, false);
                                return { *alloc, bint_type { *alloc, integer } };
                            } else {
                                static_cast<void>(validate_bounded_bytes(value));
                                return {};
                            }
                        }
                        case 3: {
                            auto &value = tag.read();
                            if constexpr (materialize) {
                                auto integer = decode_big_int(value, true);
                                return { *alloc, bint_type { *alloc, integer } };
                            } else {
                                static_cast<void>(validate_bounded_bytes(value));
                                return {};
                            }
                        }
                        default: {
                            if (id >= 121 && id < 128) {
                                id -= 121;
                                auto &fields_value = tag.read();
                                if constexpr (materialize) {
                                    auto fields = decode_list<true>(alloc, fields_value);
                                    bint_type constructor { *alloc, id };
                                    data_constr value { *alloc, constructor, std::move(fields) };
                                    return { *alloc, std::move(value) };
                                } else {
                                    static_cast<void>(decode_list<false>(nullptr, fields_value));
                                    return {};
                                }
                            }
                            if (id >= 1280 && id <= 1400) {
                                id -= 1280 - 7;
                                auto &fields_value = tag.read();
                                if constexpr (materialize) {
                                    auto fields = decode_list<true>(alloc, fields_value);
                                    bint_type constructor { *alloc, id };
                                    data_constr value { *alloc, constructor, std::move(fields) };
                                    return { *alloc, std::move(value) };
                                } else {
                                    static_cast<void>(decode_list<false>(nullptr, fields_value));
                                    return {};
                                }
                            }
                            if (id == 102) {
                                auto &value = tag.read();
                                auto &it = value.array();
                                id = it.read().uint();
                                auto &fields_value = it.read();
                                if constexpr (materialize) {
                                    auto fields = decode_list<true>(alloc, fields_value);
                                    if (!it.done()) [[unlikely]]
                                        throw error("Plutus constructor tag 102 has trailing elements");
                                    bint_type constructor { *alloc, id };
                                    data_constr result { *alloc, constructor, std::move(fields) };
                                    return { *alloc, std::move(result) };
                                } else {
                                    static_cast<void>(decode_list<false>(nullptr, fields_value));
                                    if (!it.done()) [[unlikely]]
                                        throw error("Plutus constructor tag 102 has trailing elements");
                                    return {};
                                }
                            }
                            throw error(fmt::format("unsupported tag id: {}", id));
                        }
                    }
                }
                case cbor::major_type::array: {
                    if constexpr (materialize) {
                        auto items = decode_list<true>(alloc, v);
                        return { *alloc, std::move(items) };
                    } else {
                        static_cast<void>(decode_list<false>(nullptr, v));
                        return {};
                    }
                }
                case cbor::major_type::map: {
                    if constexpr (materialize) {
                        data::map_type result { *alloc };
                        if (!v.indefinite()) [[likely]]
                            result.reserve(v.special_uint());
                        auto &it = v.map();
                        while (!it.done()) {
                            auto &key_value = it.read_key();
                            auto key = decode_data<true>(alloc, key_value);
                            auto &mapped_value = it.read_val(std::move(key_value));
                            auto mapped = decode_data<true>(alloc, mapped_value);
                            result.emplace_back(*alloc, std::move(key), std::move(mapped));
                        }
                        return { *alloc, std::move(result) };
                    } else {
                        auto &it = v.map();
                        while (!it.done()) {
                            auto &key = it.read_key();
                            static_cast<void>(decode_data<false>(nullptr, key));
                            auto &mapped = it.read_val(std::move(key));
                            static_cast<void>(decode_data<false>(nullptr, mapped));
                        }
                        return {};
                    }
                }
                case cbor::major_type::bytes: {
                    if constexpr (materialize) {
                        bstr_type::value_type bytes { *alloc };
                        materialize_bounded_bytes(v, bytes);
                        bstr_type value { *alloc, std::move(bytes) };
                        return { *alloc, std::move(value) };
                    } else {
                        static_cast<void>(validate_bounded_bytes(v));
                        return {};
                    }
                }
                case cbor::major_type::uint:
                case cbor::major_type::nint: {
                    if constexpr (materialize) {
                        auto integer = big_int_from_cbor(v);
                        return { *alloc, bint_type { *alloc, integer } };
                    } else {
                        if (typ == cbor::major_type::uint)
                            static_cast<void>(v.uint());
                        else
                            static_cast<void>(v.nint_raw());
                        return {};
                    }
                }
                [[unlikely]] default: throw error(fmt::format("unsupported CBOR type {}!", typ));
            }
        }
    }

    data data::from_cbor(allocator &alloc, const buffer bytes)
    {
        cbor::zero2::decoder dec { bytes };
        auto result = decode_data<true>(&alloc, dec.read());
        if (!dec.done()) [[unlikely]]
            throw error("Plutus data contains trailing CBOR values");
        return result;
    }

    void data::validate_cbor(cbor::zero2::value &v)
    {
        static_cast<void>(decode_data<false>(nullptr, v));
    }

    void data::validate_cbor(const buffer bytes)
    {
        cbor::zero2::decoder dec { bytes };
        validate_cbor(dec.read());
        if (!dec.done()) [[unlikely]]
            throw error("Plutus data contains trailing CBOR values");
    }
}
