#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/plutus/builtins.hpp>

namespace turbo::plutus {
    struct script_validation {
        script_validation(cardano::script_type typ, uint64_t protocol_major):
            _typ { typ }, _protocol_major { protocol_major }
        {
        }

        void check_version(const version &ver) const
        {
            if (!builtins::version_available(ver, _typ, _protocol_major)) [[unlikely]] {
                throw error(fmt::format("UPLC version {} is not available for {} at protocol version {}",
                    ver, _typ, _protocol_major));
            }
        }

        void check_builtin(const t_builtin &builtin) const
        {
            if (!builtins::available(builtin.tag, _typ, _protocol_major)) [[unlikely]] {
                throw error(fmt::format("builtin {} is not available for {} at protocol version {}",
                    builtin.name(), _typ, _protocol_major));
            }
        }

        void check_constant(const constant_type &typ) const
        {
            if (_protocol_major < 11 && _contains_batch_six_type(typ)) [[unlikely]] {
                throw error(fmt::format(
                    "constant type {} is not available before protocol version 11", typ));
            }
            if (_protocol_major >= 11) {
                const auto header_size = _constant_type_header_size(typ);
                if (header_size > 32) [[unlikely]] {
                    throw error(fmt::format(
                        "constant type header size {} exceeds the protocol limit of 32", header_size));
                }
            }
        }

        void check_constr(const size_t num_fields) const
        {
            if (_protocol_major >= 11 && num_fields > 1024) [[unlikely]] {
                throw error(fmt::format(
                    "constr with {} fields exceeds the protocol limit of 1024", num_fields));
            }
        }

        static void check_term_version(const version &ver, const term_tag tag)
        {
            if ((tag == term_tag::constr || tag == term_tag::acase) && !(ver >= version { 1, 1, 0 })) [[unlikely]] {
                throw error(fmt::format("{} is not available in UPLC version {}",
                    tag == term_tag::constr ? "constr" : "case", ver));
            }
        }
    private:
        static bool _contains_batch_six_type(const constant_type &typ)
        {
            if (typ->typ == type_tag::array || typ->typ == type_tag::value)
                return true;
            for (const auto &nested: typ->nested) {
                if (_contains_batch_six_type(nested))
                    return true;
            }
            return false;
        }

        static uint64_t _constant_type_header_size(const constant_type &typ)
        {
            switch (typ->typ) {
                case type_tag::list:
                case type_tag::array:
                    return 2 + _constant_type_header_size(typ->nested.front());
                case type_tag::pair:
                    return 3 + _constant_type_header_size(typ->nested.front())
                        + _constant_type_header_size(typ->nested.back());
                default: return 1;
            }
        }

        cardano::script_type _typ;
        uint64_t _protocol_major;
    };
}
