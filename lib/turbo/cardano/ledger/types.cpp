/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/types.hpp>
#include <turbo/common/progress.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/math/big-int.hpp>

namespace turbo::cardano::ledger {
    parallel_decoder::parallel_decoder(const std::string &path): _data { file::read(path) }
    {
        const auto data = static_cast<buffer>(_data);
        const auto num_bufs = data.subbuf(0, sizeof(size_t)).to<size_t>();
        size_t next_offset = (num_bufs + 1) * sizeof(size_t);
        for (size_t i = 0; i < num_bufs; ++i) {
            const auto buf_size = data.subbuf((i + 1) * sizeof(size_t), sizeof(size_t)).to<size_t>();
            _buffers.emplace_back(data.subbuf(next_offset, buf_size));
            next_offset += buf_size;
        }
    }

    size_t parallel_decoder::size() const
    {
        return _buffers.size();
    }

    buffer parallel_decoder::at(const size_t idx) const
    {
        return _buffers.at(idx);
    }

    void parallel_decoder::add(const decode_func &t)
    {
        _tasks.emplace_back(t);
    }

    void parallel_decoder::on_done(const done_func &f)
    {
        _on_done.emplace_back(f);
    }

    void parallel_decoder::run(scheduler &sched, const std::string &task_group, const int prio, const bool report_progress)
    {
        if (_tasks.size() != _buffers.size()) [[unlikely]]
            throw error(fmt::format("was expecting {} items in the serialized data but got {}!", _buffers.size(), _tasks.size()));
        sched.wait_all(task_group,
            [&](const auto &todo, const auto &submit_f) {
                for (size_t i = 0; i < _buffers.size(); ++i) {
                    submit_f({
                        numeric_cast<int64_t>(_buffers[i].size() * prio / _data.size()),
                        task_group,
                        [&, i, todo] {
                            _tasks[i](_buffers[i]);
                            if (report_progress) {
                                const auto new_todo = todo->load(std::memory_order_relaxed) - 1;
                                progress::get().update(task_group, _buffers.size() - new_todo, _buffers.size());
                            }
                        }
                    });
                }
            }
        );
        for (const auto &f: _on_done)
            f();
    }

    pool_info::pool_info()
    {
        new (&rational_from_storage(reward_base)) cpp_rational {};
    }

    pool_info::pool_info(const pool_params &p):
        params { p }
    {
        new (&rational_from_storage(reward_base)) cpp_rational {};
    }

    pool_info::pool_info(pool_params &&p):
        params { std::move(p) }
    {
        new (&rational_from_storage(reward_base)) cpp_rational {};
    }

    pool_info::pool_info(const pool_info &o):
        params { o.params }
    {
        new (&rational_from_storage(reward_base)) cpp_rational { rational_from_storage(o.reward_base) };
    }

    pool_info::pool_info(pool_info &&o):
        params { std::move(o.params) }
    {
        new (&rational_from_storage(reward_base)) cpp_rational { std::move(rational_from_storage(o.reward_base)) };
    }

    pool_info &pool_info::operator=(const pool_info &o)
    {
        if (this != &o) {
            params = o.params;
            rational_from_storage(reward_base) = rational_from_storage(o.reward_base);
        }
        return *this;
    }

    pool_info &pool_info::operator=(pool_info &&o)
    {
        if (this != &o) {
            params = std::move(o.params);
            rational_from_storage(reward_base) = std::move(rational_from_storage(o.reward_base));
        }
        return *this;
    }

    pool_info::~pool_info()
    {
        rational_from_storage(reward_base).~cpp_rational();
    }

    operating_pool_info operating_pool_info::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        return { decltype(rel_stake)::from_cbor(it.read()), it.read().uint(), it.read().bytes() };
    }

    operating_pool_map operating_pool_map::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        auto res = map_from_cbor<operating_pool_map>(it.read());
        res.total_stake = it.read().uint();
        return res;
    }

    void operating_pool_map::clear()
    {
        base_type::clear();
        total_stake = 1; // 1 instead of 0 to mitigate division by zero
    }

    account_info account_info::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        auto &p1_it = it.read().at(0).array();
        return {
            .reward = p1_it.read().uint(),
            .deposit = p1_it.read().uint(),
            .ptr = stake_pointer::from_cbor(it.read().at(0)),
            .deleg = decltype(deleg)::from_cbor(it.read())
        };
    }
}
