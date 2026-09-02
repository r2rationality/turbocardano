#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/auxiliary-data.hpp>

namespace turbo::cardano::dijkstra {
    using conway::auxiliary_data_array_t;
    using conway::metadata_t;

    struct auxiliary_data_map_t: conway::auxiliary_data_map_t {
        std::vector<script_info> plutus_v4_scripts {};

        static auxiliary_data_map_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(
                self.metadata,
                self.native_scripts,
                self.plutus_v1_scripts,
                self.plutus_v2_scripts,
                self.plutus_v3_scripts,
                self.plutus_v4_scripts
            );
        }
    };

    struct auxiliary_data_t {
        using value_type = std::variant<metadata_t, auxiliary_data_array_t, auxiliary_data_map_t>;

        value_type value;

        static auxiliary_data_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.value);
        }
    };
}
