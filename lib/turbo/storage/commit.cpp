/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <random>
#include <mutex>
#include <boost/interprocess/sync/file_lock.hpp>
#include <turbo/common/logger.hpp>
#include <turbo/json.hpp>
#include "commit.hpp"

namespace turbo::storage {
    namespace {
        namespace fs = std::filesystem;

        std::mutex writers_mutex{};
        std::set<fs::path> writers{};

        [[nodiscard]] std::string new_id()
        {
            std::random_device random{};
            return fmt::format("{:08x}{:08x}{:08x}{:08x}", random(), random(), random(), random());
        }

        [[nodiscard]] fs::path checked_path(const fs::path &path)
        {
            if (path.empty() || path.has_root_path() || path != path.lexically_normal())
                throw error("invalid commit journal path: '{}'", path);
            for (const auto &part: path) {
                if (part == "." || part == ".." || part.empty())
                    throw error("invalid commit journal path component '{}' in '{}'", part, path);
            }
            if (*path.begin() == "transactions")
                throw error("commit destination '{}' cannot contain transaction journals", path);
            return path;
        }

        void discard(const fs::path &directory) noexcept
        {
            // A failed deletion is harmless. The next writer retries it before
            // starting work; completed journals are never installed again.
            logger::run_log_errors([&] { fs::remove_all(directory); });
        }

        using file_actions = std::map<fs::path, uint64_t>;

        struct commit_manifest {
            std::string id;
            file_actions installs{};
            file_actions removals{};
        };

        [[nodiscard]] commit_manifest load_commit(const fs::path &directory)
        try {
            const auto manifest = json::load((directory / "manifest.json").string());
            const auto version = json::value_to<uint64_t>(manifest.at("version"));
            if (version != 1) [[unlikely]]
                throw error("invalid commit journal version: {}; expected 1", version);
            auto id = json::value_to<std::string>(manifest.at("txid"));
            if (id.size() != 32 || id.find_first_not_of("0123456789abcdef") != std::string::npos) [[unlikely]]
                throw error("invalid commit journal txid: '{}'; expected 32 lowercase hexadecimal characters", id);
            if (directory.filename() != "pending-" + id) [[unlikely]]
                throw error("invalid commit journal directory: {}; expected directory name pending-{}", directory, id);

            // Validate the entire manifest before performing any rename.
            file_actions installs{};
            for (const auto &item: manifest.at("install").as_array()) {
                auto path = checked_path(json::value_to<std::string>(item.at("path")));
                const auto [it, created] = installs.try_emplace(std::move(path), json::value_to<uint64_t>(item.at("size")));
                if (!created) [[unlikely]]
                    throw error("duplicate commit journal destination: {}", it->first);
            }
            file_actions removals{};
            for (const auto &item: manifest.at("remove").as_array()) {
                auto path = checked_path(json::value_to<std::string>(item.at("path")));
                if (installs.contains(path)) [[unlikely]]
                    throw error("commit journal both installs and removes '{}'", path);
                const auto [it, created] = removals.try_emplace(std::move(path), json::value_to<uint64_t>(item.at("size")));
                if (!created) [[unlikely]]
                    throw error("duplicate commit journal removal: {}", it->first);
            }
            return {std::move(id), std::move(installs), std::move(removals)};
        } catch (const std::exception &ex) {
            throw error(fmt::format("cannot load commit journal '{}'", directory), ex);
        }

