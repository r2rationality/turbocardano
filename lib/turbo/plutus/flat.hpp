#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus::flat {
    struct script {
        // Input is borrowed only for the duration of construction. The decoded
        // program and all surviving payloads are stored in alloc.
        explicit script(allocator &alloc, const buffer bytes, bool cbor=true);
        script(allocator &alloc, const buffer bytes, cardano::script_type, uint64_t protocol_major, bool cbor=true);
        plutus::version version() const;
        term program() const;
    private:
        struct decoded;
        struct decoder;

        explicit script(decoded &&);

        plutus::version _version;
        term _program;
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::plutus::flat::script>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus;
            return fmt::format_to(ctx.out(), "(program {} {})", v.version(), v.program());
        }
    };
}
