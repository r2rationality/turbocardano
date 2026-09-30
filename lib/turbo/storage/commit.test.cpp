/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/json.hpp>
#include "commit.hpp"

using namespace turbo;

namespace {
    namespace fs = std::filesystem;

    fs::path activate(storage::commit_journal &journal)
    {
        const auto preparing = journal.stage("journal-test-location").parent_path().parent_path();
        journal.seal();
        const auto id = preparing.filename().string().substr(std::string { "preparing-" }.size());
        const auto pending = preparing.parent_path() / ("pending-" + id);
        fs::rename(preparing, pending);
        return pending;
    }

    void stage_pair(storage::commit_journal &journal)
    {
        for (const auto *name: { "a", "b" }) {
            file::write(journal.stage(name).string(), std::string { "new-" } + name);
            journal.add(name);
        }
    }
}

suite storage_commit_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "storage::commit_journal"_test = [] {
        "replay after every publication boundary"_test = [] {
            for (unsigned installed = 0; installed <= 2; ++installed) {
                const file::tmp_directory dir { "journal-replay" };
                const fs::path root { dir.path() };
                file::write((root / "a").string(), std::string { "old-a" });
                file::write((root / "b").string(), std::string { "old-b" });
                fs::path pending;
                {
                    storage::commit_journal journal { root };
                    stage_pair(journal);
                    pending = activate(journal);
                    if (installed >= 1)
                        fs::rename(pending / "files/a", root / "a");
                    if (installed >= 2)
                        fs::rename(pending / "files/b", root / "b");
                } // Simulates loss of all process state after the chosen rename.
                expect(throws([&] { storage::commit_journal::recover(root, false); }));
                expect(fs::exists(pending));
                storage::commit_journal::recover(root, true);
                expect_equal(file::read((root / "a").string()), uint8_vector { buffer { std::string_view { "new-a" } } });
                expect_equal(file::read((root / "b").string()), uint8_vector { buffer { std::string_view { "new-b" } } });
                expect(fs::is_empty(root / "transactions"));
                storage::commit_journal::recover(root, true);
                expect(fs::is_empty(root / "transactions"));
            }
        };

        "temporary failure stops and preserves remaining actions"_test = [] {
            const file::tmp_directory dir { "journal-retry" };
            const fs::path root { dir.path() };
            fs::create_directory(root / "b"); // A removable obstruction, independent of permissions.
            {
                storage::commit_journal journal { root };
                stage_pair(journal);
                journal.seal();
                expect(throws([&] { journal.commit(); }));
                expect(journal.decided());
                expect(fs::is_regular_file(root / "a"));
            }
            expect(throws([&] { storage::commit_journal::recover(root, true); }));
            fs::remove(root / "b");
            storage::commit_journal::recover(root, true);
            expect(fs::is_regular_file(root / "b"));
            expect(fs::is_empty(root / "transactions"));
        };

        "replay installations and removals after every rename"_test = [] {
            for (unsigned step = 0; step <= 5; ++step) {
                const file::tmp_directory dir { "journal-remove-replay" };
                const fs::path root { dir.path() };
                fs::create_directory(root / "obsolete");
                file::write((root / "obsolete/a").string(), std::string { "old-a" });
                file::write((root / "obsolete/b").string(), std::string { "old-b" });
                {
                    storage::commit_journal journal { root };
                    stage_pair(journal);
                    journal.remove("obsolete/a");
                    journal.remove("obsolete/a"); // Registration is idempotent.
                    journal.remove("obsolete/b");
                    expect(fs::exists(journal.read("obsolete/a")));
                    const auto pending = activate(journal);
                    if (step >= 1)
                        fs::rename(pending / "files/a", root / "a");
                    if (step >= 2)
                        fs::rename(pending / "files/b", root / "b");
                    if (step >= 3)
                        fs::rename(root / "obsolete/a", pending / "trash/obsolete/a");
                    if (step >= 4)
                        fs::rename(root / "obsolete/b", pending / "trash/obsolete/b");
                    if (step >= 5) {
                        const auto completed = pending.parent_path() / ("completed-" + journal.id());
                        fs::rename(pending, completed);
                        fs::remove(completed / "trash/obsolete/a"); // Interrupted cleanup needs no receipts.
                        fs::remove(completed / "manifest.json");
                    }
                }
                if (step < 5) {
                    expect(throws([&] { storage::commit_journal::recover(root, false); }));
                    expect_equal(fs::exists(root / "obsolete/a"), step < 3);
                }
                storage::commit_journal::recover(root, true);
                expect(fs::is_regular_file(root / "a"));
                expect(fs::is_regular_file(root / "b"));
                expect(fs::is_empty(root / "obsolete"));
                expect(fs::is_empty(root / "transactions"));
                storage::commit_journal::recover(root, true);
            }
        };

        "retry a failed commit on the same journal"_test = [] {
            const file::tmp_directory dir { "journal-live-retry" };
            const fs::path root { dir.path() };
            fs::create_directory(root / "b");
            file::write((root / "old").string(), std::string { "old" });
            storage::commit_journal journal { root };
            stage_pair(journal);
            journal.remove("old");
            expect(throws([&] { journal.commit(); }));
            expect(!journal.decided());
            journal.seal();
            expect(!journal.decided());
            expect(throws([&] { journal.commit(); }));
            expect(journal.decided());
            expect(fs::is_regular_file(root / "a"));
            expect(fs::is_regular_file(root / "old"));
            journal.cleanup(); // A pending commit must survive cleanup.
            expect(fs::exists(root / "transactions" / ("pending-" + journal.id())));
            expect(throws([&] { journal.stage("c"); }));
            expect(throws([&] { journal.add("a"); }));
            expect(throws([&] { journal.remove("old"); }));
            expect(throws([&] { journal.seal(); }));
            fs::remove(root / "b");
            journal.commit();
            expect(journal.decided());
            expect_equal(file::read((root / "b").string()), uint8_vector { buffer { std::string_view { "new-b" } } });
            expect(!fs::exists(root / "old"));
            journal.cleanup();
            journal.commit(); // Completion remains idempotent after cleanup.
            expect(fs::is_empty(root / "transactions"));
        };

        "recovery cleans garbage before a failing commit"_test = [] {
            const file::tmp_directory dir { "journal-recovery-cleanup" };
            const fs::path root { dir.path() };
            storage::commit_journal journal { root };
            stage_pair(journal);
            const auto pending = activate(journal);
            fs::remove(pending / "files/a");
            const auto preparing = root / "transactions/preparing-11111111111111111111111111111111";
            const auto completed = root / "transactions/completed-22222222222222222222222222222222";
            file::write((preparing / "files/unused").string(), std::string { "unused" });
            file::write((completed / "trash/old").string(), std::string { "old" });
            expect(throws([&] { storage::commit_journal::recover(root, false); }));
            expect(fs::exists(preparing));
            expect(fs::exists(completed));
            expect(throws([&] { storage::commit_journal::recover(root, true); }));
            expect(!fs::exists(preparing));
            expect(!fs::exists(completed));
            expect(fs::exists(pending / "files/b"));
            expect(fs::exists(pending / "manifest.json"));
        };

        "removal failure preserves receipts and retries remaining actions"_test = [] {
            const file::tmp_directory dir { "journal-remove-retry" };
            const fs::path root { dir.path() };
            for (const auto *name: { "a", "b", "c" })
                file::write((root / name).string(), std::string { "old" });
            storage::commit_journal journal { root };
            for (const auto *name: { "a", "b", "c" })
                journal.remove(name);
            const auto pending = activate(journal);
            fs::create_directory(pending / "trash/b");
            expect(throws([&] { storage::commit_journal::recover(root, true); }));
            expect(!fs::exists(root / "a"));
            expect(fs::is_regular_file(pending / "trash/a"));
            expect(fs::exists(root / "b"));
            expect(fs::exists(root / "c"));
            fs::remove(pending / "trash/b");
            storage::commit_journal::recover(root, true);
            expect(!fs::exists(root / "b"));
            expect(!fs::exists(root / "c"));
            expect(fs::is_empty(root / "transactions"));
        };

        "removal requires an intact source or receipt"_test = [] {
            for (const bool moved: { false, true }) {
                const file::tmp_directory dir { "journal-remove-receipt" };
                const fs::path root { dir.path() };
                file::write((root / "a").string(), std::string { "old" });
                storage::commit_journal journal { root };
                journal.remove("a");
                const auto pending = activate(journal);
                const auto path = moved ? pending / "trash/a" : root / "a";
                if (moved)
                    fs::rename(root / "a", path);
                file::write(path.string(), std::string { "incorrect size" });
                expect(throws([&] { storage::commit_journal::recover(root, true); }));
                fs::remove(path);
                expect(throws([&] { storage::commit_journal::recover(root, true); }));
                expect(fs::exists(pending / "manifest.json"));
                file::write(path.string(), std::string { "old" });
                storage::commit_journal::recover(root, true);
                expect(!fs::exists(root / "a"));
            }
        };

        "removal is deferred and rejects conflicting outputs"_test = [] {
            const file::tmp_directory dir { "journal-remove-prepare" };
            const fs::path root { dir.path() };
            file::write((root / "a").string(), std::string { "old" });
            {
                storage::commit_journal journal { root };
                journal.remove("a");
                expect(throws([&] { journal.add("a"); }));
                expect(throws([&] { journal.stage("a"); }));
                expect(throws([&] { journal.remove("missing"); }));
                expect(throws([&] { journal.remove("../outside"); }));
                expect(throws([&] { journal.remove("transactions/other"); }));
                fs::create_directory(root / "directory");
                expect(throws([&] { journal.remove("directory"); }));
                journal.seal();
                expect(throws([&] { journal.remove("a"); }));
            } // Abandoning preparation must preserve the live file.
            expect(fs::exists(root / "a"));
            {
                storage::commit_journal journal { root };
                journal.add("a"); // Reused outputs also conflict with removal.
                expect(throws([&] { journal.remove("a"); }));
                file::write(journal.stage("a").string(), std::string { "replacement" });
                journal.add("a"); // Reuse may later become an installation.
                file::write(journal.stage("b").string(), std::string { "new" });
                expect(throws([&] { journal.remove("b"); }));
                journal.seal();
                journal.commit();
                expect_equal(file::read((root / "a").string()), uint8_vector { buffer { std::string_view { "replacement" } } });
            }
            {
                storage::commit_journal journal { root };
                journal.remove("a");
                journal.seal();
                journal.commit();
                journal.commit();
                expect(!fs::exists(root / "a"));
                expect(fs::is_regular_file(root / "transactions" / ("completed-" + journal.id()) / "trash/a"));
                journal.cleanup();
                expect(fs::is_empty(root / "transactions"));
            }
        };

        "invalid removal manifests are rejected before installation"_test = [] {
            for (const auto *invalid: { "../outside", "a", "duplicate" }) {
                const file::tmp_directory dir { "journal-remove-invalid" };
                const fs::path root { dir.path() };
                file::write((root / "old").string(), std::string { "old" });
                storage::commit_journal journal { root };
                stage_pair(journal);
                journal.remove("old");
                const auto pending = activate(journal);
                auto manifest = json::load((pending / "manifest.json").string());
                auto &removals = manifest.at("remove").as_array();
                auto extra = removals.front();
                if (std::string_view { invalid } != "duplicate")
                    extra.at("path") = invalid;
                removals.emplace_back(std::move(extra));
                json::save_pretty((pending / "manifest.json").string(), manifest);
                expect(throws([&] { storage::commit_journal::recover(root, true); }));
                expect(fs::exists(pending / "files/a"));
                expect(!fs::exists(root / "a"));
                expect(fs::exists(root / "old"));
            }
        };

        "staging is unique and abandoned or completed metadata is removed"_test = [] {
            const file::tmp_directory dir { "journal-cleanup" };
            const fs::path root { dir.path() };
            {
                storage::commit_journal first { root }, second { root };
                expect(first.stage("a") != second.stage("a"));
                expect(throws([&] { first.stage("../outside"); }));
                expect(throws([&] { first.stage("transactions/other"); }));
            }
            expect(fs::is_empty(root / "transactions"));
            {
                storage::commit_journal journal { root };
                stage_pair(journal);
                journal.seal();
                journal.commit();
                journal.commit();
            }
            expect(fs::is_empty(root / "transactions"));
            {
                storage::commit_journal journal { root };
                stage_pair(journal);
                const auto pending = activate(journal);
                fs::rename(pending / "files/a", root / "a");
                fs::rename(pending / "files/b", root / "b");
                const auto completed = pending.parent_path() / ("completed-" + pending.filename().string().substr(8));
                fs::rename(pending, completed);
                fs::remove(completed / "manifest.json"); // Cleanup may itself have been interrupted.
            }
            storage::commit_journal::recover(root, true);
            expect(fs::is_empty(root / "transactions"));
        };

        "missing output stops recovery without discarding the journal"_test = [] {
            const file::tmp_directory dir { "journal-missing" };
            const fs::path root { dir.path() };
            storage::commit_journal journal { root };
            stage_pair(journal);
            const auto pending = activate(journal);
            fs::remove(pending / "files/a");
            expect(throws([&] { storage::commit_journal::recover(root, true); }));
            expect(fs::exists(pending / "manifest.json"));
            expect(fs::exists(pending / "files/b"));
        };

        "validate all destinations before publication"_test = [] {
            const file::tmp_directory dir { "journal-invalid" };
            const fs::path root { dir.path() };
            storage::commit_journal journal { root };
            stage_pair(journal);
            const auto pending = activate(journal);
            auto manifest = json::load((pending / "manifest.json").string());
            manifest.at("install").as_array().at(1).at("path") = "../outside";
            json::save_pretty((pending / "manifest.json").string(), manifest);
            expect(throws([&] { storage::commit_journal::recover(root, true); }));
            expect(!fs::exists(root / "a"));
            expect(fs::exists(pending / "files/a"));
        };

        "one writer owns the registry"_test = [] {
            const file::tmp_directory dir { "journal-writer" };
            {
                storage::registry_writer_lock first { dir.path() };
                expect(throws([&] { storage::registry_writer_lock second { dir.path() }; }));
            }
            expect(nothrow([&] { storage::registry_writer_lock reopened { dir.path() }; }));
        };
    };
};