        [[nodiscard]] fs::path apply_commit(const fs::path &root, const fs::path &directory,
            const std::string &id, const file_actions &installs, const file_actions &removals)
        try {
            auto completed = directory.parent_path() / ("completed-" + id);
            for (const auto &[path, size]: installs) {
                const auto source = directory / "files" / path;
                const auto destination = root / path;
                const auto source_status = fs::status(source);
                const bool installed = !fs::exists(source_status);
                const auto &output = installed ? destination : source;
                if (!fs::is_regular_file(installed ? fs::status(destination) : source_status)) [[unlikely]]
                    throw error("transaction output '{}' is missing or is not a regular file; expected {} bytes", output, size);
                if (const auto actual_size = fs::file_size(output); actual_size != size) [[unlikely]]
                    throw error("transaction output '{}' has size {}; expected {} bytes", output, actual_size, size);
                if (installed) {
                    // Only this executor consumes staged sources. With one
                    // writer, absence therefore identifies a completed rename.
                    continue;
                }
                fs::rename(source, destination);
            }
            for (const auto &[path, size]: removals) {
                const auto source = root / path;
                const auto trash = directory / "trash" / path;
                const auto source_status = fs::symlink_status(source);
                const auto trash_status = fs::symlink_status(trash);
                const bool removed = fs::exists(trash_status);
                if (fs::exists(source_status) == removed) [[unlikely]]
                    throw error("commit removal requires exactly one of live '{}' and trash '{}' to exist", source, trash);
                const auto &file = removed ? trash : source;
                if (!fs::is_regular_file(removed ? trash_status : source_status)) [[unlikely]]
                    throw error("commit removal '{}' is not a regular file; expected {} bytes", file, size);
                if (const auto actual_size = fs::file_size(file); actual_size != size) [[unlikely]]
                    throw error("commit removal '{}' has size {}; expected {} bytes", file, actual_size, size);
                // Trash is the receipt for a completed rename. Keep it until
                // all actions finish; cleanup only consumes completed journals.
                if (removed)
                    continue;
                fs::rename(source, trash);
            }
            fs::rename(directory, completed);
            return completed;
        } catch (const std::exception &ex) {
            throw error(fmt::format("cannot apply commit journal '{}' to registry '{}'", directory, root), ex);
        }
    }

    commit_journal::commit_journal(const fs::path &root):
        _root{fs::weakly_canonical(root)},
        _id{new_id()},
        _directory{_root / "transactions" / ("preparing-" + _id)}
    {
        fs::create_directories(_directory.parent_path());
        if (!fs::create_directory(_directory)) [[unlikely]]
            throw error("transaction ID {} already exists at '{}'", _id, _directory);
    }

    commit_journal::~commit_journal()
    {
        if (_phase != phase::pending)
            discard(_directory);
    }

    void commit_journal::cleanup() noexcept
    {
        if (_phase == phase::completed)
            discard(_directory);
    }

    fs::path commit_journal::stage(const fs::path &relative) const
    {
        if (_phase != phase::preparing) [[unlikely]]
            throw error("cannot stage '{}' after sealing transaction '{}'", relative, _directory);
        const auto checked = checked_path(relative);
        if (_removed.contains(checked))
            throw error("cannot stage '{}' marked for removal in transaction '{}'", relative, _directory);
        auto path = _directory / "files" / checked;
        fs::create_directories(path.parent_path());
        path.make_preferred();
        return path;
    }

    fs::path commit_journal::read(const fs::path &relative) const
    {
        const auto path = checked_path(relative);
        const auto staged = _directory / "files" / path;
        auto result = fs::exists(staged) ? staged : _root / path;
        result.make_preferred();
        return result;
    }

    void commit_journal::add(const fs::path &relative)
    {
        if (_phase != phase::preparing)
            throw error("cannot add '{}' to sealed transaction '{}'", relative, _directory);
        const auto path = checked_path(relative);
        if (_removed.contains(path))
            throw error("cannot add '{}' marked for removal in transaction '{}'", relative, _directory);
        if (_install.contains(path))
            return;
        const auto source = _directory / "files" / path;
        const auto source_status = fs::status(source);
        if (!fs::exists(source_status)) {
            if (!fs::is_regular_file(_root / path))
                throw error("missing transaction output: neither staged '{}' nor live '{}' is a regular file", source, _root / path);
            _reused.emplace(path);
            return; // Reused immutable output already lives in the registry.
        }
        if (!fs::is_regular_file(source_status))
            throw error("transaction output '{}' is not a regular file", source);
        fs::create_directories((_root / path).parent_path());
        _install.emplace(path, fs::file_size(source));
        _reused.erase(path);
    }

    void commit_journal::remove(const fs::path &relative)
    {
        if (_phase != phase::preparing)
            throw error("cannot remove '{}' from sealed transaction '{}'", relative, _directory);
        const auto path = checked_path(relative);
        if (_removed.contains(path))
            return;
        if (_install.contains(path) || _reused.contains(path) || fs::exists(_directory / "files" / path))
            throw error("cannot remove staged or added output '{}' in transaction '{}'", relative, _directory);
        const auto source = _root / path;
        if (!fs::is_regular_file(fs::symlink_status(source)))
            throw error("commit removal '{}' is missing or is not a regular file", source);
        const auto size = fs::file_size(source);
        fs::create_directories((_directory / "trash" / path).parent_path());
        _removed.emplace(path, size);
    }

