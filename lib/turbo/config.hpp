#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <map>
#include <turbo/json.hpp>

namespace turbo {
    extern void consider_bin_dir(std::string_view bin_path);
    extern std::string install_path(std::string_view rel_path);

    struct config {
        config() =default;
        config(const config &) =default;
        config(config &&) noexcept =default;
        config &operator=(const config &) =default;
        config &operator=(config &&) noexcept =default;
        virtual ~config() =default;

        [[nodiscard]] const json::value &at(const std::string_view &name) const
        {
            return _at_impl(name);
        }

        [[nodiscard]] const json::object &json() const
        {
            return _json_impl();
        }

        [[nodiscard]] const buffer bytes() const
        {
            return _bytes_impl();
        }
    private:
        virtual const json::value &_at_impl(const std::string_view &name) const =0;
        virtual const json::object &_json_impl() const =0;
        virtual const buffer _bytes_impl() const =0;
    };

    // Used as a config mock
    struct config_json: config {
        explicit config_json(json::object &&json)
            : _json { std::move(json) }, _bytes { json::serialize_pretty(_json) }
        {
        }
        explicit config_json(const config &c): _json { c.json() }, _bytes { c.bytes() }
        {
        }
    private:
        json::object _json;
        uint8_vector _bytes;

        const json::value &_at_impl(const std::string_view &name) const override
        {
            const auto it = _json.find(name);
            if (it == _json.end())
                throw error(fmt::format("Config does not have the requested {} element!", name));
            return it->value();
        }

        const json::object &_json_impl() const override
        {
            return _json;
        }

        const buffer _bytes_impl() const override
        {
            return _bytes;
        }
    };

    struct config_file: config {
        explicit config_file(const std::string &path);
    private:
        uint8_vector _raw;
        json::object _parsed;

        const json::value &_at_impl(const std::string_view &name) const override;
        const json::object &_json_impl() const override
        {
            return _parsed;
        }
        const buffer _bytes_impl() const override
        {
            return _raw;
        }
    };

    struct configs {
        configs() =default;
        configs(const configs &) =default;
        configs(configs &&) noexcept =default;
        configs &operator=(const configs &) =default;
        configs &operator=(configs &&) noexcept =default;
        virtual ~configs() =default;

        [[nodiscard]] const config &at(const std::string &name) const
        {
            return _at_impl(name);
        }
    private:
        virtual const config &_at_impl(const std::string &) const =0;
    };

    struct configs_mock: configs {
        using map_type = std::map<std::string, config_json>;

        explicit configs_mock() =default;

        explicit configs_mock(map_type &&map): _map { std::move(map) }
        {
        }
    private:
        map_type _map;

        const config &_at_impl(const std::string &name) const override
        {
            return _map.at(name);
        }
    };

    struct configs_dir: configs {
        static void set_default_path(const std::optional<std::string> &);
        static std::string default_path();
        static const configs &get();
        explicit configs_dir(const std::string &dir);
    private:
        std::map<std::string, config_file> _configs {};

        const config &_at_impl(const std::string &) const override;
    };
}
