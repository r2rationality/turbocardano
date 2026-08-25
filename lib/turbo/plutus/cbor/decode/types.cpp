/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cbor/zero2.hpp>
#include <turbo/common/variant.hpp>
#include <turbo/plutus/builtins.hpp>
#include <turbo/plutus/types.hpp>
#include <utfcpp/utf8.h>

namespace turbo::plutus {
    // Conway CDDL: plutus_data.
    static data _from_cbor(allocator &alloc, cbor::zero2::value &item);

    static data::list_type _list_from_cbor(allocator &alloc, cbor::zero2::value &v)
    {
        data::list_type dl { alloc };
        if (!v.indefinite()) [[likely]]
            dl.reserve(v.special_uint());
        auto &it = v.array();
        while (!it.done()) {
            dl.emplace_back(_from_cbor(alloc, it.read()));
        }
        return dl;
    }

    static data _from_cbor(allocator &alloc, cbor::zero2::value &v)
    {
        switch (const auto typ = v.type(); typ) {
            case cbor::major_type::tag: {
                auto &tag = v.tag();
                switch (auto id = tag.id(); id) {
                    case 2:
                        return { alloc, bint_type { alloc, big_uint_from_cbor(tag.read()) } };
                    case 3:
                        return { alloc, bint_type { alloc, big_nint_from_cbor(tag.read()) } };
                    default: {
                        if (id >= 121 && id < 128) {
                            id -= 121;
                            return { alloc, data_constr { alloc, bint_type { alloc, id }, _list_from_cbor(alloc, tag.read()) } };
                        }
                        if (id >= 1280 && id < 1280 + 128) {
                            id -= 1280 - 7;
                            return { alloc, data_constr { alloc, bint_type { alloc, id }, _list_from_cbor(alloc, tag.read()) } };
                        }
                        if (id == 102) {
                            auto &val = tag.read();
                            auto &it = val.array();
                            id = it.read().uint();
                            auto &array_val = it.read();
                            return { alloc, data_constr { alloc, bint_type { alloc, id }, _list_from_cbor(alloc, array_val) } };
                        }
                        throw error(fmt::format("unsupported tag id: {}", id));
                    }
                }
            }
            case cbor::major_type::array: return { alloc, _list_from_cbor(alloc, v) };
            case cbor::major_type::map: {
                data::map_type m { alloc };
                if (!v.indefinite()) [[likely]]
                    m.reserve(v.special_uint());
                auto &it = v.map();
                while (!it.done()) {
                    auto &mk = it.read_key();
                    auto kd = _from_cbor(alloc, mk);
                    auto &mv = it.read_val(std::move(mk));
                    auto vd = _from_cbor(alloc, mv);
                    m.emplace_back(alloc, std::move(kd), std::move(vd));
                }
                return { alloc, std::move(m) };
            }
            case cbor::major_type::bytes: {
                bstr_type::value_type buf { alloc };
                v.to_bytes(buf);
                return { alloc, bstr_type { alloc, std::move(buf) } };
            }
            case cbor::major_type::uint:
            case cbor::major_type::nint:
                return { alloc, bint_type { alloc, big_int_from_cbor(v) } };
            default: throw error(fmt::format("unsupported CBOR type {}!", typ));
        }
    }

    data data::from_cbor(allocator &alloc, const buffer bytes)
    {
        return _from_cbor(alloc, cbor::zero2::parse(bytes).get());
    }
}
