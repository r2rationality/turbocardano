#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>

namespace turbo::cardano::shelley {
    typedef uint64_t nint64_t;

    struct metadatum_t {
        using array_t = std::vector<metadatum_t>;
        using map_t = flat_map<metadatum_t, metadatum_t>;
        using value_type = std::variant<int64_t, nint64_t, uint8_vector, std::string, array_t, map_t>;

        value_type value;

        static metadatum_t from_cbor(cbor::zero2::value &);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.value);
        }

        metadatum_t() = delete;
        metadatum_t(metadatum_t &&) = default;
        metadatum_t(const metadatum_t &) = default;
        metadatum_t(value_type &&v): value(std::move(v))
        {
        }

        metadatum_t &operator=(metadatum_t &&) = default;
        metadatum_t &operator=(const metadatum_t &) = default;

        bool operator<(const metadatum_t &o) const
        {
            if (value.index() != o.value.index())
                return value.index() < o.value.index();
            return value < o.value;
        }
    };

    using metadatum_label_t = uint64_t;

    struct metadata_t {
        flat_map<metadatum_label_t, metadatum_t> dict {};

        static metadata_t from_cbor(cbor::zero2::value &);

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.dict);
        }
    };
}
