#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/ledger/shelley.hpp>

namespace turbo::cardano::ledger::alonzo {
    struct vrf_state: shelley::vrf_state {
        vrf_state(shelley::vrf_state &&);
    };

    struct state: shelley::state {
        state(shelley::state &&);
    protected:
        void _apply_alonzo_params(protocol_params &p) const;
        void _apply_param_update(const param_update &update) override;
        void _parse_protocol_params(protocol_params &params, cbor::zero2::value &values) const override;
        void _params_to_cbor(era_encoder &enc, const protocol_params &params) const override;
        void _param_update_to_cbor(era_encoder &enc, const param_update &update) const override;
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::cardano::ledger::alonzo::vrf_state>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<const turbo::cardano::ledger::shelley::vrf_state &>(v));
        }
    };
}