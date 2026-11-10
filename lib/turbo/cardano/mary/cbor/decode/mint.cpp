/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/mary/block.hpp>
#include <turbo/cardano/mary/cbor/decode/mint.hpp>

namespace turbo::cardano::mary {
    namespace detail {
        void decode_mint(cbor::zero2::value &mints_raw, multi_mint_map &m, const invalid_mint_observer on_invalid)
        {
            if (!mints_raw.indefinite()) [[likely]]
                m.reserve(mints_raw.special_uint());
            auto &m_it = mints_raw.map();
            while (!m_it.done()) {
                auto &p_id = m_it.read_key();
                const auto policy_id_bytes = p_id.bytes();
                auto &p_mints = m_it.read_val(std::move(p_id));
                policy_mint_map p_m {};
                if (!p_mints.indefinite()) [[likely]]
                    p_m.reserve(p_mints.special_uint());
                auto &p_it = p_mints.map();
                while (!p_it.done()) {
                    auto &name_v = p_it.read_key();
                    const auto name_bytes = name_v.bytes();
                    auto &coin_v = p_it.read_val(std::move(name_v));
                    int64_t amount;
                    switch (coin_v.type()) {
                        case cbor::major_type::uint: {
                            amount = numeric_cast<int64_t>(coin_v.uint());
                            break;
                        }
                        case cbor::major_type::nint: {
                            const auto magnitude = coin_v.nint();
                            if (magnitude > uint64_t { 1 } << 63) [[unlikely]]
                                throw error("mint amount is smaller than min_int64");
                            amount = magnitude == uint64_t { 1 } << 63
                                ? std::numeric_limits<int64_t>::min()
                                : -numeric_cast<int64_t>(magnitude);
                            break;
                        }
                        [[unlikely]] default: throw error{"expecting an int but got {}", coin_v.type()};
                    }
                    // Before Conway, the last duplicate wins, including a zero that prunes an earlier value.
                    if (amount != 0) [[likely]] {
                        const auto before = p_m.size();
                        p_m.insert_or_assign(p_m.end(), asset_name_t{name_bytes}, amount);
                        if (p_m.size() == before && on_invalid) [[unlikely]]
                            on_invalid(invalid_mint::duplicate_asset_name);
                    } else {
                        if (on_invalid) [[unlikely]]
                            on_invalid(invalid_mint::zero_amount);
                        p_m.erase(asset_name_t{name_bytes});
                    }
                }
                if (!p_m.empty()) [[likely]] {
                    const auto before = m.size();
                    m.insert_or_assign(m.end(), script_hash{policy_id_bytes}, std::move(p_m));
                    if (m.size() == before && on_invalid) [[unlikely]]
                        on_invalid(invalid_mint::duplicate_policy);
                } else {
                    if (on_invalid) [[unlikely]]
                        on_invalid(invalid_mint::empty_policy);
                    m.erase(script_hash{policy_id_bytes});
                }
            }
        }
    }

    mint_t mint_t::from_cbor(cbor::zero2::value &mints_raw)
    {
        mint_t m {};
        detail::decode_mint(mints_raw, m);
        return m;
    }

}
