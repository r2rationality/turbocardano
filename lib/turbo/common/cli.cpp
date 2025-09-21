/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <iostream>
#include "cli.hpp"
#include "timer.hpp"
#ifndef _WIN32
#   include <sys/resource.h>
#endif

namespace turbo::cli {
    int run(const int argc, const char **argv, const command::command_list &command_list, const std::optional<global_options_t> &global_opts)
    {
        std::set_terminate([]() {
            std::cerr << fmt::format("std::terminate called; uncaught exceptions: {} - terminating\n", std::uncaught_exceptions());
            std::abort();
        });
        std::ios_base::sync_with_stdio(false);
#ifndef _WIN32
        {
            static constexpr size_t stack_size = 32 << 20;
            struct rlimit rl;
            if (getrlimit(RLIMIT_STACK, &rl) != 0) [[unlikely]]
                throw error_sys("getrlimit RLIMIT_STACK failed!");
            if (rl.rlim_cur < stack_size) {
                rl.rlim_cur = stack_size;
                if (setrlimit(RLIMIT_STACK, &rl) != 0) [[unlikely]]
                    throw error_sys("setrlimit RLIMIT_STACK failed!");
            }
            logger::info("stack size: {} MB", rl.rlim_cur >> 20);
        }
#endif
        try {
            std::map<std::string, command_meta> commands {};
            for (const auto &cmd: command_list) {
                command_meta meta { cmd };
                cmd->configure(meta.cfg);
                if (global_opts) {
                    for (const auto &[name, cfg]: global_opts->opts)
                        meta.cfg.opts.emplace(name, cfg);
                }
                if (const auto [it, created] = commands.try_emplace(meta.cfg.name, std::move(meta)); !created) [[unlikely]]
                    throw error(fmt::format("multiple definitions for {}", meta.cfg.name));
            }
            if (argc < 2) {
                std::cerr << "Usage: <command> [<arg> ...], where <command> is one of:\n" ;
                for (const auto &[name, cmd]: commands)
                    std::cerr << fmt::format("    {} {}\n", cmd.cfg.name, cmd.cfg.make_usage());
                return 1;
            }

            const std::string cmd { argv[1] };
            logger::debug("run {}", cmd);
            const auto cmd_it = commands.find(cmd);
            if (cmd_it == commands.end()) {
                logger::error("Unknown command {}", cmd);
                return 1;
            }

            arguments args {};
            for (int i = 2; i < argc; ++i)
                args.emplace_back(argv[i]);
            const auto &meta = cmd_it->second;
            timer t { fmt::format("run {}", cmd), logger::level::info };
            const auto pr = meta.cmd->parse(meta.cfg, args);
            if (global_opts)
                global_opts->proc(pr.opts);
            meta.cmd->run(pr.args, pr.opts);
        } catch (const std::exception &ex) {
            logger::error("{}", ex.what());
            return 1;
        } catch (...) {
            logger::error("unrecognized exception at caught at main!");
            return 1;
        }
        return 0;
    }

    int run(const int argc, const char **argv, const std::optional<global_options_t> &global_opts_f)
    {
        return run(argc, argv, command::registry(), global_opts_f);
    }
}