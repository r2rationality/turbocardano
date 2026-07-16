#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/chunk-registry-fwd.hpp>
#include <turbo/storage/chunk-info.hpp>

namespace turbo::storage {
    struct block_distance_t {
        ptrdiff_t blocks = 0;
        ptrdiff_t ebb_blocks = 0;

        ptrdiff_t height() const
        {
            return blocks - ebb_blocks;
        }

        std::strong_ordering operator<=>(const block_distance_t& o) const
        {
            if (const auto cmp = blocks <=> o.blocks; cmp != std::strong_ordering::equal)
                return cmp;
            return ebb_blocks <=> o.ebb_blocks;
        }

        bool operator==(const block_distance_t &o) const
        {
            return *this <=> o == std::strong_ordering::equal;
        }
    };

    struct const_iterator {
        using difference_type = std::ptrdiff_t;

        static std::string rel_path(const std::filesystem::path &db_dir, const std::filesystem::path &full_path);
        static std::string full_path(const std::filesystem::path &db_dir, const std::filesystem::path &rel_path);
        static const_iterator cbegin(const chunk_registry &cr, const chunk_map &chunks);
        static const_iterator cend(const chunk_registry &cr, const chunk_map &chunks);
        static block_distance_t block_distance(const const_iterator &first_it, const const_iterator &last_it);

        const_iterator() =delete;

        const_iterator(const const_iterator &o) noexcept;
        const_iterator &operator=(const const_iterator &o);

        bool operator<(const const_iterator &o) const;
        bool operator==(const const_iterator &o) const;
        const block_info &operator*() const;
        const block_info *operator->() const;

        bool is_ebb() const;
        cardano::parsed_header header() const;
        uint8_vector block_data() const;
        std::pair<uint8_vector, const_iterator> chunk_remaining_data(const_iterator last_it) const;

        const_iterator &operator--();
        const_iterator &operator++();
        difference_type operator-(const const_iterator &o) const;
        const_iterator operator+(difference_type n) const;
        const_iterator operator-(difference_type n) const;
    private:
        friend chunk_registry;

        struct chunk_cache {
            std::string full_path;
            uint8_vector data;
        };

        const chunk_registry &_cr;
        // one chunk registry can have alternative chunk sets: proposed and previous versions upon a fork
        const chunk_map &_chunks;
        chunk_map::const_iterator _chunk_it;
        size_t _block_no = 0;
        mutable std::optional<chunk_cache> _chunk_cache {};

        const_iterator(const chunk_registry &cr, const chunk_map &chunks, const chunk_map::const_iterator &chunk_it, size_t block_no) noexcept;
        buffer _prep_chunk_cache() const;
    };
}
