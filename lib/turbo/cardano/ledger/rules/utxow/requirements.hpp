#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "creds-needed.hpp"

namespace turbo::txwit {
    using namespace cardano;
    struct vkey_signer_t {
        key_hash hash {};

        bool operator<(const vkey_signer_t& o) const
        {
            return hash < o.hash;
        }
    };

    struct script_signer_t {
        script_hash hash {};
        redeemer_tag tag = redeemer_tag::spend;

        bool operator<(const script_signer_t& o) const
        {
            if (tag != o.tag)
                return tag < o.tag;
            return hash < o.hash;
        }
    };

    struct bootstrap_signer_t {
        key_hash root_hash {};

        bool operator<(const bootstrap_signer_t& o) const
        {
            return root_hash < o.root_hash;
        }
    };

    struct required_signer_t {
        using value_type = std::variant<vkey_signer_t, script_signer_t, bootstrap_signer_t>;

        value_type val;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.val);
        }

#include <turbo/cardano/ledger/rules/utxow/credentials.ipp>

        // required only for zpp serialization methods
        required_signer_t() =default;

        required_signer_t(value_type &&v): val { std::move(v) }
        {
        }

        required_signer_t(const redeemer_tag typ, const address &addr): required_signer_t { from_address(typ, addr) }
        {
        }

        required_signer_t(const credential_t cred): required_signer_t { from_cred(cred) }
        {
        }

        bool operator<(const required_signer_t &o) const
        {
            return std::visit<bool>([&](const auto &v, const auto &ov) {
                using T1 = std::decay_t<decltype(v)>;
                using T2 = std::decay_t<decltype(ov)>;
                if constexpr (!std::is_same_v<T1, T2>)
                    return val.index() < o.val.index();
                if constexpr (std::is_same_v<T1, T2>)
                    return v < ov;
            }, val, o.val);
        }
    };

    // Ledger.Conway.Specification.Script.Validation.credsNeeded: body-only
    // requirements are collected during parallel preprocessing. Input credentials
    // are added when the corresponding UTxOs are resolved.
    inline required_signer_t withdrawal_signer(const stake_ident &id)
    {
        if (id.script)
            return { script_signer_t { id.hash, redeemer_tag::reward } };
        return { vkey_signer_t { id.hash } };
    }

    inline required_signer_t voter_signer(const voter_t &voter)
    {
        const auto cred = cardano::ledger::rules::voter_credential(voter);
        if (cred.script)
            return { script_signer_t { cred.hash, redeemer_tag::vote } };
        return { vkey_signer_t { cred.hash } };
    }

    template<typename Signers>
    void add_proposal_signer(Signers &required, const gov_action_t &action)
    {
        if (const auto policy = cardano::ledger::rules::proposal_policy(action))
            required.emplace(script_signer_t { *policy, redeemer_tag::propose });
    }

    // Concrete witsKeyHashes: ordinary key hashes union bootstrap witness roots.
    // Retain the supplied witness kind because native scripts use ordinary vkeys
    // only. Probe the union without allocating another set; ordinary keys stay fast.
    template<typename Supplied>
    bool has_key_witness(const Supplied &supplied, const key_hash &hash)
    {
        return supplied.contains(required_signer_t { vkey_signer_t { hash } })
            || supplied.contains(required_signer_t { bootstrap_signer_t { hash } });
    }

    template<typename Required, typename Supplied, typename NativeAvailable>
    const required_signer_t *missing_required_witness(const Required &required,
        const Supplied &supplied, NativeAvailable &&native_available)
    {
        for (const auto &signer: required) {
            if (const auto *key = std::get_if<vkey_signer_t>(&signer.val)) {
                if (has_key_witness(supplied, key->hash))
                    continue;
            } else if (supplied.contains(signer)) {
                continue;
            }
            if (const auto *script = std::get_if<script_signer_t>(&signer.val);
                    script && native_available(script->hash))
                continue;
            return &signer;
        }
        return nullptr;
    }

    // UTXOW: needed - reference = supplied. Avoid materializing a difference set.
    template<typename Required, typename References, typename Supplied>
    bool exact_script_witnesses(const Required &required, const References &refs, const Supplied &supplied)
    {
        for (const auto &hash: required)
            if (!refs.contains(hash) && !supplied.contains(hash))
                return false;
        for (const auto &hash: supplied)
            if (!required.contains(hash) || refs.contains(hash))
                return false;
        return true;
    }
}
