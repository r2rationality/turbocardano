#pragma once
/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include <algorithm>
#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano::ledger::rules {
    // UTXO-inductive: serSize (getValue txout), excluding address, datum and script.
    inline uint32_t conway_value_size(const tx_out_data &out)
    {
        const auto head_size = [](uint64_t n) -> size_t {
            return n < 24 ? 1 : n <= UINT8_MAX ? 2 : n <= UINT16_MAX ? 3 : n <= UINT32_MAX ? 5 : 9;
        };
        size_t size = head_size(out.coin);
        if (!out.assets.empty()) {
            size += 1 + head_size(out.assets.size());
            for (const auto &[policy, assets]: out.assets) {
                size += head_size(policy.size()) + policy.size() + head_size(assets.size());
                for (const auto &[name, quantity]: assets)
                    size += head_size(name.size()) + name.size() + head_size(quantity);
            }
        }
        return numeric_cast<uint32_t>(size);
    }

    // UTXO-inductive output premises, reduced without knowing enacted parameters.
    // Concrete Conway uses 160 + serialized output bytes. The pinned Agda helper
    // uses 160 + 8 + size(Value); see the rule map's concrete-model qualification.
    struct output_size_limits {
        uint64_t max_coins_per_utxo_byte = UINT64_MAX;
        uint32_t max_value_size = 0;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.max_coins_per_utxo_byte, self.max_value_size);
        }

        void observe(const tx_out_data &out, uint32_t encoded_size)
        {
            // coin >= entry_size * parameter iff parameter <= floor(coin / entry_size).
            // Only these two scalars cross the batch boundary; no output re-encoding.
            max_coins_per_utxo_byte = std::min(max_coins_per_utxo_byte,
                out.coin / (uint64_t { 160 } + encoded_size));
            max_value_size = std::max(max_value_size, conway_value_size(out));
        }

        void validate(const protocol_params &params) const
        {
            if (max_value_size > params.max_value_size)
                throw error("output value exceeds maxValSize");
            if (params.lovelace_per_utxo_byte > max_coins_per_utxo_byte)
                throw error("output is below minimum coin");
        }
    };
}
