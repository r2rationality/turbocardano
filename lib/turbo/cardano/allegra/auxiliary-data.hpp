#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/shelley/metadata.hpp>

namespace turbo::cardano::allegra {
    using shelley::metadata_t;

    struct auxiliary_data_array_t {
        metadata_t transaction_metadata {};
        std::vector<script_info> auxiliary_scripts {};

        static auxiliary_data_array_t from_cbor(cbor::zero2::value &);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.transaction_metadata, self.auxiliary_scripts);
        }
    };

    struct auxiliary_data_t {
        using value_type = std::variant<metadata_t, auxiliary_data_array_t>;

        value_type value;

        static auxiliary_data_t from_cbor(cbor::zero2::value &);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.value);
        }
    };

    using auxiliary_data_dict_base_t = flat_map<uint64_t, auxiliary_data_t>;

    struct auxiliary_data_dict_t: auxiliary_data_dict_base_t {
        using base_type = auxiliary_data_dict_base_t;
        using base_type::base_type;

        buffer raw {};

        static auxiliary_data_dict_t from_cbor(cbor::zero2::value &);
    };
}
