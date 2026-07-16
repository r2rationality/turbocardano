#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <map>
#include <string>
#include <turbo/common/error.hpp>
#include <turbo/json.hpp>

namespace turbo::indexer::merger {
    static constexpr uint64_t part_size = static_cast<uint64_t>(1) << 33;

    struct slice {
        uint64_t offset = 0;
        uint64_t size = 0;
        uint64_t max_slot = 0;
        std::string slice_id {};

        constexpr static auto serialize(auto &archive, auto &self)
        {
            return archive(self.offset, self.size, self.slice_id);
        }

        static slice from_json(const json::object &j)
        {
            auto prefix = static_cast<std::string_view>(j.at("prefix").as_string());
            return slice {
                json::value_to<uint64_t>(j.at("offset")), json::value_to<uint64_t>(j.at("size")),
                json::value_to<uint64_t>(j.at("maxSlot")), prefix
            };
        }

        slice()
        {
        }

        slice(const uint64_t offset_, const uint64_t size_, const uint64_t max_slot_, const std::string_view &prefix="slice")
            : offset { offset_ }, size { size_ }, max_slot { max_slot_ }, slice_id { fmt::format("{}-{:013}-{:013}", prefix, offset, end_offset()) }
        {
        }

        json::object to_json() const
        {
            return json::object {
                { "offset", offset },
                { "size", size },
                { "maxSlot", max_slot },
                { "prefix", slice_id.substr(0, slice_id.find('-')) }
            };
        }

        uint64_t end_offset() const
        {
            return offset + size;
        }

        bool operator<(const auto &b) const
        {
            return end_offset() < b.end_offset();
        }
    };

    struct tree: std::map<uint64_t, slice> {
        void add(const slice &s, bool allow_same=false)
        {
            auto [it, created] = try_emplace(s.offset, s);
            if (!created) {
                if (!allow_same && it->second.size != s.size)
                    throw error(fmt::format("internal error: a duplicate slice with offset {}!", it->first));
            }
        }

        uint64_t continuous_size() const
        {
            uint64_t size = 0;
            for (const auto &[offset, info]: *this) {
                if (offset == size)
                    size += info.size;
            }
            return size;
        }

        uint64_t continuous_max_slot() const
        {
            uint64_t slot = 0;
            uint64_t size = 0;
            for (const auto &[offset, info]: *this) {
                if (offset == size) {
                    slot = info.max_slot;
                    size += info.size;
                }
            }
            return slot;
        }
    };
}