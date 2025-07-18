#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <chrono>
#include <turbo/common/logger.hpp>

namespace turbo {
    struct file_remover {
        using deleted_list = std::vector<std::string>;
        using time_point = std::chrono::time_point<std::chrono::system_clock>;
        using remove_point_map = std::map<std::string, time_point>;

        static file_remover &get()
        {
            static file_remover fr {};
            return fr;
        }

        explicit file_remover() =default;

        [[nodiscard]] size_t size() const
        {
            return _removable.size();
        }

        void mark(const std::string &path, std::optional<std::chrono::seconds> delay={})
        {
            if (!delay)
                delay = _remove_delay;
            const std::chrono::time_point<std::chrono::system_clock> &when=std::chrono::system_clock::now() + *delay;
            if (auto [it, created] = _removable.emplace(path, when); !created)
                it->second = when;
        }

        void unmark(const std::string &path)
        {
            //logger::debug("removed a file from the future deletion list: {}", path);
            _removable.erase(path);
        }

        void remove_delay(const std::chrono::seconds &new_delay)
        {
            _remove_delay = new_delay;
            logger::debug("default file_remover::remove_delay set to {}", new_delay);
        }

        const std::chrono::seconds &remove_delay() const
        {
            return _remove_delay;
        }

        void remove()
        {
            const auto delete_point = std::chrono::system_clock::now();
            for (auto it = _removable.begin(); it != _removable.end(); ) {
                if (it->second < delete_point) {
                    _remove(it->first, fmt::format("deref: when: {} delete_point: {}",
                        it->second.time_since_epoch().count(), delete_point.time_since_epoch().count()));
                    it = _removable.erase(it);
                } else {
                    ++it;
                }
            }
        }

        void mark_old_files(const std::filesystem::path &dir_path, const std::chrono::seconds &lifespan)
        {
            if (std::filesystem::exists(dir_path)) {
                const auto file_now = std::chrono::file_clock::now();
                for (auto &entry: std::filesystem::directory_iterator(dir_path)) {
                    if (entry.is_regular_file()) [[likely]] {
                        if (const auto file_age = std::chrono::duration_cast<std::chrono::seconds>(file_now - entry.last_write_time()); file_age > lifespan)
                            mark(entry.path().string(), std::chrono::seconds { 0 });
                    }
                }
            }
        }

        const remove_point_map &removable() const
        {
            return _removable;
        }
    private:
        std::chrono::seconds _remove_delay { 0 };
        remove_point_map _removable {};

        static void _remove(const std::string &path, const std::string &note)
        {
            logger::debug("removing obsolete file: {} note: {}", path, note);
            std::filesystem::remove(path);
        }
    };
}