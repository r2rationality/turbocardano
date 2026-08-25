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
                    switch (coin_v.type()) {
                        case cbor::major_type::uint: {
                            const auto amount = numeric_cast<int64_t>(coin_v.uint());
                            if (amount != 0)
                                p_m.emplace_hint(p_m.end(), name_bytes, amount);
                            else if (on_invalid) [[unlikely]]
                                on_invalid(invalid_mint::zero_amount);
                            break;
                        }
                        case cbor::major_type::nint: p_m.emplace_hint(p_m.end(), name_bytes, -numeric_cast<int64_t>(coin_v.nint())); break;
                        [[unlikely]] default: throw error(fmt::format("expecting an int but got {}", coin_v.type()));
                    }
                }
                if (!p_m.empty()) [[likely]]
                    m.emplace_hint(m.end(), policy_id_bytes, std::move(p_m));
                else if (on_invalid) [[unlikely]]
                    on_invalid(invalid_mint::empty_policy);
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