    void commit_journal::seal()
    {
        if (_phase != phase::preparing) [[unlikely]]
            throw error("transaction '{}' is already sealed", _directory);
        json::array installs, removals;
        installs.reserve(_install.size());
        removals.reserve(_removed.size());
        for (const auto &[path, size]: _install)
            installs.emplace_back(json::object { { "path", path.generic_string() }, { "size", size } });
        for (const auto &[path, size]: _removed)
            removals.emplace_back(json::object { { "path", path.generic_string() }, { "size", size } });
        json::save_pretty((_directory / "manifest.json").string(), json::object{
            {"version", 1}, {"txid", _id}, {"install", std::move(installs)}, {"remove", std::move(removals)}
        });
        _phase = phase::sealed;
    }

    void commit_journal::commit()
    {
        if (_phase == phase::preparing) [[unlikely]]
            throw error("cannot commit unsealed transaction '{}'", _directory);
        if (_phase == phase::completed)
            return;
        if (_phase == phase::sealed) {
            auto pending = _directory.parent_path() / ("pending-" + _id);
            fs::rename(_directory, pending);
            _directory.swap(pending);
            _phase = phase::pending;
        }
        auto completed = apply_commit(_root, _directory, _id, _install, _removed);
        _directory.swap(completed);
        _phase = phase::completed;
    }

    void commit_journal::recover(const fs::path &root, const bool writable)
    {
        const auto transactions = root / "transactions";
        if (!fs::exists(transactions))
            return;
        std::vector<fs::path> pending{}, garbage{};
        for (const auto &entry: fs::directory_iterator(transactions)) {
            const auto name = entry.path().filename().string();
            if (!entry.is_directory())
                throw error("unexpected non-directory entry '{}' in transaction directory '{}'", entry.path(), transactions);
            if (name.starts_with("pending-")) {
                pending.push_back(entry.path());
            } else if (name.starts_with("completed-") || name.starts_with("preparing-")) {
                garbage.push_back(entry.path());
            } else [[unlikely]] {
                throw error("unexpected transaction directory '{}'; expected preparing-, pending-, or completed- prefix", entry.path());
            }
        }
        if (pending.size() > 1) [[unlikely]]
            throw error("{} pending transactions in '{}' require intervention; expected at most one", pending.size(), transactions);
        if (!writable && !pending.empty()) [[unlikely]]
            throw error("pending commit '{}' requires opening registry '{}' in validation mode", pending.front(), root);
        if (!writable)
            return;
        for (const auto &path: garbage)
            discard(path);
        for (const auto &path: pending) {
            const auto manifest = load_commit(path);
            discard(apply_commit(root, path, manifest.id, manifest.installs, manifest.removals));
        }
    }

    struct registry_writer_lock::impl {
        const fs::path root;
        std::unique_ptr<boost::interprocess::file_lock> lock{};

        explicit impl(const fs::path &directory): root{fs::canonical(directory)}
        {
            std::scoped_lock guard{writers_mutex};
            if (!writers.emplace(root).second) [[unlikely]]
                throw error("registry '{}' already has a writer in this process", root);
            try {
                const auto path = (root / "writer.lock").string();
                auto *file = std::fopen(path.c_str(), "ab");
                if (!file) [[unlikely]]
                    throw error_sys(fmt::format("cannot open registry writer lock '{}'", path));
                if (std::fclose(file) != 0) [[unlikely]]
                    throw error_sys(fmt::format("cannot close registry writer lock '{}'", path));
                lock = std::make_unique<boost::interprocess::file_lock>(path.c_str());
                if (!lock->try_lock()) [[unlikely]]
                    throw error("registry '{}' already has a writer; cannot acquire lock '{}'", root, path);
            } catch (const std::exception &ex) {
                writers.erase(root);
                throw error(fmt::format("cannot acquire registry writer lock '{}'", root / "writer.lock"), ex);
            } catch (...) {
                writers.erase(root);
                throw;
            }
        }

        ~impl()
        {
            std::scoped_lock guard{writers_mutex};
            lock.reset();
            writers.erase(root);
        }
    };

    registry_writer_lock::registry_writer_lock(const fs::path &root):
        _impl { std::make_unique<impl>(root) }
    {}

    registry_writer_lock::~registry_writer_lock() =default;
}
