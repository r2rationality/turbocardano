/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/config.hpp>
#include <turbo/plutus/costs.hpp>
#include <turbo/plutus/machine.hpp>

namespace turbo::plutus::costs {
    static uint64_t _mem_usage(const value::value_type &val);
    static uint64_t _mem_usage(const data &d);

    static uint64_t _mem_usage(const bint_type &i)
    {
        if (*i > 0)
            return boost::multiprecision::msb(*i) / 64 + 1;
        if (*i < 0) {
            if (const auto i_adj = *i + 1; i_adj != 0) [[likely]]
                return boost::multiprecision::msb(i_adj * -1) / 64 + 1;
            return 1;
        }
        return 1;
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
            sum += _mem_usage(d);
        return sum;
    }

    static uint64_t _mem_usage(const data &d)
    {
        return std::visit([](const auto &v) {
            using t = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<t, data_constr>) {
                return 4 + _mem_usage(v->second);
            } else if constexpr (std::is_same_v<t, data::map_type>) {
                uint64_t sum = 4;
                for (const auto &p: v)
                    sum += _mem_usage(p->first) + _mem_usage(p->second);
                return sum;
            } else if constexpr (std::is_same_v<t, data::bstr_type>) {
                return 4 + _mem_usage(*v);
            } else {
                return 4 + _mem_usage(v);
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
                return static_cast<uint64_t>(sizeof(bls12_381_g1_element) / 8);
            } else if constexpr (std::is_same_v<T, bls12_381_g2_element>) {
                return static_cast<uint64_t>(sizeof(bls12_381_g2_element) / 8);
            } else if constexpr (std::is_same_v<T, bls12_381_ml_result>) {
                return static_cast<uint64_t>(sizeof(bls12_381_ml_result) / 8);
            } else if constexpr (std::is_same_v<T, constant_list>) {
                uint64_t sum = 0;
                for (const auto &ci: v->vals)
                    sum += _mem_usage(ci);
                return sum;
            } else if constexpr (std::is_same_v<T, constant_pair>) {
                return _mem_usage(v->first) + _mem_usage(v->second);
            } else {
                return _mem_usage(v);
            }
        }, *c);
    }

    static uint64_t _mem_usage(const value::value_type &val)
    {
        if (std::holds_alternative<constant>(val))
            return _mem_usage(std::get<constant>(val));
        return 1;
    }

    arg_sizes default_size_fun::size(const value_list &args) const
    {
        arg_sizes sizes {};
        for (const auto &arg: *args) {
            sizes.emplace_back(_mem_usage(*arg));
        }
        return sizes;
    };

    arg_sizes sizes_for(const op_model &model, const builtin_tag tag, const value_list &args,
            const bool text_costed_by_byte_length)
    {
        auto sizes = model.size->size(args);
        if (text_costed_by_byte_length
                && (tag == builtin_tag::append_string || tag == builtin_tag::equals_string
                    || tag == builtin_tag::encode_utf8)) {
            sizes.clear();
            for (const auto &arg: *args)
                sizes.emplace_back(arg.as_str()->size() / 4);
        }
        return sizes;
    }

    arg_sizes num_bytes_as_num_words_fun::size(const value_list &args) const
    {
        arg_sizes sizes {};
        for (const auto &arg: *args) {
            const auto bytes = static_cast<size_t>(*arg.as_int());
            sizes.emplace_back(bytes ? (bytes - 1) / 8 + 1 : 0);
        }
        return sizes;
    };

    // the names of the classes match the names in the builtinCostModel config JSON
    struct constant_cost: cost_fun {
        constant_cost(const arg_map &args)
            : _cost { std::stoull(args.at("arguments")) }
        {
        }

        uint64_t cost(const arg_sizes &, const value_list &) const override
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

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            return _intercept + _slope * sizes.at(0);
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

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            return _intercept + _slope * sizes.at(1);
        }
    };

    struct linear_in_z: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            return _intercept + _slope * sizes.at(2);
        }
    };

    struct linear_in_max_yz: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            return _intercept + _slope * std::max(sizes.at(1), sizes.at(2));
        }
    };

    struct literal_in_y_or_linear_in_z: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_list &args) const override
        {
            if (args->size() < 2) [[unlikely]]
                throw error(fmt::format("cost_function {} requires two arguments but got {}", typeid(*this).name(), args->size()));
            if (const auto &y_val = static_cast<uint64_t>(*std::next(args->begin())->as_int()); y_val != 0)
                return (y_val + 7) / 8;
            return _intercept + _slope * sizes.at(2);
        }
    };

    struct linear_in_y_and_z: cost_fun {
        linear_in_y_and_z(const arg_map &args):
            _intercept { std::stoull(args.at("arguments-intercept")) },
            _slope1 { std::stoull(args.at("arguments-slope1")) },
            _slope2 { std::stoull(args.at("arguments-slope2")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            return _intercept + _slope1 * sizes.at(1) + _slope2 * sizes.at(2);
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

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            const auto &y = sizes.at(1);
            return _c0 + _c1 * y + _c2 * y * y;
        }

        bool operator==(const cost_fun &o_) const override
        {
            const auto &o = dynamic_cast<decltype(*this) &>(o_);
            return _c0 == o._c0 && _c1 == o._c1 && _c2 == o._c2;
        }
    protected:
        const uint64_t _c0, _c1, _c2;
    };

    struct quadratic_in_z: quadratic_in_y {
        using quadratic_in_y::quadratic_in_y;

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            const auto &z = sizes.at(2);
            return _c0 + _c1 * z + _c2 * z * z;
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

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            const int64_t x = sizes.at(0);
            const int64_t y = sizes.at(1);
            const int64_t res = std::max(_minimum, _c00 + _c10 * x + _c01 * y + _c20 * x * x + _c11 * x * y + _c02 * y * y);
            if (res >= 0) [[likely]]
                return static_cast<uint64_t>(res);
            throw error("quadratic_in_x_and_y results in a negative cost!");
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

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            if (sizes.size() != 2) [[unlikely]]
                throw error("added_sizes costing function requires at least two arguments");
            const auto sum = std::accumulate(sizes.begin(), sizes.end(), uint64_t { 0 });
            return _intercept + _slope * sum;
        }
    };

    struct subtracted_sizes: linear_in_x {
        subtracted_sizes(const arg_map &args):
            linear_in_x { args },
            _minimum { std::stoull(args.at("arguments-minimum")) }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            return _intercept + _slope * std::max(_minimum, (sizes.at(0) - sizes.at(1)));
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

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            if (sizes.size() != 2) [[unlikely]]
                throw error("max_size costing function requires at least two arguments");
            const auto max = std::max_element(sizes.begin(), sizes.end());
            return _intercept + _slope * (*max);
        }
    };

    struct min_size: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            if (sizes.size() != 2) [[unlikely]]
                throw error("max_size costing function requires at least two arguments");
            const auto min = std::min_element(sizes.begin(), sizes.end());
            return _intercept + _slope * (*min);
        }
    };

    struct multiplied_sizes: linear_in_x {
        using linear_in_x::linear_in_x;

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            if (sizes.size() != 2) [[unlikely]]
                throw error("added_sizes costing function requires at least two arguments");
            const auto prod = std::accumulate(sizes.begin(), sizes.end(), uint64_t { 1 }, std::multiplies<uint64_t>());
            return _intercept + _slope * prod;
        }
    };

    static cost_fun_ptr cost_fun_from_prefixed_args(const arg_map &prefixed_args, const std::string &prefix);

    struct const_above_diagonal: cost_fun {
        const_above_diagonal(const arg_map &args):
            _cost { std::stoull(args.at("arguments-constant")) },
            _model { cost_fun_from_prefixed_args(args, "arguments-model-") }
        {
        }

        uint64_t cost(const arg_sizes &sizes, const value_list &args) const override
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

        uint64_t cost(const arg_sizes &sizes, const value_list &args) const override
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

        uint64_t cost(const arg_sizes &sizes, const value_list &) const override
        {
            const auto &x = sizes.at(0);
            const auto &y = sizes.at(1);
            if (x == y)
                return _intercept + _slope * x;
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
        if (typ == "linear_in_y")
            return std::make_shared<linear_in_y>(args);
        if (typ == "linear_in_z")
            return std::make_shared<linear_in_z>(args);
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
                    const auto [it, created] = m.builtin_fun.try_emplace(tag, op_model_from_args(args));
                    if (!created) [[unlikely]]
                        throw error("internal error: duplicate tag in the parsed cost model!");
                    switch (tag) {
                        case builtin_tag::replicate_byte: {
                            static auto custom_fun = std::make_shared<num_bytes_as_num_words_fun>();
                            it->second.size = custom_fun;
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

    const parsed_model &parsed_models::for_script(const cardano::script_type typ) const
    {
        switch (typ) {
            case cardano::script_type::plutus_v1: return v1.value();
            case cardano::script_type::plutus_v2: return v2.value();
            case cardano::script_type::plutus_v3: return v3.value();
            default: throw error(fmt::format("unsupported script type: {}", static_cast<int>(typ)));
        }
    }

    const arg_map &default_cost_args_v1()
    {
        static auto args = load_cost_args(install_path("etc/plutus/cekMachineCostsA.json"), install_path("etc/plutus/builtinCostModelA.json"));
        return args;
    }

    const arg_map &default_cost_args_v2()
    {
        static auto args = load_cost_args(install_path("etc/plutus/cekMachineCostsB.json"), install_path("etc/plutus/builtinCostModelB.json"));
        return args;
    }

    const arg_map &default_cost_args_v3()
    {
        static auto args = load_cost_args(install_path("etc/plutus/cekMachineCostsC.json"), install_path("./etc/plutus/builtinCostModelC.json"));
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
        res.v1.emplace(parse(v1 ? plutus_costs_to_args(*v1, default_cost_args_v1()) : default_cost_args_v1()));
        res.v2.emplace(parse(v2 ? plutus_costs_to_args(*v2, default_cost_args_v2()) : default_cost_args_v2()));
        res.v3.emplace(parse(v3 ? plutus_costs_to_args(*v3, default_cost_args_v3()) : default_cost_args_v3()));
        return res;
    }

    const parsed_models &defaults()
    {
        static auto models = parse(cardano::config::get().plutus_all_cost_models);
        return models;
    }
}
