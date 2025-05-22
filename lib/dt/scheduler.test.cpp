/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <iostream>
#include <vector>
#include <string>
#include <dt/common/test.hpp>
#include <dt/scheduler.hpp>
#include <dt/util.hpp>

using namespace std::literals;
using namespace daedalus_turbo;

suite scheduler_suite = [] {
    "scheduler"_test = [] {
        "chained_scheduling"_test = [] {
            scheduler s {};
            size_t preproc_calls = 0;
            std::vector<std::string> preproc_done {};

            size_t merge_1_calls = 0;
            std::vector<std::string> merge_1_done;

            size_t merge_2_calls = 0;
            std::vector<std::string> merge_2_done;

            // processed in the manager thread, no locking is needed
            std::vector<std::string> preproc_ready, merge_1_ready;

            s.on_result("pre_process", [&](const std::any &res) {
                std::string path = std::any_cast<std::string>(res);
                ++preproc_calls;
                preproc_done.emplace_back(path);
                if (s.task_count("pre_process") == 0) {
                    std::vector<std::string> preproc_ready { preproc_done.begin(), preproc_done.end() };
                    while (preproc_ready.size() > 0) {
                        std::vector<std::string> merge_paths {};
                        while (preproc_ready.size() > 0 && merge_paths.size() < 4) {
                            merge_paths.push_back(preproc_ready.back());
                            preproc_ready.pop_back();
                        }
                        s.submit("merge_1/addr_use", 10, [merge_paths]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds { 200 });
                            return merge_paths;
                        });
                    }
                }
            });
            s.on_result("merge_2/addr_use", [&](const std::any &res) {
                auto paths = std::any_cast<std::vector<std::string>>(res);
                ++merge_2_calls;
                for (const auto &p: paths) merge_2_done.emplace_back(p);
            });
            s.on_result("merge_1/addr_use", [&](const std::any &res) {
                auto paths = std::any_cast<std::vector<std::string>>(res);
                ++merge_1_calls;
                for (const auto &p: paths)
                    merge_1_done.emplace_back(p);
                merge_1_ready.insert(merge_1_ready.end(), paths.begin(), paths.end());
                if (s.task_count("merge_1/addr_use") == 0) {
                    s.submit("merge_2/addr_use", 1, [merge_1_ready] {
                        std::this_thread::sleep_for(400ms);
                        return merge_1_ready;
                    });
                    merge_1_ready.clear();
                }
            });
            for (size_t i = 0; i < 16; ++i) {
                std::string path = fmt::format("chunk-{}.in", i);
                s.submit("pre_process", 100, [path] {
                    std::this_thread::sleep_for(100ms);
                    return path;
                });
            }
            s.process();
            expect(preproc_calls == 16_u);
            expect(preproc_done.size() == 16_u);
            expect(merge_1_calls == 4_u);
            expect(merge_1_done.size() == 16_u);
            expect(merge_2_calls == 1_u);
            expect(merge_2_done.size() == 16_u);
            expect(s.num_workers() == scheduler::default_worker_count());
        };
        "exceptions"_test = [] {
            scheduler s {};
            size_t num_ok = 0, num_err = 0;
            s.on_result("bad_actor", [&](const std::any &res) {
                if (res.type() == typeid(scheduled_task_error)) ++num_err;
                else ++num_ok;
            });
            s.submit("bad_actor", 100, []() { throw error("Ha ha! I told ya!"); return true; });
            expect(!s.process_ok());
            expect(num_ok == 0_u);
            expect(num_err == 1_u);
        };
        "exceptions_no_observer"_test = [] {
            scheduler s {};
            s.submit("bad_actor", 100, []() { throw error("Ha ha! I told ya!"); return true; });
            expect(throws([&]{ s.process(); }));
        };
        "observers are cleared after each process call"_test = [] {
            scheduler s {};
            size_t ok_cnt = 0;
            s.on_result("ok", [&](const auto &) {
                ok_cnt++;
            });
            s.submit("ok", 100, []() { return true; });
            s.process();
            expect(ok_cnt == 1);
            s.on_result("ok", [&](const auto &) {
                ok_cnt++;
            });
            s.submit("ok", 100, []() { return true; });
            s.process();
            expect(ok_cnt == 2); // increment only by 1 despite two on_result observer is registered two times
        };
        "resource management"_test = [] {
            struct resource {
                resource(): _ptr { std::make_shared<int>(22) } {
                    //std::cerr << fmt::format("resource created use_cout: {}\n", _ptr.use_count());
                }
                resource(const resource &v): _ptr { v._ptr } {
                    //std::cerr << fmt::format("resource copied use_cout: {}\n", _ptr.use_count());
                }
                ~resource() {
                    //std::cerr << fmt::format("resource destroyed use_cout: {}\n", _ptr.use_count());
                }
                long use_count() const {
                    return _ptr.use_count();
                }
            private:
                std::shared_ptr<int> _ptr {};
            };

            resource r {};
            expect(r.use_count() == 1_l);
            scheduler s { 2 };
            s.on_result("test", [r](const auto &) {
                expect(r.use_count() == 2_l);
            });
            expect(r.use_count() == 2_l);
            s.submit("test", 100, [r] { return true; });
            //expect(r.use_count() == 3_l);
            s.process(false);
            expect(r.use_count() == 1_l);
        };
        "empty task list works"_test = [] {
            {
                scheduler s {};
                s.process();
                // must not hang!
                expect(true);
            }
            {
                scheduler s { 1 };
                s.process();
                // must not hang!
                expect(true);
            }
        };
        "wait_for_count"_test = [] {
            scheduler s {};
            s.submit_void("test", 100, [&] {
                s.wait_all_done("wait", 2, [&] {
                    s.submit_void("wait", 200, [] {
                        std::this_thread::sleep_for(std::chrono::milliseconds { 500 });
                    });
                    s.submit_void("wait", 300, [] {
                        std::this_thread::sleep_for(std::chrono::milliseconds { 200 });
                    });
                });
            });
            s.process();
            expect(true);
        };
        "clear_observers"_test = [] {
            scheduler s {};
            std::optional<size_t> num_before {};
            std::optional<size_t> num_after {};
            s.submit_void("wait", 100, [&] {
                s.wait_all_done("ok", 2, [&] {
                    s.submit("ok", 100, []() { return true; });
                    s.submit("ok", 100, []() { return true; });
                });
                num_before = s.num_observers("ok");
                s.clear_observers("ok");
                num_after = s.num_observers("ok");
            });
            s.process();
            expect(num_before && num_before == 1);
            expect(num_after && num_after == 0);
        };
        "on_completion"_test = [] {
            scheduler s {};
            size_t num_completions = 0;
            "process clears completion handlers"_test = [&] {
                s.on_completion("wait", 2, [&] {
                    ++num_completions;
                });
                s.submit_void("wait", 200, [] {
                    std::this_thread::sleep_for(std::chrono::milliseconds { 500 });
                });
                expect(num_completions == 0_ull);
                s.process();
                expect(num_completions == 0_ull);
                s.submit_void("wait", 300, [] {
                    std::this_thread::sleep_for(std::chrono::milliseconds { 200 });
                });
                s.process();
                expect(num_completions == 0_ull);
            };
            "success"_test = [&] {
                s.on_completion("wait", 2, [&] {
                    ++num_completions;
                });
                s.submit_void("wait", 200, [] {
                    std::this_thread::sleep_for(std::chrono::milliseconds { 500 });
                });
                expect(num_completions == 0_ull);
                s.submit_void("wait", 300, [] {
                    std::this_thread::sleep_for(std::chrono::milliseconds { 200 });
                });
                s.process();
                expect(num_completions == 1_ull);
            };
        };
        "cancel"_test = [] {
            scheduler s { 2 };
            std::atomic_size_t num_cancelled = 0;
            s.on_result("task1", [&](auto &&/*res*/) {
                const auto num_c = s.cancel([](const auto &name, const auto &param) { return name == "task1" && param && std::any_cast<bool>(*param) == true; });
                num_cancelled.fetch_add(num_c, std::memory_order_relaxed);
            });
            for (size_t i = 0; i < 1000; ++i) {
                // slow higher-priority tasks
                s.submit_void("task1", 100, [] {
                    std::this_thread::sleep_for(std::chrono::seconds { 1 });
                }, true);
                // fast lower-priority tasks
                s.submit_void("task1", 10, [] {
                    std::this_thread::sleep_for(std::chrono::milliseconds { 10 });
                }, false);
            }
            const auto num_tasks = s.task_count();
            expect(num_tasks == 2000_ull);
            s.process();
            const auto num_cancelled_tasks = num_cancelled.load();
            expect(num_cancelled_tasks >= 900 && num_cancelled_tasks < 1000) << num_cancelled_tasks;
        };
    };
};;