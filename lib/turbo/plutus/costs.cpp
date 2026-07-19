/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <bit>
#include <turbo/cardano/common/config.hpp>
#include <turbo/plutus/costs-config.hpp>
#include <turbo/plutus/machine.hpp>

namespace turbo::plutus::costs {
    static int64_t _saturated_add_signed(const int64_t a, const int64_t b) noexcept
    {
        if (b > 0 && a > std::numeric_limits<int64_t>::max() - b)
            return std::numeric_limits<int64_t>::max();
        if (b < 0 && a < std::numeric_limits<int64_t>::min() - b)
            return std::numeric_limits<int64_t>::min();
        return a + b;
    }

    static int64_t _saturated_mul_signed(const int64_t a, const int64_t b) noexcept
    {
        if (!a || !b)
            return 0;
        if (a > 0) {
            if (b > 0 && a > std::numeric_limits<int64_t>::max() / b)
                return std::numeric_limits<int64_t>::max();
            if (b < 0 && b < std::numeric_limits<int64_t>::min() / a)
                return std::numeric_limits<int64_t>::min();
        } else {
            if (b > 0 && a < std::numeric_limits<int64_t>::min() / b)
                return std::numeric_limits<int64_t>::min();
            if (b < 0 && a < std::numeric_limits<int64_t>::max() / b)
                return std::numeric_limits<int64_t>::max();
        }
        return a * b;
    }

    static uint64_t _non_negative_cost(const int64_t val)
    {
        if (val >= 0) [[likely]]
            return static_cast<uint64_t>(val);
        throw error("costing function results in a negative cost!");
    }

    static cpp_int _cpp_int_abs(const bint_type &i)
    {
        cpp_int res {};
        res = *i;
        if (res < 0)
            res = -res;
        return res;
    }

    static uint64_t _num_bytes_as_num_words(const bint_type &i)
    {
        const auto n = _cpp_int_abs(i);
        if (!n)
            return 0;
        const cpp_int words = (n - 1) / 8 + 1;
        if (words >= max_cost)
            return max_cost;
        return words.convert_to<uint64_t>();
    }

    static uint64_t _mem_usage(const value &val);
    static uint64_t _mem_usage(const data &d);

    static uint64_t _mem_usage(const bint_type &i)
    {
        const auto abs_i = _cpp_int_abs(i);
        if (!abs_i)
            return 1;
        return std::min<uint64_t>(boost::multiprecision::msb(abs_i) / 64 + 1, max_cost);
    }

    static uint64_t _mem_usage(const buffer b)
    {
        if (!b.empty()) [[likely]]
            return (b.size() - 1) / 8 + 1;
        return 1;
    }

    static uint64_t _mem_usage(const std::string_view s)
    {
        uint64_t code_points = 0;
        for (const auto c: s)
            if ((static_cast<uint8_t>(c) & 0xC0) != 0x80)
                ++code_points;
        return code_points;
    }

    static uint64_t _mem_usage(const data::list_type &l)
    {
        uint64_t sum = 0;
        for (const auto &d: l)
            sum = saturated_add(sum, _mem_usage(d));
        return sum;
    }

