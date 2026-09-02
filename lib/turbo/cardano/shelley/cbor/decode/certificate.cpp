/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano {
    stake_reg_cert stake_reg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { credential_t::from_cbor(it.read()) };
    }

    stake_dereg_cert stake_dereg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { credential_t::from_cbor(it.read()) };
    }

    stake_deleg_cert stake_deleg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { credential_t::from_cbor(it.read()), it.read().bytes() };
    }

    pool_reg_cert pool_reg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().bytes(), pool_params::from_cbor(it) };
    }

    pool_retire_cert pool_retire_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().bytes(), it.read().uint() };
    }

    genesis_deleg_cert genesis_deleg_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return { it.read().bytes(), it.read().bytes(), it.read().bytes() };
    }

    reward_source reward_source_from_cbor(cbor::zero2::value &v)
    {
        switch (const auto source_raw = v.uint(); source_raw) {
            case 0: return reward_source::reserves;
            case 1: return reward_source::treasury;
            [[unlikely]] default: throw error(fmt::format("unexpected value of reward source: {}!", source_raw));
        }
    }

    instant_reward_cert instant_reward_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        auto &reward = it.read();
        auto &r_it = reward.array();
        return { reward_source_from_cbor(r_it.read()), decltype(rewards)::from_cbor(r_it.read()) };
    }
}

namespace turbo::cardano::shelley {
    certificate_t certificate_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return {{ stake_reg_cert::from_cbor(it) }};
            case 1: return {{ stake_dereg_cert::from_cbor(it) }};
            case 2: return {{ stake_deleg_cert::from_cbor(it) }};
            case 3: return {{ pool_reg_cert::from_cbor(it) }};
            case 4: return {{ pool_retire_cert::from_cbor(it) }};
            case 5: return {{ genesis_deleg_cert::from_cbor(it) }};
            case 6: return {{ instant_reward_cert::from_cbor(it) }};
            [[unlikely]] default:
                throw error(fmt::format("unsupported cert type: {}", typ));
        }
    }
}
