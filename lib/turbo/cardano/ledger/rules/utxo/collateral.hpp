#pragma once
/* Copyright (c) 2026 R2 Rationality OÜ. License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */
#include "amount.hpp"

namespace turbo::cardano::ledger::rules {
    struct collateral_balance {
        amount_sum coin {};
        asset_sums assets {};
        size_t count = 0;

        void add(const tx_out_data &out)
        {
            const auto addr = out.addr();
            if (addr.is_byron() || addr.pay_id().type != pay_ident::ident_type::SHELLEY_KEY)
                throw error("collateral must be controlled by a payment key");
            ++count;
            coin += out.coin;
            for (const auto &[policy, entries]: out.assets)
                for (const auto &[name, quantity]: entries)
                    assets[policy][name] += quantity;
        }

        uint64_t validate(const std::optional<tx_output> &returned,
            std::optional<uint64_t> declared, uint64_t fee, uint64_t percentage) const
        {
            if (!count || coin.high)
                throw error("empty or unrepresentable collateral balance");
            const uint64_t refund = returned ? returned->coin : 0;
            if (refund > coin.low)
                throw error("collateral return exceeds collateral inputs");
            const auto collected = coin.low - refund;
            if (!matches_returned_assets(returned))
                throw error("net collateral must be ADA only");
            if (declared && *declared != collected)
                throw error("total collateral does not match its inputs and return");
            // Products fit 128 bits, including hostile uint64_t parameter/fee values.
            using boost::multiprecision::uint128_t;
            if (uint128_t { collected } * 100 < uint128_t { fee } * percentage)
                throw error("insufficient collateral percentage");
            return collected;
        }

    private:
        bool matches_returned_assets(const std::optional<tx_output> &returned) const
        {
            if (!returned)
                return assets.empty();
            // Both maps are ordered. Compare the exact token balance directly,
            // without allocating a second widened map for the return output.
            auto input_policy = assets.begin();
            for (const auto &[policy, entries]: returned->assets) {
                if (entries.empty())
                    continue;
                if (input_policy == assets.end() || input_policy->first != policy
                        || input_policy->second.size() != entries.size())
                    return false;
                auto input_asset = input_policy->second.begin();
                for (const auto &[name, quantity]: entries) {
                    if (input_asset->first != name || input_asset->second != amount_sum { quantity })
                        return false;
                    ++input_asset;
                }
                ++input_policy;
            }
            return input_policy == assets.end();
        }
    };
}
