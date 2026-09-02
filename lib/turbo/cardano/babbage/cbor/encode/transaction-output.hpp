#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::plutus {
    struct allocator;
}

namespace turbo::cardano::babbage::detail {
    using native_script_encoder_t = void (*)(era_encoder &, buffer);

    void datum_option_to_cbor_semantic(
        era_encoder &, const datum_option_t &, plutus::allocator &);
    void datum_option_to_cbor_semantic(era_encoder &, const datum_option_t &);

    void script_info_to_cbor_semantic(
        era_encoder &, const script_info &, native_script_encoder_t);

    void transaction_output_to_cbor_semantic(
        era_encoder &, const tx_out_data &, plutus::allocator &, native_script_encoder_t);
}
