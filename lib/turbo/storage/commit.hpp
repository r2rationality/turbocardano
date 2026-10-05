#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace turbo::storage {
    struct registry_writer_lock {
        explicit registry_writer_lock(const std::filesystem::path &root);
        ~registry_writer_lock();
        registry_writer_lock(const registry_writer_lock &) =delete;
        registry_writer_lock &operator=(const registry_writer_lock &) =delete;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };

    // One writer owns a registry; readers must not open it during publication.
    // Relative paths use the registry's canonical spelling and case, including
    // uppercase hexadecimal chunk hashes. All files share a filesystem.
    // This protects against process termination, not power loss.
    // Workers may use stage()/read(); after all writers close
    // their outputs, the coordinator registers actions, seals, and commits.
    // preparing-ID -> pending-ID is the commit decision; pending -> completed
    // follows the last installation/removal. Only pending directories are replayed.
    // Preparations may overlap; commits must be serialized, and a pending commit
    // must finish before another starts. The caller holds registry_writer_lock.
    struct commit_journal {
        explicit commit_journal(const std::filesystem::path &root);
        ~commit_journal();
        commit_journal(const commit_journal &) =delete;
        commit_journal &operator=(const commit_journal &) =delete;

        std::filesystem::path stage(const std::filesystem::path &relative) const;
        std::filesystem::path read(const std::filesystem::path &relative) const;
        void add(const std::filesystem::path &relative);
        // Retire an existing regular file (not a symlink) after installations, keeping it readable
        // until commit. The caller must ensure no reader needs its path afterward.
        // Cannot target a staged/added output; repeated removal is a no-op.
        void remove(const std::filesystem::path &relative);
        void seal();
        void commit();
        // Best-effort deletion is outside the rename-only commit and is retried
        // by the destructor/recovery. No transaction history is retained.
        void cleanup() noexcept;
        bool decided() const noexcept { return _phase == phase::pending || _phase == phase::completed; }
        const std::string &id() const noexcept { return _id; }

        // Must precede processor loading and cleanup.
        // Read-only opens reject pending commits without changing the directory.
        // Writable recovery requires registry_writer_lock and no active journals:
        // every preparing directory is treated as abandoned.
        static void recover(const std::filesystem::path &root, bool writable);
    private:
        enum class phase { preparing, sealed, pending, completed };
        const std::filesystem::path _root;
        const std::string _id;
        std::filesystem::path _directory;
        std::map<std::filesystem::path, uint64_t> _install;
        std::set<std::filesystem::path> _reused;
        std::map<std::filesystem::path, uint64_t> _removed;
        phase _phase = phase::preparing;
    };
}
