#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano.hpp>
#include <turbo/common/scheduler.hpp>
#include <turbo/common/logger.hpp>
#include <turbo/storage/chunk-info.hpp>

namespace turbo::storage {
    struct block_reader_t {
        block_reader_t(const buffer data, const uint64_t offset, const cardano::config &cfg)
            : _data { data }, _offset { offset }, _cfg { cfg }, _decoder { data } {}

        bool done() { return _decoder.done(); }

        cardano::block_container next()
        {
            auto &tuple = _decoder.read();
            return { _offset + numeric_cast<uint64_t>(tuple.data_begin() - _data.data()), tuple, _cfg };
        }
    private:
        buffer _data;
        uint64_t _offset;
        const cardano::config &_cfg;
        cbor::zero2::decoder _decoder;
    };

    // Admission runs on the producer, never in scheduler workers. Draining a
    // batch also propagates parser failures before more input is admitted.
    struct replay_batch_t {
        explicit replay_batch_t(scheduler &sched): _sched { sched },
            _limit { chunk_work_policy_t::default_budget(sched.num_workers()) } {}

        replay_batch_t(const replay_batch_t &) =delete;
        replay_batch_t &operator=(const replay_batch_t &) =delete;

        ~replay_batch_t()
        {
            // Declare the batch after state captured by its tasks. Explicit
            // drains report errors; unwinding must finish tasks before that
            // state is destroyed without replacing the original exception.
            if (_bytes) {
                logger::run_log_errors([&] { drain(); });
            }
        }

        void admit(const uint64_t raw_bytes, const uint64_t compressed_bytes)
        {
            const auto cost = chunk_work_policy_t::estimated_cost(raw_bytes, compressed_bytes);
            if (!chunk_work_policy_t::can_admit(_bytes, cost, _limit)) {
                drain();
            }
            _bytes += cost;
        }

        void drain()
        {
            _sched.process(true);
            _bytes = 0;
        }
    private:
        scheduler &_sched;
        uint64_t _limit;
        uint64_t _bytes = 0;
    };
}
