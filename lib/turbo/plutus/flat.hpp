#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/plutus/types.hpp>
#include <turbo/util.hpp>

namespace turbo::plutus::flat {
    struct script {
        explicit script(allocator &alloc, const buffer bytes, bool cbor=true);
        explicit script(allocator &alloc, uint8_vector &&bytes, bool cbor=true);
        script(allocator &alloc, const buffer bytes, cardano::script_type, uint64_t protocol_major, bool cbor=true);
        script(allocator &alloc, uint8_vector &&bytes, cardano::script_type, uint64_t protocol_major, bool cbor=true);
        ~script();
	    plutus::version version() const;
        term program() const;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
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