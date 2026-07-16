#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <functional>
#include <turbo/common/bytes.hpp>
#include <turbo/common/file.hpp>
#include <turbo/common/progress.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/timer.hpp>
#include <turbo/parallel/encoder.hpp>
#include <vector>

namespace turbo::parallel {
    template<typename ENC>
    struct encoder {
        using encode_func = std::function<uint8_vector(ENC)>;
        using init_func = std::function<ENC()>;

        explicit encoder(const init_func &init_fn=[]{ return ENC {}; }):
            _init_fn { init_fn }
        {
        }

        [[nodiscard]] size_t size() const
        {
            return _tasks.size();
        }

        void add(const encode_func &t)
        {
            _tasks.emplace_back(t);
            _buffers.emplace_back();
        }

        void run(scheduler &sched, const std::string &task_group, int prio=1000, bool report_progress=false)
        {
            sched.wait_all(
                task_group,
                [&](const scheduler::todo_count_t &todo, const scheduler::submit_func_t &submit_f) -> void {
                    for (size_t i = 0; i < _tasks.size(); ++i) {
                        submit_f({ prio, task_group, [this, i, todo, &task_group, report_progress]() -> void {
                            if constexpr (std::is_same_v<ENC, void>) {
                                _buffers[i] = _tasks[i]();
                            } else {
                                _buffers[i] = _tasks[i](_init_fn());
                            }
                            if (report_progress) {
                                const auto new_todo = todo->load(std::memory_order_relaxed) - 1;
                                progress::get().update(task_group, _tasks.size() - new_todo, _tasks.size());
                            }
                        } });
                    }
                }
            );
        }

        void save(const std::string &path, bool headers=false) const
        {
            const auto tmp_path = fmt::format("{}.tmp", path);
            timer t { fmt::format("writing serialized data to {}", path), logger::level::debug };
            {
                file::write_stream ws { tmp_path };
                // first write the block sizes to allow parallel load
                if (headers) {
                    ws.write(buffer::from<size_t>(_buffers.size()));
                    for (const auto &buf: _buffers)
                        ws.write(buffer::from<size_t>(buf.size()));
                }
                // then write the actual data
                for (const auto &buf: _buffers)
                    ws.write(buf);
            }
            // ensures the correct file exists only if the whole saving procedure is successful
            std::filesystem::rename(tmp_path, path);
        }

        [[nodiscard]] uint8_vector flat() const
        {
            uint8_vector res {};
            res.reserve(std::accumulate(_buffers.begin(), _buffers.end(), size_t { 0 }, [](const auto sum, const auto &b) { return sum + b.size(); }));
            for (const auto &buf: _buffers)
                res << buf;
            return res;
        }
    private:
        init_func _init_fn;
        std::vector<encode_func> _tasks {};
        std::vector<uint8_vector> _buffers {};
    };
}