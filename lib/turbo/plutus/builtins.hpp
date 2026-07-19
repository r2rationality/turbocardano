#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus {
    enum class builtin_semantics: uint8_t {
        a, b, c, d, e
    };

    struct builtin_descriptor {
        uint8_t num_args = 0;
        uint8_t polymorphic_args = 0;
        uint8_t batch = 1;
        std::string_view name {};
    };
    namespace builtins {
#define TURBO_PLUTUS_BUILTIN_ARGS_1 const value &
#define TURBO_PLUTUS_BUILTIN_ARGS_2 const value &, const value &
#define TURBO_PLUTUS_BUILTIN_ARGS_3 const value &, const value &, const value &
#define TURBO_PLUTUS_BUILTIN_ARGS_4 const value &, const value &, const value &, const value &
#define TURBO_PLUTUS_BUILTIN_ARGS_6 const value &, const value &, const value &, const value &, const value &, const value &
#define TURBO_PLUTUS_BUILTIN(tag, arity, function, name, polymorphic, batch) \
        extern value function(allocator &, TURBO_PLUTUS_BUILTIN_ARGS_##arity);
#include <turbo/plutus/builtin-registry.inc>
#undef TURBO_PLUTUS_BUILTIN
#undef TURBO_PLUTUS_BUILTIN_ARGS_1
#undef TURBO_PLUTUS_BUILTIN_ARGS_2
#undef TURBO_PLUTUS_BUILTIN_ARGS_3
#undef TURBO_PLUTUS_BUILTIN_ARGS_4
#undef TURBO_PLUTUS_BUILTIN_ARGS_6

        extern value cons_byte_string_v2(allocator &, const value &, const value &);

        extern const std::array<builtin_descriptor, builtin_tag_count> descriptors;
        inline const builtin_descriptor &descriptor(const builtin_tag tag) noexcept
        {
            return descriptors[static_cast<size_t>(tag)];
        }
        extern builtin_semantics semantics_variant(cardano::script_type, uint64_t protocol_major);
        extern bool available(builtin_tag, cardano::script_type, uint64_t protocol_major);
        extern bool version_available(const version &, cardano::script_type, uint64_t protocol_major);
    }
}
