/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/sync/p2p.hpp>
#include <dt/test.hpp>
#include <dt/validator.hpp>

namespace {
    using namespace daedalus_turbo;
    using namespace daedalus_turbo::cardano;
    using namespace daedalus_turbo::cardano::network;

    struct cardano_client_mock: cardano::network::client {
        cardano_client_mock(const network::address &addr, const buffer &raw_data)
                : client { addr }, _raw_data { raw_data }
        {
            cbor_parser p { _raw_data };
            while (!p.eof()) {
                auto &val = _cbor.emplace_back(std::make_unique<cbor_value>());
                p.read(*val);
                _blocks.emplace_back(cardano::make_block(*val, val->data - _raw_data.data()));
            }
            if (_blocks.empty())
                throw error("test chain cannot be empty!");
        }
    private:
        using cbor_val_list = std::vector<std::unique_ptr<cbor_value>>;
        using block_list = std::vector<std::unique_ptr<cardano::block_base>>;

        uint8_vector _raw_data {};
        cbor_val_list _cbor {};
        block_list _blocks {};

        std::optional<block_list::const_iterator> _find_intersection(const point_list &points)
        {
            for (const auto &p: points) {
                for (auto it = _blocks.begin(); it != _blocks.end(); ++it) {
                    if ((*it)->hash() == p.hash)
                        return it;
                }
            }
            return {};
        }

        void _find_intersection_impl(const point_list &points, const find_handler &handler) override
        {
            const auto intersection = _find_intersection(points);
            const point tip { _blocks.back()->hash(), _blocks.back()->slot(), _blocks.back()->height() };
            if (intersection) {
                const point isect { (**intersection)->hash(), (**intersection)->slot(), (**intersection)->height() };
                handler(find_response { _addr, point_pair { isect, tip } });
            } else {
                handler(find_response { _addr, tip });
            }
        }

        void _fetch_headers_impl(const point_list &points, const size_t max_blocks, const header_handler &handler) override
        {
            const auto intersection = _find_intersection(points);
            header_response resp { _addr };
            resp.tip = point { _blocks.back()->hash(), _blocks.back()->slot() };
            header_list headers {};
            if (intersection) {
                resp.intersect = point { (**intersection)->hash(), (**intersection)->slot() };
                for (auto it = std::next(*intersection); it != _blocks.end() && headers.size() < max_blocks; ++it)
                    headers.emplace_back((*it)->hash(), (*it)->slot(), (*it)->height());
            } else {
                for (auto it = _blocks.begin(); it != _blocks.end() && headers.size() < max_blocks; ++it)
                    headers.emplace_back((*it)->hash(), (*it)->slot(), (*it)->height());
            }
            resp.res = std::move(headers);
            handler(std::move(resp));
        }

        void _fetch_blocks_impl(const point &from, const point &to, const block_handler &handler) override
        {
            std::optional<block_list::const_iterator> intersection {};
            for (auto it = _blocks.begin(); it != _blocks.end(); ++it) {
                if ((*it)->hash() == from.hash) {
                    intersection = it;
                    break;
                }
            }
            if (!intersection) {
                handler(block_response { {}, error_msg { "The requested from block is unknown!" } });
                return;
            }
            for (auto it = *intersection; it != _blocks.end(); ++it) {
                block_parsed bp {};
                bp.data = std::make_unique<uint8_vector>((*it)->raw_data());
                bp.cbor = std::make_unique<cbor_value>(cbor::parse(*bp.data));
                bp.blk = cardano::make_block(*bp.cbor, (*it)->offset());
                if (!handler({ std::move(bp) }) || (*it)->hash() == to.hash)
                    break;
            }
        }

        void _process_impl(scheduler */*sched*/) override
        {
        }

        void _reset_impl() override
        {
        }
    };

    struct cardano_client_manager_mock: client_manager {
        explicit cardano_client_manager_mock(const std::string &path): _raw_data { file::read(path) }
        {
        }

        explicit cardano_client_manager_mock(const std::span<std::string> &paths): _raw_data{ file::read_all(paths) }
        {
        }
    private:
        uint8_vector _raw_data;

        std::unique_ptr<client> _connect_impl(const network::address &addr, asio::worker &/*asio_worker*/) override
        {
            return std::make_unique<cardano_client_mock>(addr, _raw_data);
        }
    };
}

suite sync_p2p_suite = [] {
    "sync::p2p"_test = [] {
        const std::string data_dir { "./tmp/sync-p2p-test" };
        cardano_client_manager_mock ccm { "./data/sync-http/success/D0AAE92BCD98E5C43D7729B8B3DFDA6007D606B3CC58868FEBAD6C6ACF8B4575.zstd" };
        "success"_test = [&] {
            std::filesystem::remove_all(data_dir);
            validator::incremental cr { data_dir };
            sync::p2p::syncer s { cr, ccm };
            expect(s.sync());
            expect(cr.max_slot() == 9_ull);
            expect(cr.valid_end_offset() == 654757_ull);
        };
        "no work"_test = [&] {
            std::filesystem::remove_all(data_dir);
            validator::incremental cr { data_dir };
            sync::p2p::syncer s { cr, ccm };
            expect(s.sync());
            expect(cr.max_slot() == 9_ull);
            expect(cr.valid_end_offset() == 654757_ull);
            expect(!s.sync());
            expect(cr.max_slot() == 9_ull);
            expect(cr.valid_end_offset() == 654757_ull);
        };
        "max_slot"_test = [&] {
            std::filesystem::remove_all(data_dir);
            validator::incremental cr { data_dir };
            sync::p2p::syncer s { cr, ccm };
            expect(s.sync({}, 5));
            expect(cr.max_slot() == 5_ull);
        };
        "multi chunk"_test = [data_dir] {
            std::filesystem::remove_all(data_dir);
            validator::incremental cr { data_dir };
            std::vector<std::string> paths {};
            paths.emplace_back("./data/chunk-registry-new/0-0.chunk");
            paths.emplace_back("./data/chunk-registry-new/0-1.chunk");
            paths.emplace_back("./data/chunk-registry-new/1-0.chunk");
            paths.emplace_back("./data/chunk-registry-new/1-1.chunk");
            paths.emplace_back("./data/chunk-registry-new/2-0.chunk");
            paths.emplace_back("./data/chunk-registry-new/2-1.chunk");
            paths.emplace_back("./data/chunk-registry-new/3-0.chunk");
            cardano_client_manager_mock ccm { paths };
            sync::p2p::syncer s { cr, ccm };
            s.sync({}, 50'000);
            expect(cr.max_slot() == 50'000_ull);
            s.sync();
            expect(cr.max_slot() == 79'999_ull);
        };
        "find_peer"_test = [&] {
            std::filesystem::remove_all(data_dir);
            validator::incremental cr { data_dir };
            sync::p2p::syncer s { cr, ccm };
            const auto peer = s.find_peer();
            expect(static_cast<bool>(peer.client));
            expect(peer.tip.slot > 0);
            expect(peer.tip.height > 0);
            expect(!peer.isect);
        };
    };
};