#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <map>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
#include <turbo/plutus/costs.hpp>

namespace turbo::plutus::costs {
    using startup_tag = std::monostate;
    using op_tag = std::variant<term_tag, builtin_tag, startup_tag>;
    using arg_map = std::map<std::string, std::string>;

    const std::vector<std::string> &cost_arg_names_v1();
    const std::vector<std::string> &cost_arg_names_v2();
    const std::vector<std::string> &cost_arg_names_v3();
    const arg_map &default_cost_args_a();
    const arg_map &default_cost_args_b();
    const arg_map &default_cost_args_c();
    const arg_map &default_cost_args_d();
    const arg_map &default_cost_args_e();
    extern std::string canonical_arg_name(const std::string &name);
    extern std::string v1_arg_name(const std::string &name);

    // Validate and compile ledger/configuration data into immutable execution-ready models.
    extern runtime_models ingest(const cardano::plutus_cost_models &);
}

namespace fmt {
    template<>
    struct formatter<turbo::plutus::costs::op_tag>: formatter<int> {
        template<typename FormatContext>
        auto format(const turbo::plutus::costs::op_tag &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using namespace turbo::plutus::costs;
            return std::visit([&ctx](const auto &vv) {
                using T = std::decay_t<decltype(vv)>;
                if constexpr (std::is_same_v<T, startup_tag>)
                    return fmt::format_to(ctx.out(), "startup");
                else
                    return fmt::format_to(ctx.out(), "{}", vv);
            }, v);
        }
    };
}