    static uint64_t _mem_usage(const data &d)
    {
        return std::visit([](const auto &v) {
            using t = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<t, data_constr>) {
                return saturated_add(4, _mem_usage(v->second));
            } else if constexpr (std::is_same_v<t, data::map_type>) {
                uint64_t sum = 4;
                for (const auto &p: v)
                    sum = saturated_add(sum, saturated_add(_mem_usage(p->first), _mem_usage(p->second)));
                return sum;
            } else if constexpr (std::is_same_v<t, data::bstr_type>) {
                return saturated_add(4, _mem_usage(*v));
            } else {
                return saturated_add(4, _mem_usage(v));
            }
        }, *d);
    }

    static uint64_t _mem_usage(const constant &c)
    {
        return std::visit([](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return static_cast<uint64_t>(1);
            } else if constexpr (std::is_same_v<T, bool>) {
                return static_cast<uint64_t>(1);
            } else if constexpr (std::is_same_v<T, bstr_type>) {
                return _mem_usage(buffer { *v });
            } else if constexpr (std::is_same_v<T, data>) {
                return _mem_usage(v);
            } else if constexpr (std::is_same_v<T, str_type>) {
                return _mem_usage(std::string_view { *v });
            } else if constexpr (std::is_same_v<T, bls12_381_g1_element>) {
                return static_cast<uint64_t>(sizeof(blst_p1) / 8);
            } else if constexpr (std::is_same_v<T, bls12_381_g2_element>) {
                return static_cast<uint64_t>(sizeof(blst_p2) / 8);
            } else if constexpr (std::is_same_v<T, bls12_381_ml_result>) {
                return static_cast<uint64_t>(sizeof(blst_fp12) / 8);
            } else if constexpr (std::is_same_v<T, constant_list> || std::is_same_v<T, constant_array>) {
                return numeric_cast<uint64_t>(v.size());
            } else if constexpr (std::is_same_v<T, asset_value>) {
                return numeric_cast<uint64_t>(v.total_size());
            } else if constexpr (std::is_same_v<T, constant_pair>) {
                return saturated_add(_mem_usage(v->first), _mem_usage(v->second));
            } else {
                return _mem_usage(v);
            }
        }, *c);
    }

    static uint64_t _mem_usage(const value &val)
    {
        return val.visit([](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, constant>)
                return _mem_usage(v);
            return uint64_t { 1 };
        });
    }

    static uint64_t _tree_depth(size_t size)
    {
        uint64_t depth = 0;
        while (size) {
            ++depth;
            size >>= 1;
        }
        return depth;
    }

    static uint64_t _data_node_count(const data &d)
    {
        return std::visit([](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            uint64_t count = 1;
            if constexpr (std::is_same_v<T, data_constr>) {
                for (const auto &item: v->second)
                    count = saturated_add(count, _data_node_count(item));
            } else if constexpr (std::is_same_v<T, data::map_type>) {
                for (const auto &item: v) {
                    count = saturated_add(count, _data_node_count(item->first));
                    count = saturated_add(count, _data_node_count(item->second));
                }
            } else if constexpr (std::is_same_v<T, data::list_type>) {
                for (const auto &item: v)
                    count = saturated_add(count, _data_node_count(item));
            }
            return count;
        }, *d);
    }

    namespace {
        struct runtime_arg_sizes {
            runtime_arg_sizes(const builtin_cost &model, const builtin_tag tag, const value_args &args,
                    const bool text_costed_by_byte_length):
                _model { model }, _tag { tag }, _args { args },
                _text_costed_by_byte_length { text_costed_by_byte_length }
            {
            }

            size_t size() const noexcept
            {
                return _args.size();
            }

            uint64_t at(const size_t idx) const
            {
                if (idx >= _args.size()) [[unlikely]]
                    throw std::out_of_range("runtime argument size index is out of range");
                const auto mask = static_cast<uint8_t>(1U << idx);
                if (_computed & mask) [[likely]]
                    return _cache[idx];
                _cache[idx] = _compute(idx);
                _computed |= mask;
                return _cache[idx];
            }
        private:
            uint64_t _compute(const size_t idx) const
            {
                const auto &arg = _args[idx];
                if (_text_costed_by_byte_length
                        && (_tag == builtin_tag::append_string || _tag == builtin_tag::equals_string
                            || _tag == builtin_tag::encode_utf8))
                    return arg.as_str()->size() / 4;
                switch (_model.size) {
                    case runtime_size_kind::default_size:
                        return _mem_usage(arg);
                    case runtime_size_kind::num_bytes_as_num_words:
                        return _num_bytes_as_num_words(arg.as_int());
                    case runtime_size_kind::literal_in_x: {
                        if (idx != 0)
                            return _mem_usage(arg);
                        const auto &literal = *arg.as_int();
                        if (literal == 0)
                            return 0;
                        const cpp_int magnitude = literal < 0 ? -literal : literal;
                        if (magnitude >= max_cost)
                            return max_cost;
                        return magnitude.convert_to<uint64_t>();
                    }
                    case runtime_size_kind::value_max_depth: {
                        if (idx != _model.size_index)
                            return _mem_usage(arg);
                        const auto &v = arg.as_asset_value();
                        return saturated_add(_tree_depth(v->size()), _tree_depth(v.max_inner_size()));
                    }
                    case runtime_size_kind::data_node_count:
                        return idx == 0 ? _data_node_count(arg.as_data()) : _mem_usage(arg);
                }
                throw error("unsupported runtime argument size model");
            }

            const builtin_cost &_model;
            builtin_tag _tag;
            value_args _args;
            bool _text_costed_by_byte_length;
            mutable uint8_t _computed = 0;
            mutable std::array<uint64_t, builtin_args::max_size> _cache;
        };

        template<typename Sizes>
        inline uint64_t evaluate_formula(const runtime_cost_kind kind, const uint64_t *p,
                const Sizes &sizes, const value_args &args)
        {
            switch (kind) {
                case runtime_cost_kind::constant:
                    return p[0];
                case runtime_cost_kind::linear_in_x:
                    return saturated_add(p[0], saturated_mul(p[1], sizes.at(0)));
                case runtime_cost_kind::linear_in_y:
                    return saturated_add(p[0], saturated_mul(p[1], sizes.at(1)));
                case runtime_cost_kind::linear_in_z:
                    return saturated_add(p[0], saturated_mul(p[1], sizes.at(2)));
                case runtime_cost_kind::linear_in_u:
                    return saturated_add(p[0], saturated_mul(p[1], sizes.at(3)));
                case runtime_cost_kind::linear_in_x_and_y:
                    return saturated_add(p[0], saturated_add(
                        saturated_mul(p[1], sizes.at(0)), saturated_mul(p[2], sizes.at(1))));
                case runtime_cost_kind::linear_in_y_and_z:
                    return saturated_add(p[0], saturated_add(
                        saturated_mul(p[1], sizes.at(1)), saturated_mul(p[2], sizes.at(2))));
                case runtime_cost_kind::linear_in_max_yz:
                    return saturated_add(p[0], saturated_mul(p[1], std::max(sizes.at(1), sizes.at(2))));
                case runtime_cost_kind::with_interaction_in_x_and_y:
                    return saturated_add(p[0], saturated_add(saturated_mul(p[2], sizes.at(0)),
                        saturated_add(saturated_mul(p[1], sizes.at(1)),
                            saturated_mul(p[3], saturated_mul(sizes.at(0), sizes.at(1))))));
                case runtime_cost_kind::quadratic_in_x: {
                    const auto x = sizes.at(0);
                    return saturated_add(p[0], saturated_add(
                        saturated_mul(p[1], x), saturated_mul(p[2], saturated_mul(x, x))));
                }
                case runtime_cost_kind::quadratic_in_y: {
                    const auto y = sizes.at(1);
                    return saturated_add(p[0], saturated_add(
                        saturated_mul(p[1], y), saturated_mul(p[2], saturated_mul(y, y))));
                }
                case runtime_cost_kind::quadratic_in_z: {
                    const auto z = sizes.at(2);
                    return saturated_add(p[0], saturated_add(
                        saturated_mul(p[1], z), saturated_mul(p[2], saturated_mul(z, z))));
                }
                case runtime_cost_kind::quadratic_in_x_and_y: {
                    const auto x = static_cast<int64_t>(std::min(sizes.at(0), max_cost));
                    const auto y = static_cast<int64_t>(std::min(sizes.at(1), max_cost));
                    const auto signed_arg = [p](const size_t idx) {
                        return std::bit_cast<int64_t>(p[idx]);
                    };
                    auto res = signed_arg(1);
                    res = _saturated_add_signed(res, _saturated_mul_signed(signed_arg(2), x));
                    res = _saturated_add_signed(res, _saturated_mul_signed(signed_arg(3), y));
                    res = _saturated_add_signed(res,
                        _saturated_mul_signed(_saturated_mul_signed(signed_arg(4), x), x));
                    res = _saturated_add_signed(res,
                        _saturated_mul_signed(_saturated_mul_signed(signed_arg(5), x), y));
                    res = _saturated_add_signed(res,
                        _saturated_mul_signed(_saturated_mul_signed(signed_arg(6), y), y));
                    return _non_negative_cost(std::max(signed_arg(0), res));
                }
                case runtime_cost_kind::literal_in_y_or_linear_in_z: {
                    const auto &literal = args[1].as_int();
                    if (*literal != 0)
                        return _num_bytes_as_num_words(literal);
                    return saturated_add(p[0], saturated_mul(p[1], sizes.at(2)));
                }
                case runtime_cost_kind::added_sizes:
                    return saturated_add(p[0], saturated_mul(p[1],
                        saturated_add(sizes.at(0), sizes.at(1))));
                case runtime_cost_kind::subtracted_sizes: {
                    const auto x = sizes.at(0);
                    const auto y = sizes.at(1);
                    const auto difference = x > y ? x - y : 0;
                    return saturated_add(p[0], saturated_mul(p[1], std::max(p[2], difference)));
                }
                case runtime_cost_kind::max_size:
                    return saturated_add(p[0], saturated_mul(p[1], std::max(sizes.at(0), sizes.at(1))));
                case runtime_cost_kind::min_size:
                    return saturated_add(p[0], saturated_mul(p[1], std::min(sizes.at(0), sizes.at(1))));
                case runtime_cost_kind::multiplied_sizes:
                    return saturated_add(p[0], saturated_mul(p[1],
                        saturated_mul(sizes.at(0), sizes.at(1))));
                case runtime_cost_kind::linear_on_diagonal: {
                    const auto x = sizes.at(0);
                    return x == sizes.at(1) ? saturated_add(p[0], saturated_mul(p[1], x)) : p[2];
                }
                case runtime_cost_kind::exp_mod: {
                    const auto exponent_modulus = saturated_mul(sizes.at(1), sizes.at(2));
                    const auto cost0 = saturated_add(p[0], saturated_add(
                        saturated_mul(p[1], exponent_modulus),
                        saturated_mul(p[2], saturated_mul(exponent_modulus, sizes.at(2)))));
                    return sizes.at(0) <= sizes.at(2) ? cost0 : saturated_add(cost0, cost0 / 2);
                }
                case runtime_cost_kind::invalid:
                case runtime_cost_kind::const_above_diagonal:
                case runtime_cost_kind::const_below_diagonal:
                case runtime_cost_kind::above_and_below_diagonal:
                    break;
            }
            throw error("invalid or nested runtime cost formula");
        }

        struct ordered_arg_sizes {
            const runtime_arg_sizes &source;

            uint64_t at(const size_t idx) const
            {
                if (idx == 0)
                    return std::max(source.at(0), source.at(1));
                if (idx == 1)
                    return std::min(source.at(0), source.at(1));
                return source.at(idx);
            }
        };

        inline uint64_t evaluate_runtime_cost(const runtime_cost &model, const uint64_t diagonal_constant,
                const runtime_arg_sizes &sizes, const value_args &args)
        {
            switch (model.kind) {
                case runtime_cost_kind::const_above_diagonal:
                    if (sizes.at(0) < sizes.at(1))
                        return diagonal_constant;
                    return evaluate_formula(model.nested_kind, model.args.data(), sizes, args);
                case runtime_cost_kind::const_below_diagonal:
                    if (sizes.at(0) > sizes.at(1))
                        return diagonal_constant;
                    return evaluate_formula(model.nested_kind, model.args.data(), sizes, args);
                case runtime_cost_kind::above_and_below_diagonal:
                    return evaluate_formula(model.nested_kind, model.args.data(),
                        ordered_arg_sizes { sizes }, args);
                default:
                    return evaluate_formula(model.kind, model.args.data(), sizes, args);
            }
        }
    }

    cardano::ex_units cost_builtin(const builtin_cost &model, const builtin_tag tag, const value_args &args,
            const bool text_costed_by_byte_length)
    {
        if (model.cpu.kind == runtime_cost_kind::constant
                && model.mem.kind == runtime_cost_kind::constant) [[likely]]
            return { model.mem.args[0], model.cpu.args[0] };
        runtime_arg_sizes sizes { model, tag, args, text_costed_by_byte_length };
        const auto cpu = evaluate_runtime_cost(model.cpu, model.cpu_diagonal_constant, sizes, args);
        const auto mem = evaluate_runtime_cost(model.mem, model.mem_diagonal_constant, sizes, args);
        return { mem, cpu };
    }

    static op_tag op_tag_from_cek_name(const std::string &name) {
        if (name == "cekApplyCost")
            return term_tag::apply;
        if (name == "cekBuiltinCost")
            return term_tag::builtin;
        if (name == "cekCaseCost")
            return term_tag::acase;
        if (name == "cekConstCost")
            return term_tag::constant;
        if (name == "cekConstrCost")
            return term_tag::constr;
        if (name == "cekDelayCost")
            return term_tag::delay;
        if (name == "cekForceCost")
            return term_tag::force;
        if (name == "cekLamCost")
            return term_tag::lambda;
        if (name == "cekStartupCost")
            return startup_tag {};
        if (name == "cekVarCost")
            return term_tag::variable;
        throw error(fmt::format("unsupported CEK cost item: {}", name));
    }

    static runtime_cost runtime_cost_from_args(const arg_map &args, uint64_t &diagonal_constant)
    {
        diagonal_constant = 0;
        const auto value = [&](const std::string_view name) {
            return std::stoull(args.at(std::string { name }));
        };
        const auto signed_value = [&](const std::string_view name) {
            return std::bit_cast<uint64_t>(std::stoll(args.at(std::string { name })));
        };
        const auto direct = [](const runtime_cost_kind kind,
                const std::initializer_list<uint64_t> params) {
            runtime_cost res {};
            res.kind = kind;
            if (params.size() > 7) [[unlikely]]
                throw error("runtime cost formula has too many parameters");
            std::copy(params.begin(), params.end(), res.args.begin());
            return res;
        };
        const auto linear = [&](const runtime_cost_kind kind) {
            return direct(kind, { value("arguments-intercept"), value("arguments-slope") });
        };
        const auto nested = [&](const runtime_cost_kind kind, const bool has_constant) {
            constexpr std::string_view prefix { "arguments-model-" };
            arg_map nested_args {};
            for (const auto &[key, val]: args) {
                if (key.starts_with(prefix)) {
                    const auto [it, created] = nested_args.try_emplace(key.substr(prefix.size()), val);
                    if (!created) [[unlikely]]
                        throw error(fmt::format("duplicate argument {}", key));
                }
            }
            uint64_t nested_diagonal_constant = 0;
            auto nested_cost = runtime_cost_from_args(nested_args, nested_diagonal_constant);
            if (nested_cost.nested_kind != runtime_cost_kind::invalid) [[unlikely]]
                throw error("nested diagonal runtime cost formulas are unsupported");
            runtime_cost res {};
            res.kind = kind;
            res.nested_kind = nested_cost.kind;
            res.args = nested_cost.args;
            if (has_constant)
                diagonal_constant = value("arguments-constant");
            return res;
        };
        const auto &type = args.at("type");
        if (type == "constant_cost")
            return direct(runtime_cost_kind::constant, { value("arguments") });
        if (type == "linear_cost" || type == "linear_in_x")
            return linear(runtime_cost_kind::linear_in_x);
        if (type == "linear_in_y" || type == "linear_in_y2")
            return linear(runtime_cost_kind::linear_in_y);
        if (type == "linear_in_z")
            return linear(runtime_cost_kind::linear_in_z);
        if (type == "linear_in_u")
            return linear(runtime_cost_kind::linear_in_u);
        if (type == "linear_in_x_and_y")
            return direct(runtime_cost_kind::linear_in_x_and_y, {
                value("arguments-intercept"), value("arguments-slope1"), value("arguments-slope2")
            });
        if (type == "linear_in_y_and_z")
            return direct(runtime_cost_kind::linear_in_y_and_z, {
                value("arguments-intercept"), value("arguments-slope1"), value("arguments-slope2")
            });
        if (type == "linear_in_max_yz")
            return linear(runtime_cost_kind::linear_in_max_yz);
        if (type == "with_interaction_in_x_and_y")
            return direct(runtime_cost_kind::with_interaction_in_x_and_y, {
                value("arguments-c00"), value("arguments-c01"),
                value("arguments-c10"), value("arguments-c11")
            });
        if (type == "quadratic_in_x")
            return direct(runtime_cost_kind::quadratic_in_x, {
                value("arguments-c0"), value("arguments-c1"), value("arguments-c2")
            });
        if (type == "quadratic_in_y")
            return direct(runtime_cost_kind::quadratic_in_y, {
                value("arguments-c0"), value("arguments-c1"), value("arguments-c2")
            });
        if (type == "quadratic_in_z")
            return direct(runtime_cost_kind::quadratic_in_z, {
                value("arguments-c0"), value("arguments-c1"), value("arguments-c2")
            });
        if (type == "quadratic_in_x_and_y")
            return direct(runtime_cost_kind::quadratic_in_x_and_y, {
                signed_value("arguments-minimum"), signed_value("arguments-c00"),
                signed_value("arguments-c10"), signed_value("arguments-c01"),
                signed_value("arguments-c20"), signed_value("arguments-c11"),
                signed_value("arguments-c02")
            });
        if (type == "literal_in_y_or_linear_in_z")
            return linear(runtime_cost_kind::literal_in_y_or_linear_in_z);
        if (type == "added_sizes")
            return linear(runtime_cost_kind::added_sizes);
        if (type == "subtracted_sizes")
            return direct(runtime_cost_kind::subtracted_sizes, {
                value("arguments-intercept"), value("arguments-slope"), value("arguments-minimum")
            });
        if (type == "max_size")
            return linear(runtime_cost_kind::max_size);
        if (type == "min_size")
            return linear(runtime_cost_kind::min_size);
        if (type == "multiplied_sizes")
            return linear(runtime_cost_kind::multiplied_sizes);
        if (type == "const_above_diagonal")
            return nested(runtime_cost_kind::const_above_diagonal, true);
        if (type == "const_below_diagonal")
            return nested(runtime_cost_kind::const_below_diagonal, true);
        if (type == "above_and_below_diagonal")
            return nested(runtime_cost_kind::above_and_below_diagonal, false);
        if (type == "linear_on_diagonal")
            return direct(runtime_cost_kind::linear_on_diagonal, {
                value("arguments-intercept"), value("arguments-slope"), value("arguments-constant")
            });
        if (type == "exp_mod_cost")
            return direct(runtime_cost_kind::exp_mod, {
                value("arguments-coefficient00"), value("arguments-coefficient11"),
                value("arguments-coefficient12")
            });
        throw error(fmt::format("unsupported cost model type: {}", type));
    }

    static cardano::ex_units ex_units_from_args(const arg_map &args)
    {
        cardano::ex_units c {};
        for (const auto &[k, v]: args) {
            const auto pos = k.find('-');
            if (pos == k.npos) {
                if (k == "exBudgetCPU") {
                    c.steps = std::stoull(v);
                } else if (k == "exBudgetMemory") {
                    c.mem = std::stoull(v);
                } else {
                    throw error(fmt::format("unsupported cost argument name: {}", k));
                }
            } else {
                throw error(fmt::format("unsupported cost argument category: {}", k));
            }
        }
        if (!c.steps || !c.mem) [[unlikely]]
            throw error(fmt::format("partially initialized constant cost {}", args));
        return c;
    }

    static builtin_cost builtin_cost_from_args(const arg_map &args)
    {
        if (args.empty()) [[unlikely]]
            throw error("cost arguments must be non-empty!");
        arg_map cpu_args {};
        arg_map mem_args {};
        for (const auto &[k, v]: args) {
            const auto pos = k.find('-');
            if (pos == k.npos) {
                if (k == "exBudgetCPU") {
                    cpu_args.emplace("arguments", v);
                    cpu_args.emplace("type", "constant_cost");
                } else if (k == "exBudgetMemory") {
                    mem_args.emplace("arguments", v);
                    mem_args.emplace("type", "constant_cost");
                } else {
                    throw error(fmt::format("unsupported cost argument name: {}", k));
                }
            } else {
                const auto cat_name = k.substr(0, pos);
                const auto sub_name = k.substr(pos + 1);
                if (cat_name == "cpu") {
                    cpu_args.emplace(sub_name, v);
                } else if (cat_name == "memory") {
                    mem_args.emplace(sub_name, v);
                } else {
                    throw error(fmt::format("unsupported cost argument category: {}", cat_name));
                }
            }
        }
        builtin_cost model {};
        model.cpu = runtime_cost_from_args(cpu_args, model.cpu_diagonal_constant);
        model.mem = runtime_cost_from_args(mem_args, model.mem_diagonal_constant);
        return model;
    }

    static arg_map cost_args_from_json(const std::string &prefix, const json::object &o)
    {
        arg_map args {};
        for (const auto &[k, v]: o) {
            switch (v.kind()) {
                case json::kind::object: {
                    auto sub_args = cost_args_from_json(fmt::format("{}{}-", prefix, static_cast<std::string_view>(k)), v.as_object());
                    for (auto &&[k, v]: sub_args)
                        args.try_emplace(k, std::move(v));
                    break;
                }
                case json::kind::uint64:
                case json::kind::int64:
                    args.try_emplace(fmt::format("{}{}", prefix, static_cast<std::string_view>(k)), fmt::format("{}", json::value_to<int64_t>(v)));
                    break;
                case json::kind::string:
                    args.try_emplace(fmt::format("{}{}", prefix, static_cast<std::string_view>(k)), fmt::format("{}", json::value_to<std::string>(v)));
                    break;
                default: throw error(fmt::format("unsupported json kind at {}{}: {}", prefix, static_cast<std::string_view>(k), static_cast<int>(v.kind())));
            }
        }
        return args;
    }

    static arg_map load_cost_args(const std::string &cek_path, const std::string &builtin_path)
    {
        auto args = cost_args_from_json("", json::load(builtin_path).as_object());
        auto sub_args = cost_args_from_json("", json::load(cek_path).as_object());
        for (auto &&[k, v]: sub_args)
            args.try_emplace(k, std::move(v));
        return args;
    }

    std::string canonical_arg_name(const std::string &name)
    {
        switch (name[0]) {
            case 'b': {
                static const std::string match1 { "blake2b" };
                static const std::string replace1 { "blake2b_256" };
                if (name == match1)
                    return replace1;
                static const std::string match2 { "blake2b-" };
                static const std::string replace2 { "blake2b_256-" };
                if (name.substr(0, match2.size()) == match2)
                    return replace2 + name.substr(match2.size());
                break;
            }
            case 'v': {
                static const std::string match1 { "verifySignature" };
                static const std::string replace1 { "verifyEd25519Signature" };
                if (name == match1)
                    return replace1;
                static const std::string match2 { "verifySignature-" };
                static const std::string replace2 { "verifyEd25519Signature-" };
                if (name.substr(0, match2.size()) == match2)
                    return replace2 + name.substr(match2.size());
                break;
            }
            default: break;
        }
        return name;
    }

    std::string v1_arg_name(const std::string &name)
    {
        switch (name[0]) {
            case 'b': {
                static const std::string match { "blake2b_256-" };
                static const std::string replace { "blake2b-" };
                if (name.substr(0, match.size()) == match)
                    return replace + name.substr(match.size());
                break;
            }
            case 'v': {
                static const std::string match { "verifyEd25519Signature-" };
                static const std::string replace { "verifySignature-" };
                if (name.substr(0, match.size()) == match)
                    return replace + name.substr(match.size());
                break;
            }
            default: break;
        }
        return name;
    }

    static arg_map plutus_costs_to_args(const cardano::plutus_cost_model &model, const arg_map &defaults)
    {
        arg_map args { defaults };
        for (const auto &[k, v]: model) {
            const auto pos = k.find('-');
            if (pos == 0 || pos == k.npos) [[unlikely]]
                throw error(fmt::format("invalid cost model item: {}", k));
            auto op_name = canonical_arg_name(k.substr(0, pos));
            const auto arg_name = k.substr(pos + 1);
            auto full_arg_name = fmt::format("{}-{}", op_name, arg_name);
            auto arg_val = fmt::format("{}", v);
            const auto [it, created] = args.try_emplace(std::move(full_arg_name), std::move(arg_val));
            if (!created)
                it->second = std::move(arg_val);
        }
        return args;
    }

    static runtime_model ingest(const arg_map &args)
    {
        using tmp_model = std::map<op_tag, arg_map>;
        tmp_model tmp {};
        std::set<std::string> unknown_builtins {};
        for (const auto &[k, v]: args) {
            const auto pos = k.find('-');
            if (pos == k.npos) [[unlikely]]
                throw error(fmt::format("invalid cost model item: {}", k));
            const auto op_name = k.substr(0, pos);
            const auto arg_name = k.substr(pos + 1);
            if (op_name.starts_with("cek")) {
                const auto [it, created] = tmp[op_tag_from_cek_name(op_name)].try_emplace(arg_name, v);
                if (!created) [[unlikely]]
                            throw error(fmt::format("duplicate argument {} for op {}", arg_name, op_name));
            } else if (builtin_tag_known_name(op_name)) {
                const auto [it, created] = tmp[builtin_tag_from_name(op_name)].try_emplace(arg_name, v);
                if (!created) [[unlikely]]
                            throw error(fmt::format("duplicate argument {} for op {}", arg_name, op_name));
            } else {
                // configs do contain builtins that are not on mainnet, such as addByteString
                // log each unsupported builtin only once
                if (const auto [it, created] = unknown_builtins.emplace(op_name); created)
                    logger::debug("found cost model for an unsupported builtin: {}", op_name);
            }
        }
        runtime_model m {};
        for (const auto &[t, args]: tmp) {
            std::visit([&](const auto &tag) {
                using T = std::decay_t<decltype(tag)>;
                if constexpr (std::is_same_v<T, startup_tag>) {
                    m.startup_op = ex_units_from_args(args);
                } else if constexpr (std::is_same_v<T, term_tag>) {
                    switch (tag) {
                        case term_tag::apply: m.apply_op = ex_units_from_args(args); break;
                        case term_tag::builtin: m.builtin_op = ex_units_from_args(args); break;
                        case term_tag::acase: m.case_op = ex_units_from_args(args); break;
                        case term_tag::constant: m.constant_op = ex_units_from_args(args); break;
                        case term_tag::constr: m.constr_op = ex_units_from_args(args); break;
                        case term_tag::delay: m.delay_op = ex_units_from_args(args); break;
                        case term_tag::force: m.force_op = ex_units_from_args(args); break;
                        case term_tag::lambda: m.lambda_op = ex_units_from_args(args); break;
                        case term_tag::variable: m.variable_op = ex_units_from_args(args); break;
                        default: throw error(fmt::format("unsupported tag: {}", tag));
                    }
                } else if constexpr (std::is_same_v<T, builtin_tag>) {
                    const auto [op, created] = m.builtin_costs.try_emplace(tag, builtin_cost_from_args(args));
                    if (!created) [[unlikely]]
                        throw error("internal error: duplicate tag in the parsed cost model!");
                    switch (tag) {
                        case builtin_tag::drop_list: {
                            op->size = runtime_size_kind::literal_in_x;
                            break;
                        }
                        case builtin_tag::replicate_byte: {
                            op->size = runtime_size_kind::num_bytes_as_num_words;
                            break;
                        }
                        case builtin_tag::insert_coin: {
                            op->size = runtime_size_kind::value_max_depth;
                            op->size_index = 3;
                            break;
                        }
                        case builtin_tag::lookup_coin: {
                            op->size = runtime_size_kind::value_max_depth;
                            op->size_index = 2;
                            break;
                        }
                        case builtin_tag::un_value_data: {
                            op->size = runtime_size_kind::data_node_count;
                            break;
                        }
                        default:
                            break;
                    }
                } else {
                    throw error(fmt::format("unsupported tag type: {}", typeid(T).name()));
                }
            }, t);
        }
        return m;
    }

    const runtime_model &runtime_models::for_script(const cardano::script_type typ,
            const builtin_semantics semantics) const
    {
        const variants *models = nullptr;
        switch (typ) {
            case cardano::script_type::plutus_v1: models = &v1; break;
            case cardano::script_type::plutus_v2: models = &v2; break;
            case cardano::script_type::plutus_v3: models = &v3; break;
            default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(typ)));
        }
        const auto &model = models->at(static_cast<size_t>(semantics));
        if (model) [[likely]]
            return *model;
        throw error(fmt::format("cost model semantics variant {} is unavailable for {}",
            static_cast<int>(semantics), typ));
    }

    const arg_map &default_cost_args_a()
    {
        static auto args = load_cost_args(install_path("etc/plutus/cekMachineCostsA.json"), install_path("etc/plutus/builtinCostModelA.json"));
        return args;
    }

    const arg_map &default_cost_args_b()
    {
        static auto args = load_cost_args(install_path("etc/plutus/cekMachineCostsB.json"), install_path("etc/plutus/builtinCostModelB.json"));
        return args;
    }

    const arg_map &default_cost_args_c()
    {
        static auto args = load_cost_args(install_path("etc/plutus/cekMachineCostsC.json"), install_path("etc/plutus/builtinCostModelC.json"));
        return args;
    }

    // Variants C, D, and E use the same CEK machine costs; only their builtin models differ.
    const arg_map &default_cost_args_d()
    {
        static auto args = load_cost_args(install_path("etc/plutus/cekMachineCostsC.json"), install_path("etc/plutus/builtinCostModelD.json"));
        return args;
    }

    const arg_map &default_cost_args_e()
    {
        static auto args = load_cost_args(install_path("etc/plutus/cekMachineCostsC.json"), install_path("etc/plutus/builtinCostModelE.json"));
        return args;
    }

    const std::vector<std::string> &cost_arg_names_v1()
    {
        static const std::vector<std::string> names {
#include "cost-args-v1.inc"
        };
        return names;
    }

    const std::vector<std::string> &cost_arg_names_v2()
    {
        static const std::vector<std::string> names {
#include "cost-args-v2.inc"
        };
        return names;
    }

    const std::vector<std::string> &cost_arg_names_v3()
    {
        static const std::vector<std::string> names {
#include "cost-args-v3.inc"
        };
        return names;
    }

    runtime_models ingest(const cardano::plutus_cost_models &models)
    {
        runtime_models res {};
        const auto *v1 = models.find(0);
        const auto *v2 = models.find(1);
        const auto *v3 = models.find(2);
        const auto add = [](runtime_models::variants &variants, const builtin_semantics semantics,
                const cardano::plutus_cost_model *params, const arg_map &defaults) {
            variants.at(static_cast<size_t>(semantics)).emplace(
                ingest(params ? plutus_costs_to_args(*params, defaults) : defaults));
        };
        add(res.v1, builtin_semantics::a, v1, default_cost_args_a());
        add(res.v1, builtin_semantics::b, v1, default_cost_args_b());
        add(res.v1, builtin_semantics::d, v1, default_cost_args_d());
        add(res.v2, builtin_semantics::a, v2, default_cost_args_a());
        add(res.v2, builtin_semantics::b, v2, default_cost_args_b());
        add(res.v2, builtin_semantics::d, v2, default_cost_args_d());
        add(res.v3, builtin_semantics::c, v3, default_cost_args_c());
        add(res.v3, builtin_semantics::e, v3, default_cost_args_e());
        return res;
    }

    const runtime_models &defaults()
    {
        static auto models = ingest(cardano::config::get().plutus_all_cost_models);
        return models;
    }
}
