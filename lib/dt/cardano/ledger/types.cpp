/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/ledger/types.hpp>
#include <dt/cardano/shelley/block.hpp>
#include <dt/progress.hpp>
#include <dt/scheduler.hpp>
#include <dt/timer.hpp>

namespace daedalus_turbo::cardano::ledger {
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
        sched.wait_all_done(task_group, _buffers.size(),
            [&] {
                for (size_t i = 0; i < _buffers.size(); ++i) {
                    sched.submit_void(task_group, _buffers[i].size() * prio / _data.size(), [&, i] { _tasks[i](_buffers[i]); } );
                }
            },
            [this, &task_group, report_progress](auto &&, auto done, auto errs) {
                if (report_progress)
                    progress::get().update(task_group, done - errs, _tasks.size());
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

    void operating_pool_info::to_cbor(era_encoder &enc) const
    {
        enc.array(3);
        switch (enc.era()) {
            case era_t::conway:
                rel_stake.to_cbor(enc);
                break;
            default:
                enc.array(2)
                    .uint(rel_stake.numerator)
                    .uint(rel_stake.denominator);
                break;
        }
        enc.uint(active_stake);
        enc.bytes(vrf_vkey);
    }

    operating_pool_map operating_pool_map::from_cbor(cbor::zero2::value &v)
    {
        auto &it = v.array();
        auto res = map_from_cbor<operating_pool_map>(it.read());
        res.total_stake = it.read().uint();
        return res;
    }

    void operating_pool_map::to_cbor(era_encoder &enc) const
    {
        enc.array(2);
        map_to_cbor(enc, *this);
        enc.uint(total_stake);
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
