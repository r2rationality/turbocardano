#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/plutus/types.hpp>
#include <turbo/util.hpp>

namespace turbo::plutus::uplc {
    struct script {
        explicit script(allocator &alloc, uint8_vector &&bytes);
        script(allocator &alloc, uint8_vector &&bytes, cardano::script_type, uint64_t protocol_major);
        script(script &&);
        ~script();
	    plutus::version version() const;
        term program() const;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}