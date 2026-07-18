/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "const-iterator.hpp"
#include <turbo/cardano.hpp>
#include <turbo/chunk-registry.hpp>

namespace turbo::storage {

    bool const_iterator::is_ebb() const
    {
        return _block_no == 0
            && _chunk_it != _chunks.end()
            && _chunk_it->second.blocks.front().era == 0;
    }

    std::string const_iterator::rel_path(const std::filesystem::path &db_dir, const std::filesystem::path &full_path)
    {
        auto canon_path = std::filesystem::weakly_canonical(full_path);
        auto [diffBegin, diffEnd] = std::mismatch(db_dir.begin(), db_dir.end(), canon_path.begin());
        if (diffBegin != db_dir.end())
            throw error(fmt::format("the supplied path '{}' is not inside the host directory '{}'", canon_path.string(), db_dir.string()));
        return std::filesystem::relative(canon_path, db_dir).string();
    }

    std::string const_iterator::full_path(const std::filesystem::path &db_dir, const std::filesystem::path &rel_path)
    {
        auto canon_path = std::filesystem::weakly_canonical(db_dir / rel_path);
        auto [diffBegin, diffEnd] = std::mismatch(db_dir.begin(), db_dir.end(), canon_path.begin());
        if (diffBegin != db_dir.end())
            throw error(fmt::format("the supplied path '{}' does not resolve into the host directory '{}'", canon_path.string(), db_dir.string()));
        std::filesystem::create_directories(canon_path.parent_path());
        return canon_path.string();
    }

    const_iterator const_iterator::cbegin(const chunk_registry &cr, const chunk_map &chunks)
    {
        return { cr, chunks, chunks.begin(), 0 };
    }

    const_iterator const_iterator::cend(const chunk_registry &cr, const chunk_map &chunks)
    {
        return { cr, chunks, chunks.end(), 0 };
    }

    // the result is positive when first_it > last_it, such as end() - begin() == size
    block_distance_t const_iterator::block_distance(const const_iterator &last_it, const const_iterator &first_it)
    {
        // less operator on iterators throws an exception if they belong to difference chunk registries
        if (last_it < first_it) {
            const auto inv_res = block_distance(first_it, last_it);
            return { -inv_res.blocks, -inv_res.ebb_blocks };
        }
        if (last_it._chunk_it == first_it._chunk_it) {
            return {
                static_cast<difference_type>(last_it._block_no) - static_cast<difference_type>(first_it._block_no),
                first_it.is_ebb() && first_it != last_it ? 1: 0
            };
        }

        block_distance_t res {};
        res.blocks += static_cast<ptrdiff_t>(first_it._chunk_it->second.blocks.size() - first_it._block_no);
        if (first_it.is_ebb())
            ++res.ebb_blocks;
        for (auto cit = first_it._chunk_it;;) {
            ++cit;
            if (cit == last_it._chunk_it)
                break;
            // blocks cannot be empty!
            res.blocks += static_cast<ptrdiff_t>(cit->second.blocks.size());
            if (cit->second.blocks.front().era == 0)
                ++res.ebb_blocks;
        }
        res.blocks += static_cast<ptrdiff_t>(last_it._block_no);
        // _block_no can be > 0 only for non-end iterators!
        if (last_it._block_no > 0 && last_it._chunk_it->second.blocks.front().era == 0)
            ++res.ebb_blocks;
        return res;
    }

    const_iterator::const_iterator(const const_iterator &o) noexcept:
        _cr { o._cr },
        _chunks { o._chunks },
        _chunk_it { o._chunk_it },
        _block_no { o._block_no }
    {
    }

    const_iterator::const_iterator(const chunk_registry &cr, const chunk_map &chunks, const chunk_map::const_iterator &chunk_it, const size_t block_no) noexcept:
        _cr { cr },
        _chunks { chunks },
        _chunk_it { chunk_it },
        _block_no { block_no }
    {
    }

    const_iterator &const_iterator::operator=(const const_iterator &o)
    {
        if (&_chunks != &o._chunks) [[unlikely]]
            throw error(fmt::format("an attempt to assign an iterator across different instances of chunk_registry"));
        _chunk_it = o._chunk_it;
        _block_no = o._block_no;
        _chunk_cache.reset();
        return *this;
    }

    bool const_iterator::operator<(const const_iterator &o) const
    {
        if (&_chunks != &o._chunks) [[unlikely]]
            throw error(fmt::format("an attempt to use an iterator across different instances of chunk_registry"));
        if (_chunk_it == o._chunk_it)
            return _block_no < o._block_no;
        if (_chunk_it == _chunks.end())
            return false;
        if (o._chunk_it == _chunks.end())
            return true;
        return _chunk_it->second.offset < o._chunk_it->second.offset;
    }

