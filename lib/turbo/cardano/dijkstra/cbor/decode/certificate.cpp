/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    namespace {
        rational_u64 unit_interval_from_cbor(cbor::zero2::value &v)
        {
            auto res = rational_u64::from_cbor(v);
            if (!res.denominator || res.numerator > res.denominator) [[unlikely]]
                throw error("invalid unit interval");
            return res;
        }

        bls_key_t bls_key_from_cbor(cbor::zero2::value &v)
        {
            auto &it = v.array();
            bls_key_t res { it.read().bytes(), it.read().bytes() };
            if (!it.done()) [[unlikely]]
                throw error("unexpected trailing BLS key elements");
            return res;
        }

        set_t<stake_keyhash_t> pool_owners_from_cbor(cbor::zero2::value &v)
        {
            auto *items = &v;
            if (v.type() == cbor::major_type::tag) {
                auto &tag = v.tag();
                if (tag.id() != 258) [[unlikely]]
                    throw error(fmt::format("expected pool owner set tag 258 but got: {}", tag.id()));
                items = &tag.read();
            }
            set_t<stake_keyhash_t> result {};
            if (!items->indefinite())
                result.reserve(items->special_uint());
            auto &it = items->array();
            while (!it.done()) {
                const auto before = result.size();
                result.emplace_hint(result.end(), it.read().bytes());
                if (result.size() == before) [[unlikely]]
                    throw error("duplicate Dijkstra pool owner");
            }
            return result;
        }

        pool_reg_cert pool_registration_from_cbor(cbor::zero2::array_reader &it)
        {
            const pool_hash pool_id { it.read().bytes() };
            pool_params params {};
            params.vrf_vkey = it.read().bytes();

            auto &bls_or_pledge = it.read();
            if (bls_or_pledge.type() == cbor::major_type::array) {
                params.bls_key.emplace(bls_key_from_cbor(bls_or_pledge));
                params.pledge = it.read().uint();
            } else if (bls_or_pledge.is_null()) {
                static_cast<void>(bls_or_pledge.special());
                params.pledge = it.read().uint();
            } else {
                params.pledge = bls_or_pledge.uint();
            }

            params.cost = it.read().uint();
            params.margin = unit_interval_from_cbor(it.read());
            params.reward_id = reward_id_t { it.read().bytes() };
            params.owners = pool_owners_from_cbor(it.read());
            params.relays = decltype(params.relays)::from_cbor(it.read());
            params.metadata = decltype(params.metadata)::from_cbor(it.read());
            return { pool_id, std::move(params) };
        }
    }

    certificate_t certificate_t::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto type = it.read().uint();
        certificate_t res = [&]() -> certificate_t {
            switch (type) {
                case 2: return {{ stake_deleg_cert::from_cbor(it) }};
                case 3: return {{ pool_registration_from_cbor(it) }};
                case 4: return {{ pool_retire_cert::from_cbor(it) }};
                case 7: return {{ reg_cert { credential_t::from_cbor(it.read()), it.read().uint() } }};
                case 8: return {{ unreg_cert { credential_t::from_cbor(it.read()), it.read().uint() } }};
                case 9: return {{ vote_deleg_cert { credential_t::from_cbor(it.read()), drep_t::from_cbor(it.read()) } }};
                case 10: return {{ stake_vote_deleg_cert {
                    credential_t::from_cbor(it.read()), it.read().bytes(), drep_t::from_cbor(it.read())
                } }};
                case 11: return {{ stake_reg_deleg_cert {
                    credential_t::from_cbor(it.read()), it.read().bytes(), it.read().uint()
                } }};
                case 12: return {{ vote_reg_deleg_cert {
                    credential_t::from_cbor(it.read()), drep_t::from_cbor(it.read()), it.read().uint()
                } }};
                case 13: return {{ stake_vote_reg_deleg_cert {
                    credential_t::from_cbor(it.read()), it.read().bytes(), drep_t::from_cbor(it.read()), it.read().uint()
                } }};
                case 14: return {{ auth_committee_hot_cert {
                    credential_t::from_cbor(it.read()), credential_t::from_cbor(it.read())
                } }};
                case 15: return {{ resign_committee_cold_cert::from_cbor(it) }};
                case 16: return {{ reg_drep_cert::from_cbor(it) }};
                case 17: return {{ unreg_drep_cert { credential_t::from_cbor(it.read()), it.read().uint() } }};
                case 18: return {{ update_drep_cert::from_cbor(it) }};
                [[unlikely]] default:
                    throw error(fmt::format("unsupported Dijkstra certificate type: {}", type));
            }
        }();
        if (!it.done()) [[unlikely]]
            throw error(fmt::format("Dijkstra certificate type {} has trailing elements", type));
        return res;
    }

    certificates_t certificates_t::from_cbor(cbor::zero2::value &v)
    {
        certificates_t res {};
        auto *items = &v;
        if (v.type() == cbor::major_type::tag) {
            auto &tag = v.tag();
            if (tag.id() != 258) [[unlikely]]
                throw error(fmt::format("expected certificate set tag 258 but got: {}", tag.id()));
            items = &tag.read();
        }
        if (!items->indefinite()) [[likely]]
            res.reserve(items->special_uint());
        auto &it = items->array();
        while (!it.done()) {
            auto &item = it.read();
            auto certificate = std::move(certificate_t::from_cbor(item).value);
            for (const auto &existing: res) {
                if (existing == certificate) [[unlikely]]
                    throw error("duplicate Dijkstra certificate");
            }
            res.emplace_back(std::move(certificate));
        }
        if (res.empty()) [[unlikely]]
            throw error("Dijkstra certificates must be nonempty when supplied");
        return res;
    }
}
