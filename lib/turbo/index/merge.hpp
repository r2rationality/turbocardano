#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/scheduler.hpp>
#include <turbo/common/logger.hpp>
#include <turbo/index/io.hpp>
#include <turbo/parallel/algorithm.hpp>

namespace turbo::index {
    template<typename T>
    uint64_t merge_index_part(writer<T> &out_idx, size_t part_idx, const std::vector<std::shared_ptr<reader_mt<T>>> &readers)
    {
        std::vector<typename reader_mt<T>::thread_data> reader_data {};
        merge_queue<T> items_to_consider {};
        uint64_t max_offset = 0;
        for (size_t i = 0; i < readers.size(); ++i) {
            reader_data.emplace_back(readers[i]->init_thread(part_idx));
            T val;
            if (readers[i]->read_part(part_idx, val, reader_data[i]))
                items_to_consider.emplace(std::move(val), i);
            uint64_t r_max_offset = readers[i]->get_meta("max_offset").template to<uint64_t>();
            if (r_max_offset > max_offset)
                max_offset = r_max_offset;
        }
        while (items_to_consider.size() > 0) {
            merge_item next { items_to_consider.top() };
            items_to_consider.pop();
            out_idx.emplace_part(part_idx, next.val);
            if (readers[next.stream_idx]->read_part(part_idx, next.val, reader_data[next.stream_idx]))
                items_to_consider.emplace(std::move(next));
        }
        return max_offset;
    }

    template<typename T>
    void merge_one_step(scheduler &sched, const std::string &task_group, size_t task_prio,
        const std::vector<std::string> &chunks, const std::string &final_path,
        const std::function<void()> &on_complete)
    {
        if (chunks.empty()) {
            logger::trace("merge: no chunks for {} - ignoring", final_path);
            on_complete();
            return;
        }
        if (chunks.size() == 1) {
            auto chunk = chunks.at(0);
            std::filesystem::rename(chunk, final_path);
            logger::trace("merged {} chunks into {}", chunk, final_path);
            on_complete();
            return;
        }
        std::vector<std::shared_ptr<reader_mt<T>>> readers {};
        size_t num_parts = 0;
        for (size_t i = 0; i < chunks.size(); ++i) {
            auto &reader = readers.emplace_back(std::make_shared<reader_mt<T>>(chunks[i]));
            if (num_parts == 0)
                num_parts = reader->num_parts();
            if (num_parts != reader->num_parts())
                throw error(fmt::format("chunk {} has a partition count: {} different from the one found in other chunks: {}!",
                        chunks[i], reader->num_parts(), num_parts));
        }
        auto out_idx = std::make_shared<index::writer<T>>(final_path, num_parts);
        auto max_offset = std::make_shared<std::atomic<uint64_t>>(0);
        auto done = std::make_shared<std::atomic_size_t>(0);
        for (size_t pi = 0; pi < num_parts; ++pi) {
            sched.submit(task_group, task_prio, [max_offset, pi, out_idx, readers, done, num_parts, final_path, on_complete]() {
                const auto part_max_offset = merge_index_part(*out_idx, pi, readers);
                parallel::atomic_max(*max_offset, part_max_offset);
                const auto new_done = done->fetch_add(1, std::memory_order_relaxed) + 1;
                if (new_done == num_parts) {
                    out_idx->set_meta("max_offset", buffer::from(*max_offset));
                    out_idx->commit();
                    for (auto &r: readers) {
                        r->close();
                        std::filesystem::remove(r->path());
                    }
                    logger::trace("merged {} chunks into {}", readers.size(), final_path);
                    on_complete();
                }
            });
        }
    }
}