#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <random>
#include <boost/url.hpp>
#include <turbo/common/error.hpp>
#include <turbo/cardano/common/block-producer.hpp>
#include <turbo/cardano/network/common.hpp>

namespace turbo::sync {
    using namespace turbo::cardano;
    using namespace turbo::cardano::network;

    enum class failure_type {
        prev_hash,
        slot_no
        /*kes_seq_no,
        kes_signature,
        vrf_signature,
        leadership_eligibility,
        block_body_hash,*/
    };

    struct mock_chain_config {
        size_t height = 9;
        std::optional<uint64_t> failure_height = {};
        sync::failure_type failure_type = failure_type::prev_hash;
        configs_mock::map_type cfg {};
    };

     struct mock_chain {
         configs_mock cfg;
         cardano::config cardano_cfg { cfg };
         uint8_vector data {};
         parsed_block_list blocks {};
         block_hash data_hash {};
         optional_point tip {};

         mock_chain(configs_mock &&cfg_);
         mock_chain(mock_chain &&o);
         mock_chain() =delete;
         mock_chain(const mock_chain &) =delete;
    };

    struct random_failure_generator {
        explicit random_failure_generator(const double fail_rate):
            _fail_rate{fail_rate}
        {
        }

        bool operator()(const std::string_view msg)
        {
            const auto r = _next_rnd();
            const auto inject = r < _fail_rate;
            logger::debug("deciding on a random failure for '{}': {} < {}: {}", msg, r, _fail_rate, inject ? "inject" : "skip");
            return inject;
        }
    private:

        double _fail_rate;

        static double _next_rnd()
        {
            // a single random generator per process
            static std::default_random_engine _rnd { 123456U };
            static std::uniform_real_distribution<double> _dist { 0.0, 1.0 };
            return _dist(_rnd);
        }
    };

    struct cardano_client_mock: cardano::network::client {
        cardano_client_mock(const network::address &addr, const buffer &raw_data, double fail_rate);
    private:
        using block_list = std::vector<std::unique_ptr<cardano::block_container>>;

        uint8_vector _raw_data;
        block_list _blocks;
        random_failure_generator _fail_gen;

        std::optional<block_list::const_iterator> _find_intersection(const point2_list &points)
        {
            for (const auto &p: points) {
                for (auto it = _blocks.begin(); it != _blocks.end(); ++it) {
                    if ((**it)->hash() == p.hash)
                        return it;
                }
            }
            return {};
        }

        void _find_intersection_impl(const point2_list &points, const find_handler &handler) override
        {
            const auto intersection = _find_intersection(points);
            const point tip { (*_blocks.back())->hash(), (*_blocks.back())->slot(), (*_blocks.back())->height() };
            if (intersection) {
                const point isect { (***intersection)->hash(), (***intersection)->slot(), (***intersection)->height() };
                handler(find_response { _addr, intersection_info_t { isect, tip } });
            } else {
                handler(find_response { _addr, intersection_info_t { {}, tip } });
            }
        }

        void _fetch_headers_impl(const point2_list &points, const size_t max_blocks, const header_handler &handler) override
        {
            const auto intersection = _find_intersection(points);
            header_response resp { _addr };
            resp.tip = point { (*_blocks.back())->hash(), (*_blocks.back())->slot() };
            header_list headers {};
            if (intersection) {
                resp.intersect = point { (***intersection)->hash(), (***intersection)->slot() };
                for (auto it = std::next(*intersection); it != _blocks.end() && headers.size() < max_blocks; ++it) {
                    headers.emplace_back((**it)->slot(), (**it)->hash());
                }
            } else {
                for (auto it = _blocks.begin(); it != _blocks.end() && headers.size() < max_blocks; ++it) {
                    headers.emplace_back((**it)->slot(), (**it)->hash());
                }
            }
            resp.res = std::move(headers);
            handler(std::move(resp));
        }

        void _fetch_blocks_impl(const point2 &from, const point2 &to, const block_handler &handler) override;

        void _process_impl(scheduler */*sched*/, asio::worker *) override
        {
        }

        void _reset_impl() override
        {
        }
    };

    struct cardano_client_manager_mock: client_manager {
        explicit cardano_client_manager_mock(const buffer data, const double fail_rate=0.0):
            _raw_data { data },
            _fail_rate { fail_rate }
        {
        }

        explicit cardano_client_manager_mock(const std::string &path, const double fail_rate=0.0):
            cardano_client_manager_mock{file::read(path), fail_rate}
        {
        }

        explicit cardano_client_manager_mock(const std::vector<std::string> &paths, const double fail_rate=0.0):
            cardano_client_manager_mock{file::read_all(paths), fail_rate}
        {
        }
    private:
        uint8_vector _raw_data;
        double _fail_rate;

        std::unique_ptr<client> _connect_impl(const network::address &addr, const version_config_t &, const cardano::config &/*cfg*/, const asio::worker_ptr &/*asio_worker*/) override
        {
            return std::make_unique<cardano_client_mock>(addr, _raw_data, _fail_rate);
        }
    };

    extern mock_chain gen_chain(const mock_chain_config &mock_cfg={});
    extern void write_turbo_metadata(const std::string &www_dir, const mock_chain &chain, const crypto::ed25519::skey &sk);
}