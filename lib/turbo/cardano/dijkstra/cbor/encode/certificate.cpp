/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/transaction.hpp>

namespace turbo::cardano::dijkstra {
    void certificate_t::to_cbor(era_encoder &enc) const
    {
        std::visit([&](const auto &cert) {
            using T = std::decay_t<decltype(cert)>;
            if constexpr (std::is_same_v<T, stake_deleg_cert>) {
                enc.array(3).uint(2);
                cert.stake_id.to_cbor(enc);
                enc.bytes(cert.pool_id);
            } else if constexpr (std::is_same_v<T, pool_reg_cert>) {
                const auto &params = cert.params;
                enc.array(params.bls_key ? 11 : 10).uint(3).bytes(cert.pool_id).bytes(params.vrf_vkey);
                if (params.bls_key) {
                    enc.array(2).bytes(params.bls_key->public_key).bytes(params.bls_key->possession_proof);
                }
                enc.uint(params.pledge).uint(params.cost);
                params.margin.to_cbor(enc);
                enc.bytes(params.reward_id);
                params.owners.to_cbor(enc);
                params.relays.to_cbor(enc);
                params.metadata.to_cbor(enc);
            } else if constexpr (std::is_same_v<T, pool_retire_cert>) {
                enc.array(3).uint(4).bytes(cert.pool_id).uint(static_cast<uint64_t>(cert.epoch));
            } else if constexpr (std::is_same_v<T, reg_cert>) {
                enc.array(3).uint(7);
                cert.stake_id.to_cbor(enc);
                enc.uint(cert.deposit);
            } else if constexpr (std::is_same_v<T, unreg_cert>) {
                enc.array(3).uint(8);
                cert.stake_id.to_cbor(enc);
                enc.uint(cert.deposit);
            } else if constexpr (std::is_same_v<T, vote_deleg_cert>) {
                enc.array(3).uint(9);
                cert.stake_id.to_cbor(enc);
                cert.drep.to_cbor(enc);
            } else if constexpr (std::is_same_v<T, stake_vote_deleg_cert>) {
                enc.array(4).uint(10);
                cert.stake_id.to_cbor(enc);
                enc.bytes(cert.pool_id);
                cert.drep.to_cbor(enc);
            } else if constexpr (std::is_same_v<T, stake_reg_deleg_cert>) {
                enc.array(4).uint(11);
                cert.stake_id.to_cbor(enc);
                enc.bytes(cert.pool_id).uint(cert.deposit);
            } else if constexpr (std::is_same_v<T, vote_reg_deleg_cert>) {
                enc.array(4).uint(12);
                cert.stake_id.to_cbor(enc);
                cert.drep.to_cbor(enc);
                enc.uint(cert.deposit);
            } else if constexpr (std::is_same_v<T, stake_vote_reg_deleg_cert>) {
                enc.array(5).uint(13);
                cert.stake_id.to_cbor(enc);
                enc.bytes(cert.pool_id);
                cert.drep.to_cbor(enc);
                enc.uint(cert.deposit);
            } else if constexpr (std::is_same_v<T, auth_committee_hot_cert>) {
                enc.array(3).uint(14);
                cert.cold_id.to_cbor(enc);
                cert.hot_id.to_cbor(enc);
            } else if constexpr (std::is_same_v<T, resign_committee_cold_cert>) {
                enc.array(3).uint(15);
                cert.cold_id.to_cbor(enc);
                cert.anchor.to_cbor(enc);
            } else if constexpr (std::is_same_v<T, reg_drep_cert>) {
                enc.array(4).uint(16);
                cert.drep_id.to_cbor(enc);
                enc.uint(cert.deposit);
                cert.anchor.to_cbor(enc);
            } else if constexpr (std::is_same_v<T, unreg_drep_cert>) {
                enc.array(3).uint(17);
                cert.drep_id.to_cbor(enc);
                enc.uint(cert.deposit);
            } else if constexpr (std::is_same_v<T, update_drep_cert>) {
                enc.array(3).uint(18);
                cert.drep_id.to_cbor(enc);
                cert.anchor.to_cbor(enc);
            } else {
                throw error(fmt::format("certificate type is not supported in Dijkstra: {}", typeid(T).name()));
            }
        }, value.val);
    }

    void certificates_t::to_cbor(era_encoder &enc) const
    {
        if (empty()) [[unlikely]]
            throw error("Dijkstra certificates must be nonempty");
        enc.tag(258);
        enc.array_compact(size(), [&] {
            for (const auto &cert: *this)
                certificate_t { cert }.to_cbor(enc);
        });
    }
}
