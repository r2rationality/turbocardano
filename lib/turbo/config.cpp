/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <filesystem>
#include <turbo/config.hpp>
#include <turbo/common/logger.hpp>

namespace turbo {
    static bool install_dir_ok(const std::filesystem::path &dir)
    {
        if (!std::filesystem::exists(dir / "etc" / "mainnet" / "config.json") && !std::getenv("DT_ETC"))
            return false;
        if (!std::filesystem::exists(dir / "log") && !std::getenv("DT_ETC"))
            return false;
        return true;
    }

    // Must be configured before any multi-threading code is executed
    static std::filesystem::path install_dir(const std::optional<std::filesystem::path> &override_dir={})
    {
        static std::optional<std::filesystem::path> dir {};
        if (override_dir) {
             if (install_dir_ok(*override_dir)) {
                 dir.emplace(*override_dir);
                 std::cerr << fmt::format("DT_INIT: install dir: {} resolved using the binary-relative path\n", dir);
            }
        }
        if (!dir) {
            auto default_dir = std::filesystem::absolute(std::filesystem::current_path());
            if (!install_dir_ok(default_dir)) {
                std::cerr << fmt::format("DT_INIT: cannot find required configuration files in {}\n", default_dir);
                std::terminate();
            }
            dir.emplace(std::move(default_dir));
            std::cerr << fmt::format("DT_INIT: install dir: {} resolved using the current directory\n", dir);
        }
        return *dir;
    }

    // The dt binary is expected to be located:
    // 1) in prod: in a bin subdirectory of the installation directory
    // 2) in dev: in a build subdirectory of the source-code directory
    void consider_bin_dir(const std::string_view bin_path)
    {
        const auto bin_dir = std::filesystem::weakly_canonical(std::filesystem::absolute(bin_path)).parent_path().parent_path();
        if (install_dir_ok(bin_dir))
            install_dir(bin_dir);
    }

    std::string install_path(const std::string_view rel_path)
    {
        std::filesystem::path path { rel_path };
        if (path.is_relative())
            path = std::filesystem::absolute(install_dir() / path);
        return std::filesystem::weakly_canonical(path).string();
    }

    config_file::config_file(const std::string &path)
            : _raw { file::read(path) }, _parsed { json::parse(_raw).as_object() }
    {
    }

    const json::value &config_file::_at_impl(const std::string_view &name) const
    {
        const auto it = _parsed.find(name);
        if (it == _parsed.end())
            throw error(fmt::format("configuration file does not have the element {}!", name));
        return it->value();
    }

    static std::optional<std::string> &_configs_default_path()
    {
        static std::optional<std::string> p {};
        return p;
    }

    void configs_dir::set_default_path(const std::optional<std::string> &p)
    {
        _configs_default_path() = p;
    }

    std::string configs_dir::default_path()
    {
        std::optional<std::string> path = _configs_default_path();
        if (const char *env_path = std::getenv("DT_ETC"); !path && env_path)
            path.emplace(env_path);
        if (!path)
            path.emplace(install_path("etc/mainnet"));
        logger::debug("Configuration directory: {}", *path);
        return *path;
    }

    const configs &configs_dir::get()
    {
        static configs_dir cfg { default_path() };
        return cfg;
    }

    configs_dir::configs_dir(const std::string &dir)
    {
        for (const auto &e: std::filesystem::directory_iterator(dir)) {
            if (!e.is_regular_file() || e.path().extension() != ".json")
                continue;
            _configs.emplace(e.path().stem().string(), e.path().string());
        }
    }

    const config &configs_dir::_at_impl(const std::string &name) const
    {
        const auto it = _configs.find(name);
        if (it == _configs.end())
            throw error(fmt::format("there is no config named {}!", name));
        return it->second;
    }
}
