#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <turbo/cardano/common/types.hpp>
#include <turbo/plutus/builtins.hpp>
#include <turbo/plutus/types.hpp>

namespace turbo::plutus::costs {
    static constexpr uint64_t max_cost = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());

    constexpr uint64_t saturated_add(uint64_t a, uint64_t b) noexcept
    {
        a = std::min(a, max_cost);
        b = std::min(b, max_cost);
        return a > max_cost - b ? max_cost : a + b;
    }

    constexpr uint64_t saturated_mul(uint64_t a, uint64_t b) noexcept
    {
        a = std::min(a, max_cost);
        b = std::min(b, max_cost);
        return a && b > max_cost / a ? max_cost : a * b;
    }

    using startup_tag = std::monostate;
    using op_tag = std::variant<term_tag, builtin_tag, startup_tag>;

    struct arg_sizes {
        using compute_fun = std::function<uint64_t(size_t)>;
        static constexpr size_t max_size = builtin_args::max_size;

        arg_sizes() =default;
        arg_sizes(std::initializer_list<uint64_t> vals): _size { _validated_size(vals.size()) }
        {
            std::copy(vals.begin(), vals.end(), _vals.begin());
        }
        arg_sizes(const size_t size, compute_fun compute):
            _size { _validated_size(size) }, _compute { std::move(compute) }
        {
        }

        size_t size() const noexcept
        {
            return _size;
        }

        uint64_t at(const size_t idx) const
        {
            if (idx >= _size) [[unlikely]]
                throw std::out_of_range("arg_sizes index is out of range");
            auto &val = _vals[idx];
            if (val == _not_computed) [[unlikely]] {
                if (!_compute)
                    throw error("argument size has no value or computation function");
                const auto computed = _compute(idx);
                if (computed == _not_computed) [[unlikely]]
                    throw error("computed argument size conflicts with the reserved sentinel");
                val = computed;
            }
            return val;
        }

        bool operator==(const arg_sizes &o) const
        {
            if (size() != o.size())
                return false;
            for (size_t i = 0; i < size(); ++i) {
                if (at(i) != o.at(i))
                    return false;
            }
            return true;
        }
    private:
        static constexpr uint64_t _not_computed = std::numeric_limits<uint64_t>::max();

        static uint8_t _validated_size(const size_t size)
        {
            if (size > max_size) [[unlikely]]
                throw std::length_error("at most six argument sizes are supported");
            return static_cast<uint8_t>(size);
        }

        mutable std::array<uint64_t, max_size> _vals {
            _not_computed, _not_computed, _not_computed,
            _not_computed, _not_computed, _not_computed
        };
        uint8_t _size = 0;
        compute_fun _compute {};
    };
    struct cost_fun {
        virtual ~cost_fun() =default;
        virtual uint64_t cost(const arg_sizes &sizes, const value_args &args) const =0;
        virtual bool operator==(const cost_fun &) const =0;
    };
    using cost_fun_ptr = std::shared_ptr<cost_fun>;

    struct size_fun {
        virtual ~size_fun() =default;
        virtual arg_sizes size(const value_args &args) const =0;
    };
    using size_fun_ptr = std::shared_ptr<size_fun>;

    struct default_size_fun: size_fun {
        arg_sizes size(const value_args &args) const override;
    };

    struct num_bytes_as_num_words_fun: size_fun {
        arg_sizes size(const value_args &args) const override;
    };

    struct literal_in_x_size_fun: size_fun {
        arg_sizes size(const value_args &args) const override;
    };

    struct value_max_depth_size_fun: size_fun {
        explicit value_max_depth_size_fun(size_t index): _index { index }
        {
        }
        arg_sizes size(const value_args &args) const override;
    private:
        size_t _index;
    };

    struct data_node_count_size_fun: size_fun {
        arg_sizes size(const value_args &args) const override;
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

    extern arg_sizes sizes_for(const op_model &, builtin_tag, const value_args &, bool text_costed_by_byte_length);

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
        using variants = std::array<std::optional<parsed_model>, 5>;

        variants v1 {};
        variants v2 {};
        variants v3 {};

        const parsed_model &for_script(cardano::script_type typ, builtin_semantics semantics) const;
    };

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
