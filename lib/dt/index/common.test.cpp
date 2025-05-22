/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/test.hpp>
#include <dt/cardano.hpp>
#include <dt/file.hpp>
#include <dt/index/common.hpp>

namespace {
    using namespace daedalus_turbo;
    using namespace daedalus_turbo::index;

    struct index_item {
        uint64_t offset = 0;
        uint16_t out_idx = 0;

        bool index_less(const index_item &b) const
        {
            return offset < b.offset;
        }

        bool operator<(const index_item &b) const
        {
            if (offset != b.offset) return offset < b.offset;
            return out_idx < b.out_idx;
        }

        bool operator==(const index_item &b) const
        {
            return offset == b.offset;
        }
    };
}

suite index_common_suite = [] {
    "index::common"_test = [] {
        "writer/reader"_test = [] {
            file::tmp idx_path { "index-writer-test" };
            size_t num_items = 0x39886;
            {
                writer<index_item> idx { idx_path };
                for (size_t i = 0; i < num_items; ++i) {
                    idx.emplace(i * 2, (uint16_t)(i % 12));
                }
            }
            expect(std::filesystem::exists(idx_path.path())) << idx_path.path();
            {
                reader<index_item> idx { idx_path };
                size_t read_items = 0;
                index_item item {};
                while (idx.read(item)) {
                    ++read_items;
                }
                expect(num_items == read_items) << read_items;
            }
            std::filesystem::remove(idx_path.path());
        };

        "writer/reader partitioned"_test = [] {
            file::tmp idx_path { "index-writer-partitioned-test" };
            size_t num_items = 0x39886;
            size_t num_parts = 4;
            {
                writer<index_item> idx { idx_path, num_parts };
                for (size_t i = 0; i < num_items; ++i) {
                    for (size_t p = 0; p < num_parts; ++p)
                        idx.emplace_part(p, p * num_items + i, (uint16_t)(i % 12));
                }
            }
            expect(std::filesystem::exists(idx_path.path())) << idx_path.path();
            {
                reader<index_item> idx { idx_path };
                std::vector<size_t> read_items(num_parts);
                index_item item {};
                for (size_t p = 0; p < num_parts; p++) {
                    while (idx.read_part(p, item)) {
                        ++read_items[p];
                    }
                    expect(num_items == read_items[p]) << read_items[p];
                }
            }
            std::filesystem::remove(idx_path.path());
        };

        "partitioned index search"_test = [] {
            file::tmp idx_path { "index-writer-search-test" };
            size_t num_items = 0x98765; // more than the default cache sparsity to test both branches of index search
            size_t chunk_size = writer<index_item>::default_chunk_size;
            size_t part_size = chunk_size * 3;
            size_t num_parts = (num_items + part_size - 1) / part_size;
            {
                writer<index_item> idx { idx_path, num_parts };
                for (size_t i = 0; i < num_items; i += 2)
                    idx.emplace_part(i / part_size, i, static_cast<uint16_t>(i % 13));
            }
            {
                reader<index_item> reader { idx_path };
                index_item item {};
                for (size_t i = 0; i < num_items; i += 2) {
                    item.offset = i;
                    auto [ found_cnt, found_item ] = reader.find(item);
                    expect(found_cnt == 1) << "can't find" << i << found_cnt;
                    if (found_cnt == 1) expect(found_item == item) << found_item.offset << "!=" << i;
                    else break;
                }
            }
            {
                reader<index_item> reader { idx_path };
                index_item item {};
                for (size_t i = 1; i < num_items; i += 2) {
                    item.offset = i;
                    auto [ found_cnt, found_item ] = reader.find(item);
                    expect(found_cnt == 0) << "found" << i << found_cnt;
                }
            }
            std::filesystem::remove(idx_path.path());
        };

        "multi-part indices work"_test = [] {
            file::tmp idx_path_1 { "index-writer-1-multi-index-test" };
            size_t num_items_1 = 0x39873; // more than the default chunk_size to test all branches of index search
            {
                writer<index_item> idx { idx_path_1 };
                idx.emplace(0x0ULL);
                for (size_t i = 0; i < num_items_1 - 2; i++)
                    idx.emplace(0xDEADBEAFULL);
                idx.emplace(0xFFFFFFFFULL);
            }
            file::tmp idx_path_2 { "index-writer-2-multi-index-test" };
            size_t num_items_2 = 0x19873; // more than the default chunk_size to test all branches of index search
            {
                writer<index_item> idx { idx_path_2 };
                idx.emplace(0x0ULL);
                for (size_t i = 0; i < num_items_2 - 2; i++)
                    idx.emplace(0xDEADBEAFULL);
                idx.emplace(0xFFFFFFFFULL);
            }
            {
                std::array<std::string, 2> paths { idx_path_1, idx_path_2 };
                reader_multi<index_item> reader { paths };
                expect(reader.size() == num_items_1 + num_items_2);
                // successful search
                {
                    index_item search_item { 0xDEADBEAF };
                    auto [ found_cnt, found_item ] = reader.find(search_item);
                    expect(found_cnt == num_items_1 - 2 + num_items_2 - 2) << found_cnt;
                    expect(found_item == search_item);
                    for (size_t i = 1; i < found_cnt; ++i) {
                        expect(reader.read(found_item));
                        expect(found_item == search_item);
                    }
                    expect(reader.read(found_item));
                    expect(found_item != search_item);
                }

                // missing-item search
                {
                    index_item missing_item { 0xDEADBEEE };
                    auto [ found_cnt, found_item ] = reader.find(missing_item);
                    expect(found_cnt == 0) << found_cnt;
                }
            }
            std::filesystem::remove(idx_path_1.path());
            std::filesystem::remove(idx_path_2.path());
        };

        "multi-part indices one item per slice"_test = [] {
            file::tmp idx_path_1 { "index-writer-1-multi-index-test" };
            {
                writer<index_item> idx { idx_path_1 };
                idx.emplace(0x0ULL);
                idx.emplace(0xDEADBEAFULL);
                idx.emplace(0xFFFFFFFFULL);
            }
            file::tmp idx_path_2 { "index-writer-2-multi-index-test" };
            {
                writer<index_item> idx { idx_path_2 };
                idx.emplace(0x11111111ULL);
                idx.emplace(0xDEADBEAFULL);
                idx.emplace(0xEEEEEEEEULL);
            }
            {
                std::array<std::string, 2> paths { idx_path_1, idx_path_2 };
                reader_multi<index_item> reader { paths };
                expect(reader.size() == 6_u);
                index_item search_item { 0xDEADBEAF };
                auto [ found_cnt, found_item ] = reader.find(search_item);
                expect(found_cnt == 2_u);
                expect(found_item == search_item);
                expect(reader.read(found_item));
                expect(found_item == search_item);
            }
            std::filesystem::remove(idx_path_1.path());
            std::filesystem::remove(idx_path_2.path());
        };

        "index metadata"_test = [] {
            file::tmp idx_path { "index-writer-test" };
            {
                writer<index_item> idx { idx_path };
                idx.set_meta("hello", std::string_view { "world!" });
                idx.set_meta("offset", buffer::from<uint64_t>(0xDEADBEAF));
            }
            {
                reader_mt<index_item> reader { idx_path };
                expect(static_cast<std::string_view>(reader.get_meta("hello")) == std::string_view { "world!" });
                auto offset = reader.get_meta("offset").to<uint64_t>();
                expect(offset == 0xDEADBEAF) << offset;
            }
            std::filesystem::remove(idx_path.path());
        };

        "schedule truncate"_test = [] {
            struct item {
                uint8_t a = 0;
                uint8_t b = 0;
                uint64_t offset = 0;

                bool operator<(const item &o) const
                {
                    return a < o.a && b < o.b && offset < o.offset;
                }

                bool index_less(const item &o) const
                {
                    return *this < o;
                }

                bool operator==(const item &o) const
                {
                    return a == o.a && b == o.b && offset == o.offset;
                }
            };

            struct my_chunk_indexer: chunk_indexer_multi_part<item> {
                using chunk_indexer_multi_part<item>::chunk_indexer_multi_part;
            protected:
                void _index(const cardano::block_container &blk) override
                {
                    blk->foreach_tx([&](const auto &tx) {
                        tx.foreach_output([&](const auto &tx_out) {
                            const auto addr = tx_out.addr();
                            if (!addr.has_pay_id()) return;
                            const auto id = addr.pay_id();
                            _idx.emplace_part(id.hash.data()[0] / _part_range, id.hash.data()[0], id.hash.data()[1], blk.offset());
                        });
                    });
                }
            };
            using my_indexer = indexer_offset<item, my_chunk_indexer>;

            file::tmp_directory tmp_idx_dir { "test-index-common" };
            const auto raw_data = zstd::read("./data/chunk-registry/compressed/chunk/977E9BB3D15A5CFF5C5E48617288C5A731DB654C0B42D63627C690CEADC9E1F3.zstd");
            my_indexer idxr { tmp_idx_dir, "myidx" };
            {
                auto ch_idxr = idxr.make_chunk_indexer("update", 0);
                cbor::zero2::decoder dec { raw_data };
                while (!dec.done()) {
                    auto &block_tuple = dec.read();
                    const cardano::block_container blk { numeric_cast<uint64_t>(block_tuple.data_begin() - raw_data.data()), block_tuple };
                    ch_idxr->index(blk);
                }
            }

            uint64_t size1 {};
            {
                auto reader = idxr.make_reader("update-0");
                expect(reader.size() > 0);
                size1 = reader.size();
            }
            {
                idxr.schedule_truncate("update-0", "update-0-half", raw_data.size() / 2);
                scheduler::get().process();
                expect(std::filesystem::exists(idxr.reader_path("update-0-half")));
                expect(std::filesystem::exists(idxr.reader_path("update-0")));
            }
            {
                auto reader = idxr.make_reader("update-0");
                expect(reader.size() == size1);
            }
            {
                auto reader = idxr.make_reader("update-0-half");
                expect(reader.size() > 0);
                expect(reader.size() < size1);
            }
        };
    };
};