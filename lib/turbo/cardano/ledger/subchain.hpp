#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <functional>
#include <map>
#include <turbo/cardano/common/types.hpp>
#include <turbo/common/error.hpp>

namespace turbo::cardano::ledger {
    struct subchain {
        size_t offset = 0;
        size_t num_bytes = 0;
        size_t num_blocks = 0;
        size_t valid_blocks = 0;
        size_t first_block_slot = 0;
        block_hash first_block_hash {};
        size_t last_block_slot = 0;
        block_hash last_block_hash {};

        void merge(const subchain &right)
        {
            right.check_coherency();
            if (end_offset() != right.offset) [[unlikely]]
                throw error(fmt::format("unmergeable sub-chains: left end: {} right start: {}", end_offset(), right.offset));
            if (right.first_block_slot < last_block_slot) [[unlikely]]
                throw error(fmt::format("unmergeable sub-chains: left last_block_slot: {} right first_block_slot: {}", last_block_slot, right.first_block_slot));
            num_bytes += right.num_bytes;
            num_blocks += right.num_blocks;
            valid_blocks += right.valid_blocks;
            last_block_slot = right.last_block_slot;
            last_block_hash = right.last_block_hash;
            check_coherency();
        }

        void check_coherency() const
        {
            if (num_bytes == 0) [[unlikely]]
                throw error("a sub-chain must contain data!");
            if (num_blocks == 0) [[unlikely]]
                throw error("a sub-chain must contain blocks!");
            if (first_block_slot > last_block_slot) [[unlikely]]
                throw error(fmt::format("a sub-chain must contain monotonically increasing slots but got {} > {}!", first_block_slot, last_block_slot));
            if (num_blocks == 1) {
                if (first_block_hash != last_block_hash) [[unlikely]]
                    throw error(fmt::format("a single block sub-chain must have the same first and last hashes but got: {} and {}!", first_block_hash, last_block_hash));
                if (first_block_slot != last_block_slot) [[unlikely]]
                    throw error(fmt::format("a single block sub-chain must have the same first and last slots but got: {} and {}!", first_block_slot, last_block_slot));
            } else {
                if (first_block_hash == last_block_hash) [[unlikely]]
                    throw error(fmt::format("a multi-block sub-chain must have different first and last hashes but got: {} and {}!", first_block_hash, last_block_hash));
                // it is theoretically possible for two blocks to have the same slot, so no need to check for the slots
            }
        }

        size_t end_offset() const
        {
            return offset + num_bytes;
        }

        bool operator==(const auto &o) const
        {
            return offset == o.offset && num_bytes == o.num_bytes && num_blocks == o.num_blocks
                && valid_blocks == o.valid_blocks && first_block_slot == o.first_block_slot && last_block_slot == o.last_block_slot
                && first_block_hash == o.first_block_hash && last_block_hash == o.last_block_hash;
        }

        bool operator<(const auto &v) const
        {
            return offset < v.offset;
        }

        explicit operator bool() const
        {
            bool res = num_bytes > 0;
            res &= num_blocks > 0;
            res &= num_blocks == valid_blocks;
            return res;
        }
    };

    struct subchain_list: std::map<uint64_t, subchain> {
        void add(subchain &&sc)
        {
            sc.check_coherency();
            const auto last_byte_offset = sc.offset + sc.num_bytes - 1;
            if (auto it = lower_bound(sc.offset); it != end() && it->second.offset <= last_byte_offset) [[unlikely]]
                throw fmt_error(std::source_location::current(), "intersecting subchains: existing: {} new: {}", it->second, sc);
            auto [it, created] = emplace(last_byte_offset, std::move(sc));
            if (!created) [[unlikely]]
                throw fmt_error(std::source_location::current(), "a duplicate subchain: existing: {} new: {}", it->second, sc);
            if (it->second)
                merge_valid();
        }

        void add(const subchain &sc)
        {
            add(subchain { sc });
        }

