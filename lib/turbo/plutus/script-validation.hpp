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
            if (!builtins::version_available(ver, _typ, _protocol_major)) {
                throw error(fmt::format("UPLC version {} is not available for {} at protocol version {}",
                    ver, _typ, _protocol_major));
            }
        }

        void check_builtin(const t_builtin &builtin) const
        {
            if (!builtins::available(builtin.tag, _typ, _protocol_major)) {
                throw error(fmt::format("builtin {} is not available for {} at protocol version {}",
                    builtin.name(), _typ, _protocol_major));
            }
        }

        void check_constant(const constant_type &typ) const
        {
            if (_protocol_major >= 11) {
                const auto header_size = _constant_type_header_size(typ);
                if (header_size > 32) {
                    throw error(fmt::format(
                        "constant type header size {} exceeds the protocol limit of 32", header_size));
                }
            }
        }

        void check_constr(const size_t num_fields) const
        {
            if (_protocol_major >= 11 && num_fields > 1024) {
                throw error(fmt::format(
                    "constr with {} fields exceeds the protocol limit of 1024", num_fields));
            }
        }

        static void check_term_version(const version &ver, const term_tag tag)
        {
            if ((tag == term_tag::constr || tag == term_tag::acase) && !(ver >= version { 1, 1, 0 })) {
                throw error(fmt::format("{} is not available in UPLC version {}",
                    tag == term_tag::constr ? "constr" : "case", ver));
            }
        }
    private:
        static uint64_t _constant_type_header_size(const constant_type &typ)
        {
            switch (typ->typ) {
                case type_tag::list:
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
