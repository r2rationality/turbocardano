#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/types.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus::costs {
    using startup_tag = std::monostate;
    using op_tag = std::variant<term_tag, builtin_tag, startup_tag>;

    using arg_sizes = std::vector<uint64_t>;
    struct cost_fun {
        virtual ~cost_fun() =default;
        virtual uint64_t cost(const arg_sizes &sizes, const value_list &args) const =0;
        virtual bool operator==(const cost_fun &) const =0;
    };
    using cost_fun_ptr = std::shared_ptr<cost_fun>;

    struct size_fun {
        virtual ~size_fun() =default;
        virtual arg_sizes size(const value_list &args) const =0;
    };
    using size_fun_ptr = std::shared_ptr<size_fun>;

    struct default_size_fun: size_fun {
        arg_sizes size(const value_list &args) const override;
    };

    struct num_bytes_as_num_words_fun: size_fun {
        arg_sizes size(const value_list &args) const override;
    };

    struct op_model {
        cost_fun_ptr cpu {};
        cost_fun_ptr mem {};
        size_fun_ptr size {};

        bool operator==(const op_model &o) const
        {
            return cpu && o.cpu && *cpu == *o.cpu && mem && o.mem && *mem == *o.mem && size.get() == o.size.get();
        }
    };

    extern arg_sizes sizes_for(const op_model &, builtin_tag, const value_list &, bool text_costed_by_byte_length);

    struct parsed_model {
        cardano::ex_units startup_op;
        cardano::ex_units apply_op;
        cardano::ex_units builtin_op;
        cardano::ex_units case_op;
        cardano::ex_units constant_op;
        cardano::ex_units constr_op;
        cardano::ex_units delay_op;
        cardano::ex_units force_op;
        cardano::ex_units lambda_op;
        cardano::ex_units variable_op;
        std::unordered_map<builtin_tag, op_model> builtin_fun {};
    };

    struct parsed_models {
        std::optional<parsed_model> v1 {};
        std::optional<parsed_model> v2 {};
        std::optional<parsed_model> v3 {};

        const parsed_model &for_script(cardano::script_type typ) const;
    };

    using arg_map = std::map<std::string, std::string>;

    const std::vector<std::string> &cost_arg_names_v1();
    const std::vector<std::string> &cost_arg_names_v2();
    const std::vector<std::string> &cost_arg_names_v3();
    const arg_map &default_cost_args_v1();
    const arg_map &default_cost_args_v2();
    const arg_map &default_cost_args_v3();
    extern std::string canonical_arg_name(const std::string &name);
    extern std::string v1_arg_name(const std::string &name);
    extern parsed_models parse(const cardano::plutus_cost_models &);
    extern const parsed_models &defaults();
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