#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <compare>

namespace turbo::cardano::shelley {
    struct nint64_t {
        // Raw CBOR argument: the mathematical value is -1 - raw.
        uint64_t raw;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.raw);
        }

        constexpr bool operator==(const nint64_t &) const = default;

        constexpr auto operator<=>(const nint64_t &o) const
        {
            return o.raw <=> raw;
        }
    };

    struct metadatum_t {
        using array_t = std::vector<metadatum_t>;
        // Metadata maps preserve their CBOR order and may contain duplicate keys.
        using map_t = std::vector<std::pair<metadatum_t, metadatum_t>>;
        using value_type = std::variant<nint64_t, uint64_t, uint8_vector, std::string, array_t, map_t>;

        value_type value;

        static metadatum_t from_cbor(cbor::zero2::value &);
        void to_cbor(era_encoder &) const;

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
        void to_cbor(era_encoder &) const;

        static constexpr auto serialize(auto &archive, auto &self)
        {
            return archive(self.dict);
        }
    };
}
