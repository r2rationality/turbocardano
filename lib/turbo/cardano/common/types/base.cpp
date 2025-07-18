/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/types/base.hpp>
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano {
    era_t era_from_number(const uint64_t era)
    {
        switch (era) {
            case 1: return era_t::byron;
            case 2: return era_t::shelley;
            case 3: return era_t::allegra;
            case 4: return era_t::mary;
            case 5: return era_t::alonzo;
            case 6: return era_t::babbage;
            case 7: return era_t::conway;
            [[unlikely]] default: throw error(fmt::format("unsupported era value: {}", era));
        }
    }
}