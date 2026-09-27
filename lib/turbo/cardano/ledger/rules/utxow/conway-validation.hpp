#pragma once
/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <array>
#include <turbo/cardano/conway/block.hpp>
#include <turbo/cardano/ledger/rules/utxo/output-size.hpp>
#include "creds-needed.hpp"

namespace turbo::cardano::ledger::rules {
    using language_view_cache = std::array<uint8_vector, 8>;
    language_view_cache make_language_views(const plutus_cost_models &);

    // Compact preprocessing results. No ledger state or script decoding is duplicated.
    struct conway_validation {
        credential_requirements credentials {};
        flat_set<redeemer_id> redeemers {};
        flat_map<script_hash, script_type> scripts {};
        flat_set<datum_hash> datums {}, allowed_datums {};
        uint8_vector integrity_prefix {};
        std::optional<hash_32> integrity_hash {};
        std::optional<uint64_t> treasury {};
        output_size_limits outputs {};
        bool uses_v3_features = false;
        bool has_inline_datum = false;
        bool has_bootstrap = false;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.credentials, self.redeemers, self.scripts, self.datums,
                self.allowed_datums, self.integrity_prefix, self.integrity_hash,
                self.treasury, self.outputs,
                self.uses_v3_features, self.has_inline_datum, self.has_bootstrap);
        }

        void collect(const conway::tx &, std::optional<buffer> auxiliary);
        void observe_output(const tx_out_data &);
        void observe_input(const tx_out_data &, uint32_t index);
        void validate(const protocol_params &, const language_view_cache *views=nullptr,
            uint64_t actual_treasury=0) const;
    };

    uint32_t conway_tx_size(const tx_base &, std::optional<buffer> auxiliary);
    // The concrete ledger encoding of Agda's abstract hashScriptIntegrity.
    std::optional<hash_32> script_integrity_hash(buffer prefix,
        const flat_set<script_type> &languages, const plutus_cost_models &, bool has_data,
        const language_view_cache *views=nullptr);
}
