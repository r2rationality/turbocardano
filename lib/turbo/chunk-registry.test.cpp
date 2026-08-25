/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>
#include <turbo/common/test.hpp>
#include <turbo/json.hpp>

namespace {
    using namespace turbo;
    using boost::ext::ut::v2_1_0::nothrow;

    void copy_chunk(chunk_registry &dst_cr, const chunk_registry &src_cr, const storage::chunk_info &chunk)
    {
        const auto dst_path = dst_cr.full_path(chunk.rel_path());
        const auto src_path = src_cr.full_path(chunk.rel_path());
        std::filesystem::remove(dst_path);
        std::filesystem::copy(src_path, dst_path);
        dst_cr.add_file(dst_cr.num_bytes(), dst_path);
    }
}

suite chunk_registry_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "chunk_registry"_test = [] {
        static std::string data_dir = install_path("./data/chunk-registry");
        static std::string tmp_data_dir = install_path("./tmp/chunk-registry");

        const auto clear_tmp_data_dir = [&] {
            std::filesystem::remove_all(tmp_data_dir);
            std::filesystem::create_directories(tmp_data_dir);
        };

        "chang"_test = [&] {
            clear_tmp_data_dir();
            std::filesystem::create_directories(tmp_data_dir + "/chang");
            std::filesystem::copy(install_path("./data/chunk-registry/chang"), tmp_data_dir + "/chang");
            chunk_registry cr { tmp_data_dir, chunk_registry::mode::index };
            const std::string local_path = install_path(tmp_data_dir + "/chang/9326B83719AEAB06A671EA653EE297F1DA601A4FC279A759503D79F55DA6EEC7.zstd");
            const progress_point target_tip { 133703981, 10746832 };
            const auto ex_ptr = cr.accept_progress({}, target_tip, [&] {
                cr.add_file(0, local_path);
            });
            expect_equal(target_tip.end_offset, cr.num_bytes());
        };

        const auto recreate_tmp_data_dir = [&] {
            clear_tmp_data_dir();
            std::filesystem::copy(data_dir, tmp_data_dir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
        };

        "strict creation"_test = [&] {
            recreate_tmp_data_dir();
            file_remover fr {};
            expect(nothrow([&] {
                chunk_registry cr {
                    tmp_data_dir,
                    chunk_registry::mode::validate,
                    cardano::config::get(),
                    scheduler::get(),
                    fr
                };
                expect(cr.empty());
                expect_equal(0, cr.num_bytes());
            }));
            recreate_tmp_data_dir();
            expect(nothrow([&] { chunk_registry cr { tmp_data_dir, chunk_registry::mode::store }; }));
        };

        "empty"_test = [&] {
            std::filesystem::remove_all(tmp_data_dir);
            chunk_registry cr { tmp_data_dir, chunk_registry::mode::validate };
            expect(cr.num_bytes() == 0_ull);
            expect(cr.num_compressed_bytes() == 0_ull);
            expect(cr.max_slot() == 0_ull);
            expect(cr.empty());
            expect(!cr.has_epoch(0));
            expect(cr.epochs().empty());
            expect(&cr.sched() == &scheduler::get());
            expect(&cr.remover() == &file_remover::get());
            expect(cr.data_dir() == tmp_data_dir);
            expect(!cr.tx());
        };

        // access
        {
            chunk_registry cr { data_dir, chunk_registry::mode::store };
            "create chunk registry"_test = [&cr] {
                expect(!cr.empty());
                expect(cr.num_bytes() == 175'115'499_u) << cr.num_bytes();
            };
            "find"_test = [&cr] {
                expect(!cr.empty());
                const auto rel_path = cr.find_offset(100'000'000).rel_path();
                expect_equal(rel_path, "chunk/47F62675C9B0161211B9261B7BB1CF801EDD4B9C0728D9A6C7A910A1581EED41.zstd");
                expect(throws([&cr] { cr.find_offset(200'000'000); }));
                expect(cr.find_block_by_offset(100'000'000).offset == 99'936'542_ull);
                expect(throws([&cr] { cr.find_block_by_offset(200'000'000); }));
                expect(cr.find_last_block_hash(cardano::block_hash::from_hex("EF282E85A8EF8A9C31D255C736F52AA0D52BEA260276BF2FB4AF3ADB700D0F1B")).offset == 84'430'954_ull);
                expect(throws([&cr] { cr.find_last_block_hash(cardano::block_hash {}); }));
                expect(cr.find_block_by_offset(100'000'000).slot == 71420546ULL) << fmt::format("{}", cr.find_block_by_offset(100'000'000).slot);
                expect(throws([&cr] { cr.find_block_by_offset(200'000'000); }));
                expect(cr.find_block_by_slot(71431152).offset == 120'772'796_ull);
                expect(throws([&cr] { cr.find_block_by_slot(72'000'000); }));
                expect(cr.find_offset_it(100'000'000)->second.offset == 84'430'954_ull);
                expect(throws([&cr] { cr.find_offset_it(200'000'000); }));
            };
            "latest_block_after_slot"_test = [&] {
                if (const auto blk = cr.latest_block_after_or_at_slot(0); blk != cr.cend()) {
                    expect_equal(0, blk->slot);
                } else {
                    expect(false);
                }
                if (const auto blk = cr.latest_block_after_or_at_slot(cr.max_slot()); blk != cr.cend()) {
                    expect_equal(cr.max_slot(), blk->slot );
                } else {
                    expect(false);
                }
                const uint64_t mid_slot = cr.max_slot() / 2;
                if (const auto blk = cr.latest_block_after_or_at_slot(mid_slot); blk != cr.cend()) {
                    expect(blk->slot >= mid_slot) << fmt::format("{}", blk->point()) << mid_slot;
                } else {
                    expect(false);
                }
            };
            "latest_block_before_slot"_test = [&] {
                if (const auto blk = cr.latest_block_before_or_at_slot(0); blk != cr.cend()) {
                    expect_equal(0, blk->slot);
                } else {
                    expect(false);
                }
                if (const auto blk = cr.latest_block_before_or_at_slot(cr.max_slot()); blk != cr.cend()) {
                    expect(blk->slot <= cr.max_slot()) << fmt::format("{}", blk->point()) << cr.max_slot();
                    expect(cr.max_slot() - blk->slot < 400) << fmt::format("{}", blk->point()) << cr.max_slot();
                } else {
                    expect(false);
                }
                const uint64_t mid_slot = cr.max_slot() / 2;
                if (const auto blk = cr.latest_block_before_or_at_slot(mid_slot); blk != cr.cend()) {
                    expect(blk->slot <= mid_slot) << fmt::format("{}", blk->point()) << mid_slot;
                    // test chain is sparse so the closeness transition is expected to fail
                    // expect(cr.max_slot() - blk->slot < 400) << fmt::format("{}", blk->point()) << cr.max_slot();
                } else {
                    expect(false);
                }
            };
            "read"_test = [&cr] {
                auto block_tuple_pv = cr.read(28'762'567);
                auto &block_tuple = block_tuple_pv.get();
                expect_equal(cbor::major_type::array, block_tuple.type());
                expect_equal(false, block_tuple.indefinite());
                expect_equal(2, block_tuple.special_uint());
            };
        }
        {
            chunk_registry cr { tmp_data_dir, chunk_registry::mode::store };
            "full_path"_test = [&] {
                auto exp = std::filesystem::weakly_canonical(std::filesystem::absolute(tmp_data_dir) / "compressed/some-dir/some-file.ext");
                auto act = cr.full_path("some-dir/some-file.ext");
                expect(exp == act) << act;
                auto dir_path = cr.full_path("some-dir");
                expect(std::filesystem::exists(dir_path));
                std::filesystem::remove_all(dir_path);
                expect(throws([&] { cr.full_path("../../../../../etc/passwd"); }));
            };
            "rel_path"_test = [&] {
                auto full_path = std::filesystem::weakly_canonical(std::filesystem::absolute(tmp_data_dir) / "compressed/some-dir/some-file.ext");
                auto exp = std::filesystem::path { "some-dir/some-file.ext" }.make_preferred().string();
                auto act = cr.rel_path(full_path);
                expect(exp == act) << act;
                expect(throws([&] { cr.rel_path(std::filesystem::weakly_canonical("./data2/another-file.txt")); }));
            };
        }

        "truncate chunk boundary"_test = [&] {
            recreate_tmp_data_dir();
            chunk_registry cr { tmp_data_dir, chunk_registry::mode::store };
            const auto before_tip = cr.tip();
            const auto before_size = cr.num_bytes();
            const auto before_slot = cr.max_slot();
            cr.truncate(before_tip);
            expect(before_size == cr.num_bytes());
            const auto mid_point = cr.find_block_by_offset(before_size / 2).point();
            expect(mid_point < before_tip);
            cr.truncate(mid_point);
            expect(cr.tip() == mid_point);
            expect(cr.max_slot() < before_slot);
            cr.truncate({});
            expect_equal(cr.num_bytes(), 0);
            expect_equal(cr.max_slot(), 0);
            expect(cr.empty());
        };

        "truncate block boundary"_test = [&] {
            recreate_tmp_data_dir();
            chunk_registry cr { tmp_data_dir, chunk_registry::mode::store };
            const auto before_size = cr.num_bytes();
            const auto before_slot = cr.max_slot();
            const auto before_blocks = cr.num_blocks();
            expect_equal(175115499, before_size);
            const auto last_chunk = !cr.empty();
            expect(!!last_chunk);
            if (last_chunk) {
                const auto new_tip = (cr.cend() - 2)->point();
                cr.truncate(new_tip);
                expect_equal(cr.tip(), new_tip);
                expect(cr.num_blocks() == before_blocks - 1);
                expect_equal(74044763, cr.max_slot());
                expect(cr.max_slot() != before_slot);
            }
        };

        "epoch-level auto-merge"_test = [&] {
            std::filesystem::remove_all(tmp_data_dir);
            // 0, 1, 2, 1, 0, 2, 3
            std::set<uint64_t> epochs {};
            size_t num_bytes = 0;
            static const std::string src_dir { "./data/chunk-registry-new" };
            auto j_chunks = turbo::json::load(src_dir + "/epoch-merge.json").as_array();
            chunk_processor proc {
                .on_epoch_update = [&](const auto epoch, const auto &info) {
                    epochs.emplace(epoch);
                    num_bytes += info.size();
                }
            };
            chunk_registry cr { tmp_data_dir, chunk_registry::mode::store };
            cr.register_processor(proc);
            expect(epochs.empty());

            cardano::point target_tip {
                cardano::block_hash::from_hex("CB7B006C985635E9197FE7D005E1598391AA2CA1DD507543743E416E84B2F1B9"),
                79999, 79999
            };
            {
                uint64_t target_offset = 0;
                for (const auto &j_chunk: j_chunks) {
                    auto src_path = fmt::format("{}/{}", src_dir, static_cast<std::string_view>(j_chunk.at("relPath").as_string()));
                    target_offset += std::filesystem::file_size(src_path);
                }
                target_tip.end_offset = target_offset;
            }

            const auto ex_ptr = cr.accept_progress({}, target_tip, [&]{
                for (const auto &j_chunk: j_chunks) {
                    std::string orig_rel_path { static_cast<std::string_view>(j_chunk.at("relPath").as_string()) };
                    const auto src_path = fmt::format("{}/{}", src_dir, orig_rel_path);
                    const auto offset = turbo::json::value_to<uint64_t>(j_chunk.at("offset"));
                    const auto local_path = cr.full_path(orig_rel_path);
                    const auto data = file::read(src_path);
                    const auto compressed = zstd::compress(data);
                    file::write(local_path, compressed);
                    cr.add_file(offset, local_path, zstd::default_compression_level);
                    auto exp_max_epoch = turbo::json::value_to<uint64_t>(j_chunk.at("expMaxEpoch"));
                    auto act_max_epoch = epochs.empty() ? 0 : *epochs.rbegin();
                    expect_equal(act_max_epoch, exp_max_epoch);
                }
            });
            expect(!cr.tx());
            expect(!ex_ptr);
            expect_equal(2602139, num_bytes);
            if (!epochs.empty())
                expect(3 == *epochs.rbegin()) << fmt::format("{}", epochs);

            expect_equal(7, cr.chunks().size());
            const auto before_repack_size = cr.num_bytes();
            const auto before_repack_blocks = cr.num_blocks();
            chunk_registry::file_set old_chunk_paths {};
            for (const auto &[last_byte_offset, chunk]: cr.chunks())
                old_chunk_paths.emplace(cr.full_path(chunk.rel_path()));
            const auto old_repack_dir = std::filesystem::path { tmp_data_dir } / "compressed" / ".repack-old";
            std::filesystem::create_directories(old_repack_dir);
            const auto repack_stats = cr.repack();
            expect_equal(7, repack_stats.chunks_analyzed);
            expect_equal(3, repack_stats.chunks_repacked);
            expect_equal(3, repack_stats.partial_groups_merged);
            expect_equal(4, cr.chunks().size());
            expect_equal(before_repack_size, cr.num_bytes());
            expect_equal(before_repack_blocks, cr.num_blocks());
            expect(!std::filesystem::exists(old_repack_dir));
            expect(!std::filesystem::exists(std::filesystem::path { tmp_data_dir } / "compressed" / "repack"));
            std::optional<uint64_t> prev_chunk_id {};
            for (const auto &[last_byte_offset, chunk]: cr.chunks()) {
                const auto chunk_id = cr.make_slot(chunk.first_slot).chunk_id();
                if (prev_chunk_id)
                    expect(*prev_chunk_id != chunk_id);
                prev_chunk_id = chunk_id;
                expect_equal(zstd::default_compression_level, chunk.compression_level);
                old_chunk_paths.erase(cr.full_path(chunk.rel_path()));
            }
            for (const auto &old_chunk_path: old_chunk_paths)
                expect(!std::filesystem::exists(old_chunk_path));
            chunk_registry reloaded { tmp_data_dir, chunk_registry::mode::store };
            expect_equal(4, reloaded.chunks().size());
            expect_equal(cr.num_bytes(), reloaded.num_bytes());
            for (const auto &[last_byte_offset, chunk]: reloaded.chunks())
                expect_equal(zstd::default_compression_level, chunk.compression_level);
            const auto second_repack_stats = reloaded.repack();
            expect_equal(4, second_repack_stats.chunks_analyzed);
            expect_equal(0, second_repack_stats.chunks_repacked);
            expect_equal(0, second_repack_stats.partial_groups_merged);
            expect_equal(second_repack_stats.compressed_size_before, second_repack_stats.compressed_size_after);
            expect(!std::filesystem::exists(std::filesystem::path { tmp_data_dir } / "compressed" / "repack"));

            std::map<std::string, uint64_t> repaired_paths {};
            for (const auto &[last_byte_offset, chunk]: reloaded.chunks()) {
                if (repaired_paths.size() >= 2)
                    break;
                const auto path = reloaded.full_path(chunk.rel_path());
                const auto compressed = zstd::compress(zstd::read(path), 3);
                if (compressed.size() == chunk.compressed_size)
                    continue;
                file::write(path, compressed);
                repaired_paths.try_emplace(path, compressed.size());
            }
            expect_equal(2, repaired_paths.size());

            chunk_registry recovered { tmp_data_dir, chunk_registry::mode::store };
            expect_equal(reloaded.chunks().size(), recovered.chunks().size());
            expect_equal(reloaded.num_bytes(), recovered.num_bytes());
            for (const auto &[last_byte_offset, chunk]: recovered.chunks()) {
                const auto path = recovered.full_path(chunk.rel_path());
                if (const auto repaired_it = repaired_paths.find(path); repaired_it != repaired_paths.end()) {
                    expect_equal(repaired_it->second, chunk.compressed_size);
                    expect_equal(0, chunk.compression_level);
                } else {
                    expect_equal(zstd::default_compression_level, chunk.compression_level);
                }
            }

            chunk_registry repair_reloaded { tmp_data_dir, chunk_registry::mode::store };
            expect_equal(recovered.chunks().size(), repair_reloaded.chunks().size());
            for (const auto &[last_byte_offset, chunk]: repair_reloaded.chunks()) {
                const auto path = repair_reloaded.full_path(chunk.rel_path());
                if (const auto repaired_it = repaired_paths.find(path); repaired_it != repaired_paths.end()) {
                    expect_equal(repaired_it->second, chunk.compressed_size);
                    expect_equal(0, chunk.compression_level);
                } else {
                    expect_equal(zstd::default_compression_level, chunk.compression_level);
                }
            }

            const auto invalid_path = repair_reloaded.full_path(repair_reloaded.chunks().begin()->second.rel_path());
            file::write(invalid_path, uint8_vector { 0 });
            expect(throws([&] {
                chunk_registry invalid { tmp_data_dir, chunk_registry::mode::store };
            }));
        };
        "epoch info"_test = [&] {
            expect(throws([]{
                epoch_info::chunk_list no_chunks {};
                epoch_info { std::move(no_chunks) };
            }));
            chunk_registry cr { data_dir, chunk_registry::mode::store };
            uint64_t total_size = 0;
            uint64_t total_compressed_size = 0;
            std::optional<cardano::block_hash> last_block_hash {};
            std::optional<uint64_t> last_slot {};
            for (const auto &[epoch, einfo]: cr.epochs()) {
                // need a strict test data set to re-enable the following check
                // if (last_block_hash)
                //    expect(*last_block_hash == einfo.prev_block_hash()) << epoch;
                last_block_hash = einfo.last_block_hash();
                if (last_slot)
                    expect(einfo.last_slot() >= *last_slot);
                last_slot = einfo.last_slot();
                expect(einfo.start_offset() == total_size);
                total_size += einfo.size();
                expect(einfo.end_offset() == total_size);
                expect(einfo.size() == einfo.end_offset() - einfo.start_offset());
                total_compressed_size += einfo.compressed_size();
            }
            expect(cr.num_bytes() == total_size);
            expect(cr.num_compressed_bytes() == total_compressed_size);
            expect(static_cast<bool>(last_block_hash));
            expect(!cr.empty());
            if (last_block_hash && cr.tip())
                expect(*last_block_hash == cr.tip()->hash);
            if (last_slot)
                expect(*last_slot == cr.max_slot());
        };
        "progress despite errors and rollback"_test = [&] {
            chunk_registry src_cr { data_dir, chunk_registry::mode::store };
            std::filesystem::remove_all(tmp_data_dir);
            chunk_registry dst_cr { tmp_data_dir, chunk_registry::mode::store };
            expect(dst_cr.empty());
            expect(!!dst_cr.accept_progress({}, src_cr.tip(), [&] {
                throw error("something went wrong");
                copy_chunk(dst_cr, src_cr, src_cr.chunks().begin()->second);
            }));
            expect(dst_cr.empty());
            expect(!dst_cr.tx());
            expect(!dst_cr.accept_progress({}, src_cr.tip(), [&] {
                copy_chunk(dst_cr, src_cr, src_cr.chunks().begin()->second);
            }));
            expect(!dst_cr.tx());
            expect(!dst_cr.empty());
            expect(!!dst_cr.accept_progress({}, src_cr.tip(), [&] {
                copy_chunk(dst_cr, src_cr, src_cr.chunks().begin()->second);
                throw error("something went wrong");
                copy_chunk(dst_cr, src_cr, (++src_cr.chunks().begin())->second);
            }));
            expect(!dst_cr.tx());
            expect(!dst_cr.empty());
        };
    };
};
