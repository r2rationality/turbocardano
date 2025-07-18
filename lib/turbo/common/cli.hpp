#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "logger.hpp"
#include "numeric-cast.hpp"

namespace turbo::cli {
    using arguments = std::vector<std::string>;
    using options = std::map<std::string, std::optional<std::string>>;

    struct command_info {
        std::string name {};
        std::string usage {};
        std::string descr {};
    };

    using option_validator = std::function<std::optional<std::string>(const std::optional<std::string> &)>;

    struct option_config {
        std::string desc {};
        std::optional<std::string> default_value {};
        std::optional<option_validator> validator {};
    };
    using option_config_map = std::map<std::string, option_config>;

    struct argument_config {
        std::optional<size_t> min {};
        std::optional<size_t> max {};
        std::vector<std::string> names {};

        void expect(const std::initializer_list<std::string> &args)
        {
            names = args;
            size_t req = 0;
            size_t opt = 0;
            for (const auto &a: args) {
                if (a.at(0) == '[') {
                    if (a.ends_with("...]"))
                        opt = std::numeric_limits<size_t>::max();
                    if (opt < std::numeric_limits<size_t>::max())
                        ++opt;
                } else {
                    ++req;
                }
            }
            min = req;
            max = req;
            if (opt == std::numeric_limits<size_t>::max())
                *max = opt;
            else
                *max += opt;
        }
    };

    struct config {
        std::string name {};
        std::string desc {};
        argument_config args {};
        option_config_map opts {};
        std::optional<std::string> usage {};

        std::string make_usage() const
        {
            if (usage)
                return fmt::format("{} - {}", *usage, desc);
            std::string arg_info {};
            if (!args.names.empty()) {
                for (const auto &name: args.names)
                    arg_info += fmt::format(" {}", name);
            }
            const std::string opt_info { opts.empty() ? "" : "[options]" };
            return fmt::format("{}{} - {}", opt_info, arg_info, desc);
        }
    };

    struct parse_result {
        arguments args {};
        options opts {};
    };

    struct command {
        using command_list = std::vector<std::shared_ptr<command>>;

        static const command_list &registry()
        {
            return _registry();
        }

        static std::shared_ptr<command> reg(std::shared_ptr<command> &&cmd)
        {
            return _registry().emplace_back(std::move(cmd));
        }

        virtual ~command() =default;

        virtual const command_info &info() const
        {
            throw error("not implemented");
        }

        virtual void run(const arguments &) const
        {
            throw error("not implemented!");
        }

        virtual void run(const arguments &args, const options &) const
        {
            run(args);
        }

        virtual void configure(config &meta) const
        {
            const auto &inf = info();
            meta.name = inf.name;
            meta.desc = inf.descr;
            meta.usage = inf.usage;
        }

        parse_result parse(const config &cfg, const arguments &args) const
        {
            parse_result pr {};
            for (const auto &arg: args) {
                if (arg.substr(0, 2) == "--") {
                    std::string name = arg.substr(2);
                    std::optional<std::string> val {};
                    if (const auto eq_pos = arg.find('=', 2); eq_pos != arg.npos) {
                        val = arg.substr(eq_pos + 1);
                        name = arg.substr(2, eq_pos - 2);
                    }
                    const auto cfg_it = cfg.opts.find(name);
                    if (cfg_it == cfg.opts.end())
                        throw error(fmt::format("unknown option '--{}'", name));
                    const auto [opt_it, opt_created] = pr.opts.try_emplace(name, std::move(val));
                    if (!opt_created)
                        throw error(fmt::format("duplicate option specification '{}'", arg));
                } else {
                    pr.args.emplace_back(arg);
                }
            }
            for (const auto &[name, cfg]: cfg.opts) {
                if (cfg.default_value && !pr.opts.contains(name))
                    pr.opts.emplace(name, *cfg.default_value);
                // creates an empty value if not initialized
                if (const auto &val_it = pr.opts.find(name); cfg.validator && val_it != pr.opts.end()) {
                    if (const auto val_err = (*cfg.validator)(val_it->second); val_err)
                        throw error(fmt::format("value {} is invalid for '--{}': {}", val_it->second, name, *val_err));
                }
            }
            if (cfg.args.min && pr.args.size() < *cfg.args.min)
                _throw_usage(cfg);
            if (cfg.args.max && pr.args.size() > *cfg.args.max)
                _throw_usage(cfg);
            //if (const auto opt_it = pr.opts.find("config-dir"); opt_it != pr.opts.end() && opt_it->second)
            //    configs_dir::set_default_path(*opt_it->second);
            return pr;
        }
    protected:
        void _throw_usage(const config &cmd) const
        {
            std::string usage = fmt::format("usage: {}", cmd.make_usage());
            if (!cmd.opts.empty()) {
                usage += fmt::format("\n{} supports the following options:", cmd.name);
                for (const auto &[name, cfg]: cmd.opts) {
                    if (cfg.default_value)
                        usage += fmt::format("\n    --{} ({} by default) - {}", name, *cfg.default_value, cfg.desc);
                    else
                        usage += fmt::format("\n    --{} - {}", name, cfg.desc);
                }
            }
            throw error(usage);
        }

        void _throw_usage() const
        {
            config cmd {};
            configure(cmd);
            _throw_usage(cmd);
        }
    private:
        static command_list &_registry()
        {
            static command_list l {};
            return l;
        }
    };

    struct command_meta {
        std::shared_ptr<command> cmd {};
        config cfg {};
    };

    using global_options_proc_t = std::function<void(const options &)>;

    extern int run(int argc, const char **argv, const std::optional<global_options_proc_t> &global_opts_f={});

    template<typename T>
    T from_str(const char *str)
    {
        char *end;
        errno = 0;
        const long val = strtoll(str, &end, 10);
        if (errno || end == str || *end != '\0') [[unlikely]]
            throw error_sys(fmt::format("failed to parse {} from '{}'", typeid(T).name(), str));
        return numeric_cast<T>(val);
    }
}