/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano {
    resign_committee_cold_cert resign_committee_cold_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            decltype(cold_id)::from_cbor(it.read()),
            decltype(anchor)::from_cbor(it.read())
        };
    }

    reg_drep_cert reg_drep_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return {
            decltype(drep_id)::from_cbor(it.read()),
            it.read().uint(),
            decltype(anchor)::from_cbor(it.read())
        };
    }

    update_drep_cert update_drep_cert::from_cbor(cbor::zero2::array_reader &it)
    {
        return  {
            decltype(drep_id)::from_cbor(it.read()),
            decltype(anchor)::from_cbor(it.read())
        };
    }

}

namespace turbo::cardano::conway {
    certificate_t certificate_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        switch (const auto typ = it.read().uint(); typ) {
            case 0: return {{ stake_reg_cert::from_cbor(it) }};
            case 1: return {{ stake_dereg_cert::from_cbor(it) }};
            case 2: return {{ stake_deleg_cert::from_cbor(it) }};
            case 3: return {{ pool_reg_cert::from_cbor(it) }};
            case 4: return {{ pool_retire_cert::from_cbor(it) }};
            case 7: return {{ reg_cert {
                credential_t::from_cbor(it.read()),
                it.read().uint()
            } }};
            case 8: return {{ unreg_cert {
                credential_t::from_cbor(it.read()),
                it.read().uint()
            } }};
            case 9: return {{ vote_deleg_cert {
                credential_t::from_cbor(it.read()),
                drep_t::from_cbor(it.read())
            } }};
            case 10: return {{ stake_vote_deleg_cert {
                credential_t::from_cbor(it.read()),
                it.read().bytes(),
                drep_t::from_cbor(it.read())
            } }};
            case 11: return {{ stake_reg_deleg_cert {
                credential_t::from_cbor(it.read()),
                it.read().bytes(),
                it.read().uint()
            } }};
            case 12: return {{ vote_reg_deleg_cert {
                credential_t::from_cbor(it.read()),
                drep_t::from_cbor(it.read()),
                it.read().uint()
            } }};
            case 13: return {{ stake_vote_reg_deleg_cert {
                credential_t::from_cbor(it.read()),
                it.read().bytes(),
                drep_t::from_cbor(it.read()),
                it.read().uint()
            } }};
            case 14: return {{ auth_committee_hot_cert {
                credential_t::from_cbor(it.read()),
                credential_t::from_cbor(it.read())
            } }};
            case 15: return {{ resign_committee_cold_cert::from_cbor(it) }};
            case 16: return {{ reg_drep_cert::from_cbor(it) }};
            case 17: return {{ unreg_drep_cert {
                credential_t::from_cbor(it.read()),
                it.read().uint()
            } }};
            case 18: return {{ update_drep_cert::from_cbor(it) }};
            [[unlikely]] default:
                throw error(fmt::format("unsupported cert type: {}", typ));
        }
    }
}
