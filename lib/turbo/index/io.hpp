#pragma once
/* This file is part of Daedalus Turbo project: https://github.cm/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <atomic>
#include <memory>
#include <queue>
#include <turbo/common/mutex.hpp>
#include <turbo/common/zstd.hpp>
#include <turbo/crypto/blake2b.hpp>

namespace turbo::index {
    template<typename T>
    struct chunk_info {
        uint64_t file_offset = 0;
        size_t packed_size = 0;
        T max_item {};
        crypto::blake2b::hash_8 packed_hash {};
    };

    template<typename T>
    struct merge_item {
        T val;
        size_t stream_idx;

        merge_item(const merge_item &i): val { i.val }, stream_idx { i.stream_idx }
        {}

        merge_item(const T &v, size_t idx): val { v }, stream_idx { idx }
        {}

        merge_item &operator=(const merge_item &v)
        {
            val = v.val;
            stream_idx = v.stream_idx;
            return *this;
        }

        bool operator<(const merge_item& v) const noexcept
        {
            // priority queue returns the greatest element but we need the smallest!
            return !(val < v.val);
        }
    };

    template<typename T>
    struct merge_queue: std::priority_queue<merge_item<T>> {
        using std::priority_queue<merge_item<T>>::priority_queue;
    };

    static constexpr size_t max_parts = 256;
    static constexpr size_t default_parts = 16;

    // Each partition can be written only from a single thread to minimize cross-thread synchronization
    template<typename T>
    struct writer {
        static constexpr size_t default_chunk_size = 0x1000;

        static uint64_t disk_size(const std::string &path)
        {
            if (!std::filesystem::exists(path))
                return 0;
            return std::filesystem::file_size(path);
        }

        writer() =delete;
        writer(const writer<T> &) =delete;

        writer(const std::string &path, size_t num_partitions = 1, size_t chunk_size=default_chunk_size)
            : _parts(num_partitions), _bufs(num_partitions), _cnts(num_partitions),
                _path { path },
                _num_parts { num_partitions }, _chunk_size { chunk_size },
                _os { _path + ".tmp" }
        {
            if (_num_parts > max_parts)
                throw error(fmt::format("num_partitions: {} is greater than the pre-configured maximum: {}!", _num_parts, max_parts));
            for (auto &buf: _bufs)
                buf.resize(chunk_size);
        }

        writer(writer<T> &&w)
            : _parts { std::move(w._parts) }, _bufs { std::move(w._bufs) }, _cnts { std::move(w._cnts) },
                _path { std::move(w._path) },
                _num_parts { w._num_parts }, _chunk_size { w._chunk_size },
                _commited { (bool)w._commited },_os { std::move(w._os) }, _free_off { (size_t)w._free_off }
        {
        }

        ~writer()
        {
            if (!_commited)
                _commit();
        }

        void commit()
        {
            _commit();
        }

        template<typename ...A>
        const T &emplace_part(size_t part_id, A &&...args)
        {
            if (_commited)
                throw error(fmt::format("writer::emplace_part {} has already been commited!", _path));
            auto &cnt = _cnts.at(part_id);
            auto &buf = _bufs.at(part_id);
            T &item = buf.at(cnt % _chunk_size);
            new (&item) T { std::forward<A>(args)... };
            cnt++;
            if (cnt % _chunk_size == 0)
                _flush_part(part_id);
            return item;
        }

        template<typename ...A>
        const T &emplace(A &&...args)
        {
            return emplace_part(0, std::forward<A>(args)...);
        }

        const std::string &path() const
        {
            return _path;
        }

        void rename(const std::string &new_path) const
        {
            std::filesystem::rename(_path, new_path);
        }

        void remove() const
        {
            remove(_path);
        }

        void set_meta(const std::string &name, const buffer &data)
        {
            mutex::scoped_lock lock { _write_mutex };
            if (_commited)
                throw error(fmt::format("set_meta called for {} after it has been commited!", _path));
            if (name.size() > 255)
                throw error(fmt::format("name of metadata item is too long: {}!", name.size()));
            if (data.size() > 255)
                throw error(fmt::format("size of the metadata item is too big: {}!", data.size()));
            _meta[name] = data;
        }

        uint64_t size() const
        {
            return _free_off;
        }
    protected:
        std::vector<std::vector<chunk_info<T>>> _parts;
        std::vector<std::vector<T>> _bufs;
        std::vector<size_t> _cnts;
        std::map<std::string, uint8_vector> _meta {};
        std::string _path;
        size_t _num_parts, _chunk_size;

        std::atomic_bool _commited = false;
        mutex::unique_lock::mutex_type _write_mutex alignas(mutex::alignment) {};
        file::write_stream _os;
        size_t _free_off = 0;

        void _commit()
        {
            if (_commited)
                throw error(fmt::format("writer::commit {} has already been commited!", _path));
            {
                mutex::scoped_lock lock { _write_mutex };
                _commited = true;
            }
            // flush takes _write_mutex so need to release it first
            for (size_t i = 0; i < _num_parts; ++i)
                _flush_part(i);
            mutex::scoped_lock lock { _write_mutex };
            uint64_t meta_off = _os.tellp();
            if (_meta.size() > 255)
                throw error(fmt::format("internal error: only up to 255 meta items are supported but got: {}", _meta.size()));
            const uint8_t meta_size = _meta.size();
            uint8_vector meta_buf {};
            meta_buf << buffer::from(_num_parts)
                << buffer::from(_chunk_size)
                << buffer::from(meta_size);
            for (const auto &[name, data]: _meta) {
                if (name.size() > 255)
                    throw error(fmt::format("internal error: metadata name must not exceed 255 bytes but got {}", name.size()));
                if (data.size() > 255)
                    throw error(fmt::format("internal error: metadata value must not exceed 255 bytes but got {}", data.size()));
                uint8_t name_size = name.size();
                uint8_t data_size = data.size();
                meta_buf << buffer::from(name_size)
                    << buffer { name }
                    << buffer::from(data_size)
                    << data;
            }
            meta_buf << buffer { reinterpret_cast<const uint8_t *>(_cnts.data()), sizeof(_cnts[0]) * _cnts.size() };
            for (const auto &chunk_list: _parts) {
                if (!chunk_list.empty())
                    meta_buf << buffer { reinterpret_cast<const uint8_t *>(chunk_list.data()), sizeof(chunk_list[0]) * chunk_list.size() };
            }
            _os.write(meta_buf.data(), meta_buf.size());
            auto meta_hash = crypto::blake2b::digest<crypto::blake2b::hash_8>(meta_buf);
            _os.write(&meta_hash, sizeof(meta_hash));
            _os.write(&meta_off, sizeof(meta_off));
            _free_off = _os.tellp();
            _os.close();
            std::filesystem::rename(_path + ".tmp", _path);
        }

        void _flush_part(const size_t part_id)
        {
            auto &cnt = _cnts.at(part_id);
            auto &part = _parts.at(part_id);
            size_t cnt_flushed = part.size() * _chunk_size;
            if (cnt > cnt_flushed) {
                size_t cnt_todo = cnt - cnt_flushed;
                if (!_commited && cnt_todo != _chunk_size)
                    throw error("internal_error: only the final chunk may have a size less than chunk_size constant!");
                auto &buf = _bufs.at(part_id);
                if (part.size() > 0 && buf.at(cnt_todo - 1) < part.back().max_item)
                    throw error(fmt::format("{} partition-{} chunks {} and {} are not ordered!", _path, part_id, part.size() - 1, part.size()));
                const buffer data { reinterpret_cast<uint8_t *>(buf.data()), cnt_todo * sizeof(T) };
                thread_local uint8_vector comp_data {};
                zstd::compress(comp_data, data, 3);

                mutex::scoped_lock lock { _write_mutex };
                size_t fact_off = _os.tellp();
                if (fact_off != _free_off)
                    throw error(fmt::format("internal error with {}: expected file position {} but got {}", _path, (size_t)_free_off, fact_off));
                auto packed_hash = crypto::blake2b::digest<crypto::blake2b::hash_8>(comp_data);
                part.emplace_back(_free_off, comp_data.size(), buf.at(cnt_todo - 1), packed_hash);
                _free_off += comp_data.size();
                _os.write(comp_data.data(), comp_data.size());
            }
        }
    };

    template<class T>
    struct sorting_writer {
        sorting_writer(const std::string &path, size_t num_partitions = 1)
            : _bufs(num_partitions), _writer { path, num_partitions }
        {
            for (auto &buf: _bufs)
                buf.reserve(1024);
        }

        ~sorting_writer()
        {
            if (!_commited) _commit();
        }

        template<typename ...A>
        const T &emplace_part(size_t part_idx, A &&...args)
        {
            if (_commited)
                throw error(fmt::format("sorting-writer::emplace_part {} has already been commited!", _writer.path()));
            auto &buf = _bufs.at(part_idx);
            auto &item = buf.emplace_back(std::forward<A>(args)...);
            return item;
        }

        template<typename ...A>
        const T &emplace(A &&...args)
        {
            return emplace_part(0, std::forward<A>(args)...);
        }

        void set_meta(const std::string &name, const buffer &data)
        {
            _writer.set_meta(name, data);
        }

    private:
        std::vector<std::vector<T>> _bufs;
        writer<T> _writer;
        bool _commited = false;

        void _commit()
        {
            _commited = true;
            for (size_t pi = 0; pi < _bufs.size(); pi++) {
                auto &buf = _bufs.at(pi);
                std::sort(buf.begin(), buf.end());
                for (auto &&item: buf)
                    _writer.emplace_part(pi, item);
            }
            _writer.commit();
        }
    };

    template<class T>
    struct reader_mt {
        using find_result = std::tuple<size_t, T>;

        struct thread_data {
            std::vector<size_t> offsets {};
            std::vector<size_t> cache_chunk_idxs {};
            std::vector<std::vector<T>> caches {};
            uint8_vector read_buf {};
            size_t num_reads = 0;
            size_t next_part_idx = 0;
            size_t single_part_idx = max_parts;
        };

        reader_mt(const std::string &path)
            : _path { path }, _is { _path }
        {
            auto data_size = std::filesystem::file_size(_path);
            if (data_size < sizeof(uint64_t))
                throw error(fmt::format("{} is too small - no metadata can be found", _path));
            _is.seek(data_size - sizeof(uint64_t));
            uint64_t meta_off;
            _is.read(&meta_off, sizeof(meta_off));
            _is.seek(meta_off);
            crypto::blake2b::hash_8 meta_hash {};
            uint64_t meta_buf_size = data_size - meta_off - sizeof(meta_off) - sizeof(meta_hash);
            uint8_vector meta_buf {};
            meta_buf.resize(meta_buf_size);
            _is.read(meta_buf.data(), meta_buf_size);
            _is.read(&meta_hash, sizeof(meta_hash));
            auto meta_hash_computed = crypto::blake2b::digest<crypto::blake2b::hash_8>(meta_buf);
            if (meta_hash_computed != meta_hash)
                throw error(fmt::format("{}: metadata hash mismatch computed: {} vs stored: {}", _path, meta_hash_computed, meta_hash));
            _is.seek(meta_off);
            _is.read(&_num_parts, sizeof(_num_parts));
            if (_num_parts == 0)
                throw error(fmt::format("num_partitions is {} for {}!", _num_parts, _path));
            if (_num_parts > max_parts)
                throw error(fmt::format("num_partitions: {} is greater than the preconfigured maximum: {}!", _num_parts, max_parts));
            _is.read(&_chunk_size, sizeof(_chunk_size));
            uint8_t meta_cnt;
            _is.read(&meta_cnt, sizeof(meta_cnt));
            while (meta_cnt > 0) {
                uint8_t name_size;
                _is.read(&name_size, sizeof(name_size));
                std::string name {};
                name.resize(name_size);
                _is.read(name.data(), name_size);
                uint8_t data_size;
                _is.read(&data_size, sizeof(data_size));
                uint8_vector data {};
                data.resize(data_size);
                _is.read(data.data(), data_size);
                _meta[name] = std::move(data);
                meta_cnt--;
            }
            _cnts.resize(_num_parts);
            _is.read(_cnts.data(), sizeof(_cnts[0]) * _num_parts);
            _chunk_lists.resize(_num_parts);
            _max_items.resize(_num_parts);
            for (size_t p = 0; p < _num_parts; ++p) {
                size_t chunk_cnt = _cnts.at(p);
                if (chunk_cnt > 0) {
                    size_t list_size = (chunk_cnt + _chunk_size - 1) / _chunk_size;
                    auto &chunk_list = _chunk_lists.at(p);
                    chunk_list.resize(list_size);
                    _is.read(chunk_list.data(), sizeof(chunk_list[0]) * list_size);
                    for (size_t ci = 1; ci < chunk_list.size(); ci++) {
                        if (chunk_list.at(ci).max_item < chunk_list.at(ci - 1).max_item)
                            throw error(fmt::format("index {}: partition-{} chunks {} and {} are not ordered!", _path, p, ci - 1, ci));
                    }
                    _max_items.at(p) = chunk_list.back().max_item;
                } else if (p > 0) {
                    _max_items.at(p) = _max_items.at(p - 1);
                }
            }
            for (size_t pi = 1; pi < _num_parts; ++pi) {
                if (_max_items.at(pi) < _max_items.at(pi - 1))
                    throw error(fmt::format("index {}: partitions {} and {} are not ordered!", _path, pi - 1, pi));
            }
        }

        thread_data init_thread(size_t part_idx=max_parts) const
        {
            thread_data t {};
            if (part_idx == max_parts) {
                t.offsets.resize(_num_parts);
                t.cache_chunk_idxs.resize(_num_parts);
                t.caches.resize(_num_parts);
            } else {
                t.single_part_idx = part_idx;
                t.offsets.resize(1);
                t.cache_chunk_idxs.resize(1);
                t.caches.resize(1);
            }
            return t;
        }

        bool eof_part(size_t part_idx, thread_data &t) const
        {
            size_t t_part_idx = _thread_part_idx(part_idx, t);
            const auto &cnt = _cnts.at(part_idx);
            const auto &off = t.offsets.at(t_part_idx);
            return off >= cnt;
        }

        bool eof(thread_data &t) const
        {
            for (size_t p = 0; p < _num_parts; ++p) {
                if (eof_part(p, t)) return true;
            }
            return false;
        }

        find_result find(const T &search_item, thread_data &t) const
        {
            size_t match_size = 0;
            T match_item {};
            bool multi_match = false;
            auto comp = [](const T &a, const T &b) { return a.index_less(b); };
            auto part_it = std::lower_bound(_max_items.begin(), _max_items.end(), search_item, comp);
            if (part_it != _max_items.end()) {
                size_t part_idx = part_it - _max_items.begin();
                size_t t_part_idx = _thread_part_idx(part_idx, t);
                auto &chunk_list = _chunk_lists.at(part_idx);
                auto &cache_chunk_idx = t.cache_chunk_idxs.at(t_part_idx);
                auto chunk_it = std::lower_bound(chunk_list.begin(), chunk_list.end(), search_item,
                                                    [](const chunk_info<T> &el, const T &val) { return el.max_item.index_less(val); });
                auto chunk_it_end = std::upper_bound(chunk_list.begin(), chunk_list.end(), search_item,
                                                    [](const T &val, const chunk_info<T> &el) { return val.index_less(el.max_item); });
                if (chunk_it != chunk_list.end()) {
                    size_t new_cache_chunk_idx = chunk_it - chunk_list.begin();
                    auto &cache = t.caches.at(t_part_idx);
                    if (new_cache_chunk_idx != cache_chunk_idx || cache.size() == 0)
                        _load_cache(part_idx, new_cache_chunk_idx, t);
                    auto [it, it_end] = std::equal_range(cache.begin(), cache.end(), search_item, comp);
                    if (it != it_end && *it == search_item) {
                        match_item = *it;
                        match_size = it_end - it;
                        if (it_end == cache.end()) multi_match = true;
                        // set the position for a potential call to next()
                        t.offsets.at(t_part_idx) = cache_chunk_idx * _chunk_size + (it - cache.begin()) + 1;
                    }
                }
                if (match_size > 0) {
                    if (multi_match) {
                        match_size += (chunk_it_end - chunk_it - 1) * _chunk_size;
                        if (chunk_it_end != chunk_list.end()) {
                            size_t new_cache_chunk_idx = chunk_it_end - chunk_list.begin();
                            auto &cache = t.caches.at(t_part_idx);
                            if (new_cache_chunk_idx != cache_chunk_idx || cache.size() == 0)
                                _load_cache(part_idx, new_cache_chunk_idx, t);
                            auto it = std::upper_bound(cache.begin(), cache.end(), search_item, comp);
                            match_size += it - cache.begin();
                        }
                    }
                    t.next_part_idx = part_idx;
                }
            }
            return find_result { match_size, match_item };
        }

        buffer get_meta(const std::string &name) const
        {
            auto it = _meta.find(name);
            if (it == _meta.end())
                throw error(fmt::format("unknown metadata item: {}!", name));
            return it->second;
        }

        size_t num_parts() const
        {
            return _num_parts;
        }

        size_t offset_part(size_t part_idx, thread_data &t) const
        {
            size_t t_part_idx = _thread_part_idx(part_idx, t);
            return t.offsets.at(t_part_idx);
        }

        const std::string &path() const
        {
            return _path;
        }

        void seek_part(size_t part_idx, size_t new_offset, thread_data &t) const
        {
            size_t t_part_idx = _thread_part_idx(part_idx, t);
            const auto &cnt = _cnts.at(part_idx);
            auto &offset = t.offsets.at(t_part_idx);
            if (new_offset >= cnt)
                throw error(fmt::format("offset is larger than the number of available elements: {} >= {}", new_offset, cnt));
            offset = new_offset;
        }

        void seek(size_t new_offset, thread_data &t) const
        {
            seek_part(0, new_offset, t);
        }

        size_t size_part(size_t part_idx) const
        {
            return _cnts.at(part_idx);
        }

        size_t size() const
        {
            size_t tot_size = 0;
            for (size_t p = 0; p < _num_parts; ++p)
                tot_size += size_part(p);
            return tot_size;
        }

        bool read_part(size_t part_idx, T& item, thread_data &t) const
        {
            size_t t_part_idx = _thread_part_idx(part_idx, t);
            size_t &off = t.offsets.at(t_part_idx);
            size_t cnt = _cnts.at(part_idx);
            if (off >= cnt) return false;
            size_t new_cache_chunk_idx = off / _chunk_size;
            auto &cache_chunk_idx = t.cache_chunk_idxs.at(t_part_idx);
            auto &cache = t.caches.at(t_part_idx);
            if (new_cache_chunk_idx != cache_chunk_idx || cache.size() == 0)
                _load_cache(part_idx, new_cache_chunk_idx, t);
            item = cache.at(off - new_cache_chunk_idx * _chunk_size);
            off++;
            return true;
        }

        bool read(T &item, thread_data &t) const
        {
            for (; t.next_part_idx < _num_parts; t.next_part_idx++) {
                if (read_part(t.next_part_idx, item, t)) return true;
            }
            return false;
        }

        void close()
        {
            mutex::scoped_lock lock { _read_mutex };
            _is.close();
        }
    private:
        std::string _path;
        size_t _num_parts = 0;
        size_t _chunk_size = 0;
        std::map<std::string, uint8_vector> _meta {};
        std::vector<std::vector<chunk_info<T>>> _chunk_lists {};
        std::vector<size_t> _cnts {};
        std::vector<T> _max_items {};
        mutable mutex::unique_lock::mutex_type _read_mutex alignas(mutex::alignment) {};
        mutable file::read_stream _is;

        static size_t _thread_part_idx(size_t part_idx, thread_data &t)
        {
            if (t.single_part_idx != max_parts) {
                if (part_idx != t.single_part_idx)
                    throw error(fmt::format("reader configured for a single partition {} but got request for data from partition {}!", t.single_part_idx, part_idx));
                part_idx = 0;
            }
            return part_idx;
        }

        void _load_cache(size_t part_idx, size_t new_chunk_idx, thread_data &t) const
        {
            size_t t_part_idx = _thread_part_idx(part_idx, t);
            auto &cache_chunk_idx = t.cache_chunk_idxs.at(t_part_idx);
            cache_chunk_idx = new_chunk_idx;
            auto &cache = t.caches.at(t_part_idx);
            const auto &cnt = _cnts.at(part_idx);
            const auto &chunk_list = _chunk_lists.at(part_idx);
            const auto &chunk = chunk_list.at(new_chunk_idx);
            size_t cache_offset = new_chunk_idx * _chunk_size;
            if (cache_offset + _chunk_size <= cnt) cache.resize(_chunk_size);
            else cache.resize(cnt - cache_offset);
            t.read_buf.resize(chunk.packed_size);

            {
                mutex::scoped_lock lock { _read_mutex };
                _is.seek(chunk.file_offset);
                _is.read(t.read_buf.data(), t.read_buf.size());
            }
            auto packed_hash = crypto::blake2b::digest<crypto::blake2b::hash_8>(t.read_buf);
            if (packed_hash != chunk.packed_hash)
                throw error(fmt::format("corrupted chunk data in index {} part {} chunk {} at offset {} size {} hash {} while expected hash {}",
                        _path, part_idx, new_chunk_idx, chunk.file_offset, chunk.packed_size, packed_hash, chunk.packed_hash));
            std::span<uint8_t> cache_buf { reinterpret_cast<uint8_t *>(cache.data()), sizeof(T) * cache.size() };
            zstd::decompress(cache_buf, t.read_buf);
            t.num_reads++;
        }
    };

    template<class T>
    struct reader {
        reader(const std::string &path): _reader { path }, _data { _reader.init_thread() }
        {
        }

        bool eof_part(size_t part_idx)
        {
            return _reader.eof_part(part_idx, _data);
        }

        bool eof()
        {
            return _reader.eof(_data);
        }

        reader_mt<T>::find_result find(const T &search_item)
        {
            return _reader.find(search_item, _data);
        }

        const buffer get_meta(const std::string &name) const
        {
            return _reader.get_meta(name);
        }

        size_t num_parts()
        {
            return _reader.num_parts();
        }

        bool read_part(size_t part_id, T& item)
        {
            return _reader.read_part(part_id, item, _data);
        }

        bool read(T &item)
        {
            return _reader.read(item, _data);
        }

        size_t size_part(size_t part_idx)
        {
            return _reader.size_part(part_idx);
        }

        size_t size()
        {
            return _reader.size();
        }

        reader_mt<T> &mt()
        {
            return _reader;
        }

    private:
        reader_mt<T> _reader;
        reader_mt<T>::thread_data _data;
    };

    template<class T>
    struct reader_multi_mt {
        struct thread_data {
            std::vector<std::unique_ptr<typename reader_mt<T>::thread_data>> data {};
            std::vector<merge_queue<T>> read_part_queue {};
            std::vector<size_t> matches {};
            size_t next_match_count = 0;
            size_t num_reads = 0;
            size_t single_part_no = max_parts;
        };

        reader_multi_mt(const std::span<const std::string> &paths): _readers {}
        {
            _readers.reserve(paths.size());
            for (const auto &p: paths)
                _readers.emplace_back(std::make_unique<reader_mt<T>>(p));
        }

        thread_data init_thread(size_t part_no=max_parts) const
        {
            thread_data t {};
            if (part_no != max_parts)
                t.single_part_no = part_no;
            t.data.reserve(_readers.size());
            for (const auto &reader: _readers) {
                t.data.emplace_back(std::make_unique<typename reader_mt<T>::thread_data>(reader->init_thread(part_no)));
                t.read_part_queue.emplace_back();
            }
            t.matches.resize(_readers.size());
            return t;
        }

        size_t num_parts() const
        {
            std::optional<size_t> n_parts {};
            for (size_t ri = 0; ri < _readers.size(); ++ri) {
                auto &reader = _readers.at(ri);
                if (n_parts) {
                    if (*n_parts != reader->num_parts())
                        throw error("index slices have differing number of partitions!");
                } else
                    n_parts.emplace(reader->num_parts());
            }
            if (!n_parts)
                throw error("can't determine the number of partitions in a multi-slice index");
            return *n_parts;
        }

        bool eof_part(size_t part_no, thread_data &t) const
        {
            size_t t_part_no = _thread_part_no(part_no, t);
            if (!t.read_part_queue.at(t_part_no).empty())
                return false;
            for (size_t ri = 0; ri < _readers.size(); ++ri) {
                auto &reader = _readers.at(ri);
                auto &data = *(t.data.at(ri));
                if (!reader->eof_part(part_no, data))
                    return false;
            }
            return true;
        }

        bool eof(thread_data &t) const
        {
            for (size_t ri = 0; ri < _readers.size(); ++ri) {
                auto &reader = _readers.at(ri);
                auto &data = *(t.data.at(ri));
                if (!reader->eof(data))
                    return false;
            }
            return true;
        }

        reader_mt<T>::find_result find(const T &search_item, thread_data &t) const
        {
            size_t total_match_count = 0;
            t.next_match_count = 0;
            t.num_reads = 0;
            T first_item {};
            for (size_t ri = 0; ri < _readers.size(); ++ri) {
                auto &reader = _readers.at(ri);
                auto &data = *(t.data.at(ri));
                auto [ match_count, match_item ] = reader->find(search_item, data);
                if (match_count) {
                    if (total_match_count == 0) {
                        first_item = match_item;
                        t.matches.at(ri) = match_count - 1;
                    } else {
                        reader->seek_part(data.next_part_idx, reader->offset_part(data.next_part_idx, data) - 1, data);
                        t.matches.at(ri) = match_count;
                    }
                    total_match_count += match_count;
                    t.next_match_count += t.matches.at(ri);
                } else {
                    t.matches.at(ri) = 0;
                }
                t.num_reads += data.num_reads;
            }
            return typename reader_mt<T>::find_result { total_match_count, first_item };
        }

        bool read_part(size_t part_no, T &item, thread_data &t) const
        {
            size_t t_part_no = _thread_part_no(part_no, t);
            auto &read_queue = t.read_part_queue.at(t_part_no);
            if (read_queue.empty()) {
                T val {};
                for (size_t i = 0; i < _readers.size(); ++i)
                    if (_readers.at(i)->read_part(part_no, val, *(t.data.at(i))))
                        read_queue.emplace(std::move(val), i);
            }
            if (!read_queue.empty()) {
                auto next = read_queue.top();
                read_queue.pop();
                item = next.val;
                if (_readers.at(next.stream_idx)->read_part(part_no, next.val, *(t.data.at(next.stream_idx))))
                    read_queue.emplace(std::move(next));
                return true;
            }
            return false;
        }

        bool read(T &item, thread_data &t) const
        {
            for (size_t ri = 0; ri < _readers.size(); ++ri) {
                if (t.next_match_count > 0) {
                    auto &match_cnt = t.matches.at(ri);
                    if (match_cnt == 0)
                        continue;
                    match_cnt--;
                    t.next_match_count--;
                }
                auto &reader = _readers.at(ri);
                auto &data = *(t.data.at(ri));
                if (reader->read(item, data))
                    return true;
            }
            return false;
        }

        size_t size_part(const size_t part_no) const
        {
            size_t tot_size = 0;
            for (size_t i = 0; i < _readers.size(); ++i)
                tot_size += _readers.at(i)->size_part(part_no);
            return tot_size;
        }

        size_t size() const
        {
            size_t tot_size = 0;
            for (const auto &reader: _readers)
                tot_size += reader->size();
            return tot_size; 
        }

        [[nodiscard]] std::vector<std::string> paths() const
        {
            std::vector<std::string> p {};
            p.reserve(_readers.size());
            for (const auto &r: _readers)
                p.emplace_back(r->path());
            return p;
        }
    private:
        std::vector<std::unique_ptr<reader_mt<T>>> _readers;

        static size_t _thread_part_no(size_t part_no, thread_data &t)
        {
            if (t.single_part_no != max_parts) {
                if (part_no != t.single_part_no)
                    throw error(fmt::format("reader configured for a single partition {} but got request for data from partition {}!", t.single_part_no, part_no));
                part_no = 0;
            }
            return part_no;
        }
    };

    template<class T>
    struct reader_multi {
        reader_multi(const std::span<const std::string> &paths)
            : _reader { paths }, _data { _reader.init_thread() }
        {
        }

        bool eof()
        {
            return _reader.eof(_data);
        }

        reader_mt<T>::find_result find(const T &search_item)
        {
            return _reader.find(search_item, _data);
        }

        bool read(T &item)
        {
            return _reader.read(item, _data);
        }

        size_t size() const
        {
            return _reader.size();
        }

        [[nodiscard]] std::vector<std::string> paths() const
        {
            return _reader.paths();
        }
    private:
        reader_multi_mt<T> _reader;
        reader_multi_mt<T>::thread_data _data;
    };
}
