/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_PLUTUS_FLAT_ENCODER_HPP
#define DAEDALUS_TURBO_PLUTUS_FLAT_ENCODER_HPP

#include <dt/plutus/flat.hpp>

namespace daedalus_turbo::plutus::flat {
    extern uint8_vector encode(const term &);
    extern uint8_vector encode(const version &, const term &);
    extern uint8_vector encode_cbor(const version &, const term &);
}

#endif // !DAEDALUS_TURBO_PLUTUS_FLAT_ENCODER_HPP