        bool truncate(const uint64_t max_end_offset)
        {
            bool changed = false;
            for (auto it = begin(); it != end(); ) {
                const auto sc_end_offset = it->second.end_offset();
                if (it->second.offset >= max_end_offset || (sc_end_offset > max_end_offset && !it->second)) {
                    it = erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
            return changed;
        }

        iterator find(uint64_t offset)
        {
            auto it = lower_bound(offset);
            if (it == end() || !(it->second.offset <= offset && it->first >= offset)) [[unlikely]]
                throw error(fmt::format("internal error: can't find a sub-chain for the blockchain offset {}", offset));
            return it;
        }

        uint64_t valid_size() const
        {
            if (!empty() && begin()->second && begin()->second.offset == 0) [[likely]]
                return begin()->second.num_bytes;
            return 0;
        }

        optional_point max_valid_point() const
        {
            if (const auto it = begin(); it != end() && it->second && it->second.offset == 0) [[likely]]
                return point { it->second.last_block_hash, it->second.last_block_slot, it->second.num_blocks, it->second.num_bytes };
            return {};
        }

        void merge_valid()
        {
            if (!empty() && begin()->second) {
                auto it = begin();
                for (auto next_it = std::next(it);
                    next_it != end() && it->second.end_offset() == next_it->second.offset && next_it->second;
                    next_it = erase(next_it))
                {
                    it->second.merge(next_it->second);
                }
                _adjust_updated_subchain(it);
            }
        }

        void merge_same_epoch(const cardano::config &cfg)
        {
            for (auto it = begin(); it != end(); ) {
                auto next_it = std::next(it);
                while (next_it != end() && it->second.end_offset() == next_it->second.offset) {
                    const auto first_block_slot = cardano::slot { it->second.first_block_slot, cfg };
                    const auto last_block_slot = cardano::slot { it->second.last_block_slot, cfg };
                    if (!it->second && first_block_slot.epoch() != last_block_slot.epoch()) [[unlikely]]
                        throw error(fmt::format("an unvalidated subchain at [{}:{})) has slots from different epochs: first slot: {} last_slot: {}",
                            it->second.offset, it->second.end_offset(), first_block_slot, last_block_slot));
                    const auto next_first_block_slot = cardano::slot { next_it->second.first_block_slot, cfg };
                    const auto next_last_block_slot = cardano::slot { next_it->second.last_block_slot, cfg };
                    if (!next_it->second && next_first_block_slot.epoch() != next_last_block_slot.epoch()) [[unlikely]]
                        throw error(fmt::format("an unvalidated subchain at [{}:{}) has slots from different epochs: first slot: {} last_slot: {}",
                            next_it->second.offset, next_it->second.end_offset(), next_first_block_slot, next_last_block_slot));
                    if (static_cast<bool>(it->second) != static_cast<bool>(next_it->second))
                        break;
                    if (last_block_slot.epoch() != next_first_block_slot.epoch())
                        break;
                    it->second.merge(next_it->second);
                    next_it = erase(next_it);
                }
                _adjust_updated_subchain(it);
                it = next_it;
            }
        }

        optional_point report_valid_blocks(const uint64_t chunk_offset, const size_t num_valid_blocks)
        {
            auto sc_it = find(chunk_offset);
            sc_it->second.valid_blocks += num_valid_blocks;
            if (sc_it->second) {
                merge_valid();
                return max_valid_point();
            }
            return {};
        }
    private:
        std::function<void(const subchain &)> _take_snapshot {};

        void _adjust_updated_subchain(iterator it)
        {
            uint64_t last_offset = it->second.end_offset() - 1;
            if (it->first != last_offset) {
                auto node = extract(it);
                node.key() = last_offset;
                auto [new_it, created, nt] = insert(std::move(node));
                if (!created) [[unlikely]]
                    throw error(fmt::format("duplicate subchain found with last_offset {}", last_offset));
            }
        }
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::cardano::ledger::subchain>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::cardano::ledger::subchain &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "offset: {} size: {} blocks: {} valid_blocks: {} first_block_slot: {} first_block_hash: {} last_block_slot: {} last_block_hash: {}",
                v.offset, v.num_bytes, v.num_blocks, v.valid_blocks, v.first_block_slot, v.first_block_hash, v.last_block_slot, v.last_block_hash);
        }
    };

    template<>
    struct formatter<turbo::cardano::ledger::subchain_list>: formatter<uint64_t> {
        template<typename FormatContext>
        auto format(const turbo::cardano::ledger::subchain_list &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "{}", static_cast<const std::map<uint64_t, turbo::cardano::ledger::subchain> &>(v));
        }
    };
}
