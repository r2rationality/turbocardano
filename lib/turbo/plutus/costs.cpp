/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/config.hpp>
#include <turbo/plutus/costs.hpp>
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

    arg_sizes default_size_fun::size(const value_args &args) const
    {
        const auto *args_ptr = args.data();
        return { args.size(), [args_ptr](const size_t idx) {
            return _mem_usage(args_ptr[idx]);
        } };
    };

    arg_sizes literal_in_x_size_fun::size(const value_args &args) const
    {
        const auto *args_ptr = args.data();
        return { args.size(), [args_ptr](const size_t idx) {
            const auto &arg = args_ptr[idx];
            if (idx != 0)
                return _mem_usage(arg);
            const auto &literal = *arg.as_int();
            if (literal == 0)
                return uint64_t { 0 };
            const cpp_int magnitude = literal < 0 ? -literal : literal;
            if (magnitude >= max_cost)
                return max_cost;
            return magnitude.convert_to<uint64_t>();
        } };
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

    arg_sizes value_max_depth_size_fun::size(const value_args &args) const
    {
        const auto *args_ptr = args.data();
        const auto index = _index;
        return { args.size(), [args_ptr, index](const size_t idx) {
            const auto &arg = args_ptr[idx];
            if (idx != index)
                return _mem_usage(arg);
            const auto &v = arg.as_asset_value();
            return saturated_add(_tree_depth(v->size()), _tree_depth(v.max_inner_size()));
        } };
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

    arg_sizes data_node_count_size_fun::size(const value_args &args) const
    {
        const auto *args_ptr = args.data();
        return { args.size(), [args_ptr](const size_t idx) {
            const auto &arg = args_ptr[idx];
            return idx == 0 ? _data_node_count(arg.as_data()) : _mem_usage(arg);
        } };
    }

    arg_sizes sizes_for(const op_model &model, const builtin_tag tag, const value_args &args,
            const bool text_costed_by_byte_length)
    {
        if (text_costed_by_byte_length
                && (tag == builtin_tag::append_string || tag == builtin_tag::equals_string
                    || tag == builtin_tag::encode_utf8)) {
            const auto *args_ptr = args.data();
            return { args.size(), [args_ptr](const size_t idx) {
                return args_ptr[idx].as_str()->size() / 4;
            } };
        }
        return model.size->size(args);
    }

    arg_sizes num_bytes_as_num_words_fun::size(const value_args &args) const
    {
        const auto *args_ptr = args.data();
        return { args.size(), [args_ptr](const size_t idx) {
            return _num_bytes_as_num_words(args_ptr[idx].as_int());
        } };
    };

    // the names of the classes match the names in the builtinCostModel config JSON
    struct constant_cost: cost_fun {
        constant_cost(const arg_map &args)
            : _cost { std::stoull(args.at("arguments")) }
        {
        }

        uint64_t cost(const arg_sizes &, const value_args &) const override
        {
            return _cost;
        }

        bool operator==(const cost_fun &o) const override
        {
            return _cost == dynamic_cast<const constant_cost &>(o)._cost;
        }
    protected:
        const uint64_t _cost;
    };

    struct linear_in_x: cost_fun {
        linear_in_x(const arg_map &args):
            _intercept { std::stoull(args.at("arguments-intercept")) },
            _slope { std::stoull(args.at("arguments-slope")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            return saturated_add(_intercept, saturated_mul(_slope, sizes.at(0)));
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _intercept == o._intercept && _slope == o._slope;
        }
    protected:
        const uint64_t _intercept, _slope;
    };

    struct linear_in_y: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            return saturated_add(_intercept, saturated_mul(_slope, sizes.at(1)));
        }
    };

    struct linear_in_z: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            return saturated_add(_intercept, saturated_mul(_slope, sizes.at(2)));
        }
    };

    struct linear_in_u: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            return saturated_add(_intercept, saturated_mul(_slope, sizes.at(3)));
        }
    };

    struct linear_in_x_and_y: cost_fun {
        linear_in_x_and_y(const arg_map &args):
            _intercept { std::stoull(args.at("arguments-intercept")) },
            _slope1 { std::stoull(args.at("arguments-slope1")) },
            _slope2 { std::stoull(args.at("arguments-slope2")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            return saturated_add(_intercept,
                saturated_add(saturated_mul(_slope1, sizes.at(0)), saturated_mul(_slope2, sizes.at(1))));
        }

        bool operator==(const cost_fun &other) const override
        {
            const auto &o = dynamic_cast<const linear_in_x_and_y &>(other);
            return _intercept == o._intercept && _slope1 == o._slope1 && _slope2 == o._slope2;
        }
    private:
        uint64_t _intercept, _slope1, _slope2;
    };

    struct with_interaction_in_x_and_y: cost_fun {
        with_interaction_in_x_and_y(const arg_map &args):
            _c00 { std::stoull(args.at("arguments-c00")) },
            _c01 { std::stoull(args.at("arguments-c01")) },
            _c10 { std::stoull(args.at("arguments-c10")) },
            _c11 { std::stoull(args.at("arguments-c11")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            return saturated_add(_c00, saturated_add(saturated_mul(_c10, sizes.at(0)),
                saturated_add(saturated_mul(_c01, sizes.at(1)),
                    saturated_mul(_c11, saturated_mul(sizes.at(0), sizes.at(1))))));
        }

        bool operator==(const cost_fun &other) const override
        {
            const auto &o = dynamic_cast<const with_interaction_in_x_and_y &>(other);
            return _c00 == o._c00 && _c01 == o._c01 && _c10 == o._c10 && _c11 == o._c11;
        }
    private:
        uint64_t _c00, _c01, _c10, _c11;
    };

    struct linear_in_max_yz: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            return saturated_add(_intercept, saturated_mul(_slope, std::max(sizes.at(1), sizes.at(2))));
        }
    };

    struct literal_in_y_or_linear_in_z: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_args &args) const override
        {
            if (args.size() < 2) [[unlikely]]
                throw error(fmt::format("cost_function {} requires two arguments but got {}", typeid(*this).name(), args.size()));
            if (const auto &y_val = args[1].as_int(); *y_val != 0)
                return _num_bytes_as_num_words(y_val);
            return saturated_add(_intercept, saturated_mul(_slope, sizes.at(2)));
        }
    };

    struct linear_in_y_and_z: cost_fun {
        linear_in_y_and_z(const arg_map &args):
            _intercept { std::stoull(args.at("arguments-intercept")) },
            _slope1 { std::stoull(args.at("arguments-slope1")) },
            _slope2 { std::stoull(args.at("arguments-slope2")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            return saturated_add(_intercept, saturated_add(saturated_mul(_slope1, sizes.at(1)), saturated_mul(_slope2, sizes.at(2))));
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _intercept == o._intercept && _slope1 == o._slope1 && _slope2 == o._slope2;
        }
    protected:
        const uint64_t _intercept, _slope1, _slope2;
    };

    struct quadratic_in_y: cost_fun {
        quadratic_in_y(const arg_map &args):
            _c0 { std::stoull(args.at("arguments-c0")) },
            _c1 { std::stoull(args.at("arguments-c1")) },
            _c2 { std::stoull(args.at("arguments-c2")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            const auto &y = sizes.at(1);
            return saturated_add(_c0, saturated_add(saturated_mul(_c1, y), saturated_mul(_c2, saturated_mul(y, y))));
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _c0 == o._c0 && _c1 == o._c1 && _c2 == o._c2;
        }
    protected:
        const uint64_t _c0, _c1, _c2;
    };

    struct quadratic_in_x: quadratic_in_y {
        using quadratic_in_y::quadratic_in_y;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            const auto &x = sizes.at(0);
            return saturated_add(_c0, saturated_add(saturated_mul(_c1, x), saturated_mul(_c2, saturated_mul(x, x))));
        }
    };

    struct quadratic_in_z: quadratic_in_y {
        using quadratic_in_y::quadratic_in_y;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            const auto &z = sizes.at(2);
            return saturated_add(_c0, saturated_add(saturated_mul(_c1, z), saturated_mul(_c2, saturated_mul(z, z))));
        }
    };

    struct quadratic_in_x_and_y: cost_fun {
        quadratic_in_x_and_y(const arg_map &args):
            _minimum { std::stoll(args.at("arguments-minimum")) },
            _c00 { std::stoll(args.at("arguments-c00")) },
            _c10 { std::stoll(args.at("arguments-c10")) },
            _c01 { std::stoll(args.at("arguments-c01")) },
            _c20 { std::stoll(args.at("arguments-c20")) },
            _c11 { std::stoll(args.at("arguments-c11")) },
            _c02 { std::stoll(args.at("arguments-c02")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            const auto x = static_cast<int64_t>(std::min(sizes.at(0), max_cost));
            const auto y = static_cast<int64_t>(std::min(sizes.at(1), max_cost));
            auto res = _c00;
            res = _saturated_add_signed(res, _saturated_mul_signed(_c10, x));
            res = _saturated_add_signed(res, _saturated_mul_signed(_c01, y));
            res = _saturated_add_signed(res, _saturated_mul_signed(_saturated_mul_signed(_c20, x), x));
            res = _saturated_add_signed(res, _saturated_mul_signed(_saturated_mul_signed(_c11, x), y));
            res = _saturated_add_signed(res, _saturated_mul_signed(_saturated_mul_signed(_c02, y), y));
            return _non_negative_cost(std::max(_minimum, res));
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _minimum == o._minimum && _c00 == o._c00 && _c10 == o._c10 && _c01 == o._c01
                && _c20 == o._c20 && _c11 == o._c11 && _c02 == o._c02;
        }
    protected:
        const int64_t _minimum, _c00, _c10, _c01, _c20, _c11, _c02;
    };

    struct added_sizes: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            if (sizes.size() != 2) [[unlikely]]
                throw error(fmt::format("added_sizes costing function requires exactly two arguments but got {}", sizes.size()));
            const auto sum = saturated_add(sizes.at(0), sizes.at(1));
            return saturated_add(_intercept, saturated_mul(_slope, sum));
        }
    };

    struct subtracted_sizes: linear_in_x {
        subtracted_sizes(const arg_map &args):
            linear_in_x { args },
            _minimum { std::stoull(args.at("arguments-minimum")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            const auto difference = sizes.at(0) > sizes.at(1) ? sizes.at(0) - sizes.at(1) : 0;
            return saturated_add(_intercept, saturated_mul(_slope, std::max(_minimum, difference)));
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _minimum == o._minimum && linear_in_x::operator==(o_);
        }
    protected:
        const uint64_t _minimum;
    };

    struct max_size: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            if (sizes.size() != 2) [[unlikely]]
                throw error(fmt::format("max_size costing function requires exactly two arguments but got {}", sizes.size()));
            const auto max = std::max(sizes.at(0), sizes.at(1));
            return saturated_add(_intercept, saturated_mul(_slope, max));
        }
    };

    struct min_size: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            if (sizes.size() != 2) [[unlikely]]
                throw error(fmt::format("min_size costing function requires exactly two arguments but got {}", sizes.size()));
            const auto min = std::min(sizes.at(0), sizes.at(1));
            return saturated_add(_intercept, saturated_mul(_slope, min));
        }
    };

    struct multiplied_sizes: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            if (sizes.size() != 2) [[unlikely]]
                throw error(fmt::format("multiplied_sizes costing function requires exactly two arguments but got {}", sizes.size()));
            const auto prod = saturated_mul(sizes.at(0), sizes.at(1));
            return saturated_add(_intercept, saturated_mul(_slope, prod));
        }
    };

    static cost_fun_ptr cost_fun_from_prefixed_args(const arg_map &prefixed_args, const std::string &prefix);

    struct const_above_diagonal: cost_fun {
        const_above_diagonal(const arg_map &args):
            _cost { std::stoull(args.at("arguments-constant")) },
            _model { cost_fun_from_prefixed_args(args, "arguments-model-") }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &args) const override
        {
            const auto &x = sizes.at(0);
            const auto &y = sizes.at(1);
            if (x < y)
                return _cost;
            return _model->cost(sizes, args);
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _cost == o._cost && _model && o._model && *_model == *o._model;
        }
    protected:
        const uint64_t _cost;
        const cost_fun_ptr _model;
    };

    struct const_below_diagonal: const_above_diagonal {
        using const_above_diagonal::const_above_diagonal;

        uint64_t cost(const arg_sizes &sizes, const value_args &args) const override
        {
            const auto &x = sizes.at(0);
            const auto &y = sizes.at(1);
            if (x > y)
                return _cost;
            return _model->cost(sizes, args);
        }
    };

    struct linear_on_diagonal: linear_in_x {
        linear_on_diagonal(const arg_map &args):
            linear_in_x { args },
            _cost { std::stoull(args.at("arguments-constant")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            const auto &x = sizes.at(0);
            const auto &y = sizes.at(1);
            if (x == y)
                return saturated_add(_intercept, saturated_mul(_slope, x));
            return _cost;
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _cost == o._cost && linear_in_x::operator==(o_);
        }
    protected:
        const uint64_t _cost;
    };

    struct above_and_below_diagonal: cost_fun {
        above_and_below_diagonal(const arg_map &args):
            _model { cost_fun_from_prefixed_args(args, "arguments-model-") }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &args) const override
        {
            if (sizes.size() != 2) [[unlikely]]
                throw error(fmt::format("above_and_below_diagonal costing function requires exactly two arguments but got {}", sizes.size()));
            const arg_sizes ordered { std::max(sizes.at(0), sizes.at(1)), std::min(sizes.at(0), sizes.at(1)) };
            return _model->cost(ordered, args);
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _model && o._model && *_model == *o._model;
        }
    private:
        const cost_fun_ptr _model;
    };

    struct exp_mod_cost: cost_fun {
        exp_mod_cost(const arg_map &args):
            _coefficient00 { std::stoull(args.at("arguments-coefficient00")) },
            _coefficient11 { std::stoull(args.at("arguments-coefficient11")) },
            _coefficient12 { std::stoull(args.at("arguments-coefficient12")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_args &) const override
        {
            if (sizes.size() != 3) [[unlikely]]
                throw error(fmt::format("exp_mod_cost costing function requires exactly three arguments but got {}", sizes.size()));
            const auto exponent_modulus = saturated_mul(sizes.at(1), sizes.at(2));
            const auto cost0 = saturated_add(_coefficient00,
                saturated_add(saturated_mul(_coefficient11, exponent_modulus),
                    saturated_mul(_coefficient12, saturated_mul(exponent_modulus, sizes.at(2)))));
            return sizes.at(0) <= sizes.at(2) ? cost0 : saturated_add(cost0, cost0 / 2);
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _coefficient00 == o._coefficient00 && _coefficient11 == o._coefficient11
                && _coefficient12 == o._coefficient12;
        }
    private:
        const uint64_t _coefficient00, _coefficient11, _coefficient12;
    };
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

    static cost_fun_ptr cost_fun_from_args(const arg_map &args)
    {
        const auto &typ = args.at("type");
        if (typ == "constant_cost")
            return std::make_shared<constant_cost>(args);
        if (typ == "added_sizes")
            return std::make_shared<added_sizes>(args);
        if (typ == "min_size")
            return std::make_shared<min_size>(args);
        if (typ == "max_size")
            return std::make_shared<max_size>(args);
        if (typ == "multiplied_sizes")
            return std::make_shared<multiplied_sizes>(args);
        if (typ == "linear_cost")
            return std::make_shared<linear_in_x>(args);
        if (typ == "linear_in_x")
            return std::make_shared<linear_in_x>(args);
        if (typ == "linear_in_y" || typ == "linear_in_y2")
            return std::make_shared<linear_in_y>(args);
        if (typ == "linear_in_z")
            return std::make_shared<linear_in_z>(args);
        if (typ == "linear_in_u")
            return std::make_shared<linear_in_u>(args);
        if (typ == "linear_in_x_and_y")
            return std::make_shared<linear_in_x_and_y>(args);
        if (typ == "with_interaction_in_x_and_y")
            return std::make_shared<with_interaction_in_x_and_y>(args);
        if (typ == "quadratic_in_x")
            return std::make_shared<quadratic_in_x>(args);
        if (typ == "quadratic_in_y")
            return std::make_shared<quadratic_in_y>(args);
        if (typ == "quadratic_in_z")
            return std::make_shared<quadratic_in_z>(args);
        if (typ == "quadratic_in_x_and_y")
            return std::make_shared<quadratic_in_x_and_y>(args);
        if (typ == "literal_in_y_or_linear_in_z")
            return std::make_shared<literal_in_y_or_linear_in_z>(args);
        if (typ == "linear_in_max_yz")
            return std::make_shared<linear_in_max_yz>(args);
        if (typ == "linear_in_y_and_z")
            return std::make_shared<linear_in_y_and_z>(args);
        if (typ == "subtracted_sizes")
            return std::make_shared<subtracted_sizes>(args);
        if (typ == "const_above_diagonal")
            return std::make_shared<const_above_diagonal>(args);
        if (typ == "const_below_diagonal")
            return std::make_shared<const_below_diagonal>(args);
        if (typ == "linear_on_diagonal")
            return std::make_shared<linear_on_diagonal>(args);
        if (typ == "above_and_below_diagonal")
            return std::make_shared<above_and_below_diagonal>(args);
        if (typ == "exp_mod_cost")
            return std::make_shared<exp_mod_cost>(args);
        throw error(fmt::format("unsupported cost model type: {}", typ));
    }

    static cost_fun_ptr cost_fun_from_prefixed_args(const arg_map &prefixed_args, const std::string &prefix)
    {
        arg_map args {};
        for (const auto &[k, v]: prefixed_args) {
            if (k.starts_with(prefix)) {
                const auto [it, created] = args.try_emplace(k.substr(prefix.size()), v);
                if (!created) [[unlikely]]
                    throw error(fmt::format("duplicate argument {}", k));
            }
        }
        return cost_fun_from_args(args);
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

    static op_model op_model_from_args(const arg_map &args)
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
        static auto default_sizer = std::make_shared<default_size_fun>();
        return { cost_fun_from_args(cpu_args), cost_fun_from_args(mem_args), default_sizer };
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

    static parsed_model parse(const arg_map &args)
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
        parsed_model m {};
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
                    const auto [op, created] = m.builtin_fun.try_emplace(tag, op_model_from_args(args));
                    if (!created) [[unlikely]]
                        throw error("internal error: duplicate tag in the parsed cost model!");
                    switch (tag) {
                        case builtin_tag::drop_list: {
                            static auto custom_fun = std::make_shared<literal_in_x_size_fun>();
                            op->size = custom_fun;
                            break;
                        }
                        case builtin_tag::replicate_byte: {
                            static auto custom_fun = std::make_shared<num_bytes_as_num_words_fun>();
                            op->size = custom_fun;
                            break;
                        }
                        case builtin_tag::insert_coin: {
                            static auto custom_fun = std::make_shared<value_max_depth_size_fun>(3);
                            op->size = custom_fun;
                            break;
                        }
                        case builtin_tag::lookup_coin: {
                            static auto custom_fun = std::make_shared<value_max_depth_size_fun>(2);
                            op->size = custom_fun;
                            break;
                        }
                        case builtin_tag::un_value_data: {
                            static auto custom_fun = std::make_shared<data_node_count_size_fun>();
                            op->size = custom_fun;
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

    const parsed_model &parsed_models::for_script(const cardano::script_type typ,
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

    parsed_models parse(const cardano::plutus_cost_models &models)
    {
        parsed_models res {};
        const auto *v1 = models.find(0);
        const auto *v2 = models.find(1);
        const auto *v3 = models.find(2);
        const auto add = [](parsed_models::variants &variants, const builtin_semantics semantics,
                const cardano::plutus_cost_model *params, const arg_map &defaults) {
            variants.at(static_cast<size_t>(semantics)).emplace(
                parse(params ? plutus_costs_to_args(*params, defaults) : defaults));
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

    const parsed_models &defaults()
    {
        static auto models = parse(cardano::config::get().plutus_all_cost_models);
        return models;
    }
}
