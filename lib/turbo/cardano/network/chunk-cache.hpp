#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <turbo/cardano/common/types.hpp>
#include <turbo/common/scope-exit.hpp>

namespace turbo::cardano::network {
    struct chunk_cache {
        static constexpr size_t default_max_bytes = size_t { 1 } << 30;
        using data_ptr = std::shared_ptr<const uint8_vector>;

        explicit chunk_cache(size_t max_bytes=default_max_bytes): _max_bytes { max_bytes } {}

        template<typename Loader>
        data_ptr get(const block_hash &hash, size_t size, Loader &&load)
        {
            std::shared_ptr<entry> e;
            {
                std::unique_lock lk { _mutex };
                if (const auto it = _entries.find(hash); it != _entries.end()) {
                    e = it->second;
                    if (e->size != size)
                        throw error("inconsistent decompressed chunk size");
                    e->ready.wait(lk, [&] { return !e->loading; });
                    if (e->failure) std::rethrow_exception(e->failure);
                    _lru.splice(_lru.end(), _lru, e->position);
                    return e->data;
                }
                _reserve(size);
                e = std::make_shared<entry>(size);
                e->position = _lru.insert(_lru.end(), hash);
                scope_exit cleanup { [&] { _lru.erase(e->position); } };
                _entries.emplace(hash, e);
                _used_bytes += size;
                cleanup.release();
            }

            data_ptr data;
            const auto failure = logger::run_log_errors([&] {
                auto bytes = std::make_shared<uint8_vector>(size);
                load(write_buffer { bytes->data(), bytes->size() });
                data = std::move(bytes);
            });
            {
                std::scoped_lock lk { _mutex };
                e->data = std::move(data);
                e->failure = failure;
                e->loading = false;
                if (failure) {
                    _used_bytes -= size;
                    _lru.erase(e->position);
                    _entries.erase(hash);
                }
            }
            e->ready.notify_all();
            if (failure) std::rethrow_exception(failure);
            return e->data;
        }

    private:
        struct entry {
            explicit entry(size_t sz): size { sz } {}
            const size_t size;
            bool loading = true;
            std::condition_variable ready;
            std::exception_ptr failure;
            data_ptr data;
            std::list<block_hash>::iterator position;
        };

        const size_t _max_bytes;
        size_t _used_bytes = 0;
        std::mutex _mutex;
        std::list<block_hash> _lru;
        std::map<block_hash, std::shared_ptr<entry>> _entries;

        void _reserve(size_t size)
        {
            if (size > _max_bytes)
                throw error(fmt::format("chunk of {} bytes exceeds the chunk cache limit of {} bytes", size, _max_bytes));
            for (auto it = _lru.begin(); size > _max_bytes - _used_bytes && it != _lru.end();) {
                const auto found = _entries.find(*it);
                const auto &e = found->second;
                // Loaders and waiters also pin entries until they acquire the buffer.
                if (e.use_count() == 1 && !e->loading && e->data.use_count() == 1) {
                    _used_bytes -= e->size;
                    _entries.erase(found);
                    it = _lru.erase(it);
                } else {
                    ++it;
                }
            }
            if (size > _max_bytes - _used_bytes)
                throw error(fmt::format("chunk cache is full: {} of {} bytes reserved; cannot admit {} bytes",
                    _used_bytes, _max_bytes, size));
        }
    };
}