    bool const_iterator::operator==(const const_iterator &o) const
    {
        if (&_chunks != &o._chunks) [[unlikely]]
            return false;
        if (_chunk_it != o._chunk_it)
            return false;
        if (_block_no != o._block_no)
            return false;
        return true;
    }

    const block_info &const_iterator::operator*() const
    {
        return _chunk_it->second.blocks.at(_block_no);
    }

    const block_info *const_iterator::operator->() const
    {
        return &operator*();
    }

    const_iterator &const_iterator::operator--()
    {
        if (_chunk_it != _chunks.begin() || _block_no > 0) {
            if (_block_no == 0) [[unlikely]] {
                --_chunk_it;
                _block_no = _chunk_it->second.blocks.size();
            }
            --_block_no;
        }
        return *this;
    }

    const_iterator &const_iterator::operator++()
    {
        if (_chunk_it != _chunks.end()) {
            ++_block_no;
            if (_block_no == _chunk_it->second.blocks.size()) [[likely]] {
                ++_chunk_it;
                _block_no = 0;
            }
        }
        return *this;
    }

    const_iterator::difference_type const_iterator::operator-(const const_iterator &o) const
    {
        const auto dist = block_distance(*this, o);
        return dist.blocks;
    }

    const_iterator const_iterator::operator+(difference_type n) const
    {
        if (n < 0) {
            const auto pos = *this - cbegin(_cr, _chunks);
            return cbegin(_cr, _chunks) + std::max(difference_type { 0 }, pos + n);
        }
        auto it = *this;
        while (it._chunk_it != _chunks.end() && n > 0) {
            if (n < it._chunk_it->second.blocks.size() - it._block_no)
                return { _cr, _chunks, it._chunk_it, it._block_no + n };
            n -= it._chunk_it->second.blocks.size() - it._block_no;
            ++it._chunk_it;
            it._block_no = 0;
        }
        return it;
    }

    const_iterator const_iterator::operator-(const difference_type n) const
    {
        return operator+(-n);
    }

    buffer const_iterator::_prep_chunk_cache() const
    {
        const auto rel_path = _chunk_it->second.rel_path();
        const auto path = full_path(_cr._db_dir, _chunk_it->second.rel_path());
        if (!_chunk_cache || _chunk_cache->full_path != path || _chunk_it->second.data_size != _chunk_cache->data.size()) {
            _chunk_cache.emplace(path, zstd::read(path));
        }
        return _chunk_cache->data;
    }

    cardano::parsed_header const_iterator::header() const
    {
        const auto bytes = _prep_chunk_cache();
        const auto &blk = operator*();
        // + 1 is the offset of the first entry in the block array which is the header
        const auto file_offset = blk.offset + blk.header_offset + 1 - _chunk_it->second.offset;
        return { blk.era, static_cast<buffer>(bytes).subbuf(file_offset,  blk.header_size), _cr.config() };
    }

    uint8_vector const_iterator::block_data() const
    {
        const auto bytes = _prep_chunk_cache();
        const auto &blk = operator*();
        // + 1 is the offset of the first entry in the block array which is the header
        const auto file_offset = blk.offset - _chunk_it->second.offset;
        return { bytes.subbuf(file_offset,  blk.size) };
    }

    std::pair<uint8_vector, const_iterator>
    const_iterator::chunk_remaining_data(const const_iterator last_it) const
    {
        if (*this == last_it) [[unlikely]]
            return std::make_pair(uint8_vector {}, last_it);
        if (**this == _chunk_it->second.blocks.front() && last_it._chunk_it != _chunk_it) {
            const auto path = full_path(_cr._db_dir, _chunk_it->second.rel_path());
            return std::make_pair(file::read(path), const_iterator { _cr, _chunks, std::next(_chunk_it), 0 });
        }
        const auto bytes = _prep_chunk_cache();
        const auto &blk = operator*();
        const auto file_offset = blk.offset - _chunk_it->second.offset;
        if (last_it._chunk_it == _chunk_it && last_it._block_no < _chunk_it->second.blocks.size()) {
            return std::make_pair(
                zstd::compress(bytes.subbuf(file_offset, last_it->offset - blk.offset), 3),
                last_it
            );
        }
        return std::make_pair(
            zstd::compress(bytes.subbuf(file_offset), 3),
            const_iterator { _cr, _chunks, std::next(_chunk_it), 0 }
        );
    }
}
