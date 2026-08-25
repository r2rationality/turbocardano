/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/plutus/builtins.hpp>
#include <turbo/plutus/machine.hpp>
#include <array>
#include <limits>

#if defined(_MSC_VER)
#define TURBO_PLUTUS_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define TURBO_PLUTUS_NOINLINE __attribute__((noinline))
#else
#define TURBO_PLUTUS_NOINLINE
#endif

namespace turbo::plutus {
    struct machine::impl {
        impl(allocator &alloc, const costs::runtime_model &model, const optional_budget &budget,
                const uint64_t protocol_major,
                const builtin_semantics semantics_variant):
            _alloc { alloc }, _cost_model { model }, _budget { budget },
            _protocol_major { protocol_major }, _semantics_variant { semantics_variant },
            _use_v2_semantics {
                semantics_variant == builtin_semantics::c || semantics_variant == builtin_semantics::e }
        {
        }

        cardano::ex_units evaluate_no_res(const term &expr)
        {
            _eval(expr);
            return _cost;
        }

        result evaluate(const term &expr)
        {
            const auto res_v = _eval(expr);
            return { _discharge(res_v), _cost };
        }
    private:
        allocator &_alloc;
        const costs::runtime_model &_cost_model;
        optional_budget _budget;
        cardano::ex_units _cost {};
        uint64_t _protocol_major;
        builtin_semantics _semantics_variant;
        const bool _use_v2_semantics;

        enum class step_kind: uint8_t {
            constant,
            variable,
            lambda,
            apply,
            delay,
            force,
            builtin,
            constr,
            acase,
            count
        };

        // Match Plutus Core's production CEK policy: fixed machine steps may
        // slip by a bounded amount, while startup and builtin costs stay immediate.
        static constexpr uint8_t _step_slippage = 200;
        static constexpr size_t _num_step_kinds = static_cast<size_t>(step_kind::count);
        static constexpr size_t _total_step_idx = _num_step_kinds;
        std::array<uint8_t, _num_step_kinds + 1> _step_counts {};

        value _eval(const term &expr)
        {
            _cost = {};
            _step_counts.fill(0);
            _spend(_cost_model.startup_op);
            const environment empty_env {};
            auto result = _compute(empty_env, expr);
            _spend_accumulated_steps();
            return result;
        }

        void _check_budget() const
        {
            if (_budget) {
                if (_cost.steps > _budget->steps) [[unlikely]]
                    throw error(fmt::format(
                        "plutus program CPU cost has exceeded its budget: consumed {} allowed {}",
                        _cost.steps, _budget->steps));
                if (_cost.mem > _budget->mem) [[unlikely]]
                    throw error(fmt::format(
                        "plutus program memory has exceeded its budget: consumed {} allowed {}",
                        _cost.mem, _budget->mem));
            }
        }

        void _spend(const uint64_t mem_cost, const uint64_t cpu_cost)
        {
            _cost.steps = costs::saturated_add(_cost.steps, cpu_cost);
            _cost.mem = costs::saturated_add(_cost.mem, mem_cost);
            //logger::info("SPEND cpu: {} mem: {} TOTAL cpu: {} mem: {}", cpu_cost, mem_cost, _cost.steps, _cost.mem);
            _check_budget();
        }

        void _spend(const cardano::ex_units &c)
        {
            _spend(c.mem, c.steps);
        }

        template<step_kind Kind>
        const cardano::ex_units &_step_cost() const
        {
            if constexpr (Kind == step_kind::constant)
                return _cost_model.constant_op;
            else if constexpr (Kind == step_kind::variable)
                return _cost_model.variable_op;
            else if constexpr (Kind == step_kind::lambda)
                return _cost_model.lambda_op;
            else if constexpr (Kind == step_kind::apply)
                return _cost_model.apply_op;
            else if constexpr (Kind == step_kind::delay)
                return _cost_model.delay_op;
            else if constexpr (Kind == step_kind::force)
                return _cost_model.force_op;
            else if constexpr (Kind == step_kind::builtin)
                return _cost_model.builtin_op;
            else if constexpr (Kind == step_kind::constr)
                return _cost_model.constr_op;
            else if constexpr (Kind == step_kind::acase)
                return _cost_model.case_op;
            else
                static_assert(Kind != step_kind::count, "the total step counter has no cost");
        }

        template<step_kind Kind>
        void _add_accumulated_step_cost(cardano::ex_units &cost) const
        {
            constexpr auto idx = static_cast<size_t>(Kind);
            const auto count = _step_counts[idx];
            const auto &step_cost = _step_cost<Kind>();
            cost.mem = costs::saturated_add(cost.mem, costs::saturated_mul(step_cost.mem, count));
            cost.steps = costs::saturated_add(cost.steps, costs::saturated_mul(step_cost.steps, count));
        }

        TURBO_PLUTUS_NOINLINE void _spend_accumulated_steps()
        {
            if (_step_counts[_total_step_idx] == 0)
                return;
            cardano::ex_units cost {};
            _add_accumulated_step_cost<step_kind::constant>(cost);
            _add_accumulated_step_cost<step_kind::variable>(cost);
            _add_accumulated_step_cost<step_kind::lambda>(cost);
            _add_accumulated_step_cost<step_kind::apply>(cost);
            _add_accumulated_step_cost<step_kind::delay>(cost);
            _add_accumulated_step_cost<step_kind::force>(cost);
            _add_accumulated_step_cost<step_kind::builtin>(cost);
            _add_accumulated_step_cost<step_kind::constr>(cost);
            _add_accumulated_step_cost<step_kind::acase>(cost);
            _step_counts.fill(0);
            _spend(cost);
        }

        template<step_kind Kind>
        void _step()
        {
            constexpr auto idx = static_cast<size_t>(Kind);
            ++_step_counts[idx];
            auto &total = _step_counts[_total_step_idx];
            ++total;
            if (total >= _step_slippage) [[unlikely]]
                _spend_accumulated_steps();
        }

        template<step_kind Kind>
        void _steps(size_t count)
        {
            constexpr auto idx = static_cast<size_t>(Kind);
            while (count > 0) {
                const auto total = static_cast<size_t>(_step_counts[_total_step_idx]);
                const auto room = static_cast<size_t>(_step_slippage) - total;
                const auto take = std::min(count, room);
                _step_counts[idx] = static_cast<uint8_t>(_step_counts[idx] + take);
                _step_counts[_total_step_idx] = static_cast<uint8_t>(total + take);
                count -= take;
                if (_step_counts[_total_step_idx] >= _step_slippage) [[unlikely]]
                    _spend_accumulated_steps();
            }
        }

        static const value &_arg(const value_args &args, const size_t idx)
        {
            return args[idx];
        }

        static void _check_integer_range(const value &arg, const cpp_int &lower, const cpp_int &upper,
                const std::string_view what)
        {
            const auto &i = *arg.as_int();
            if (i < lower || i > upper)
                throw error(fmt::format("{} is out of bounds: {}", what, i));
        }

        static void _check_cardano_integer(const value &arg)
        {
            static const cpp_int lower = -(cpp_int { 1 } << 262143);
            static const cpp_int upper = (cpp_int { 1 } << 262143) - 1;
            _check_integer_range(arg, lower, upper, "integer");
        }

        static void _check_cardano_bytestring(const value &arg)
        {
            if (arg.as_bstr()->size() > 65536)
                throw error(fmt::format("bytestring size {} exceeds the protocol limit of 65536", arg.as_bstr()->size()));
        }

        void _check_ensurable_args(const builtin_tag tag, const value_args &args) const
        {
            if (_semantics_variant != builtin_semantics::d && _semantics_variant != builtin_semantics::e)
                return;
            switch (tag) {
                case builtin_tag::add_integer:
                case builtin_tag::subtract_integer:
                case builtin_tag::multiply_integer:
                case builtin_tag::divide_integer:
                case builtin_tag::quotient_integer:
                case builtin_tag::remainder_integer:
                case builtin_tag::mod_integer:
                case builtin_tag::less_than_integer:
                case builtin_tag::less_than_equals_integer:
                    _check_cardano_integer(_arg(args, 0));
                    _check_cardano_integer(_arg(args, 1));
                    break;
                case builtin_tag::append_byte_string:
                case builtin_tag::equals_byte_string:
                case builtin_tag::less_than_byte_string:
                case builtin_tag::less_than_equals_byte_string:
                    _check_cardano_bytestring(_arg(args, 0));
                    _check_cardano_bytestring(_arg(args, 1));
                    break;
                case builtin_tag::cons_byte_string:
                    if (_semantics_variant == builtin_semantics::d)
                        _check_cardano_integer(_arg(args, 0));
                    _check_cardano_bytestring(_arg(args, 1));
                    break;
                case builtin_tag::slice_byte_string:
                    _check_cardano_bytestring(_arg(args, 2));
                    break;
                case builtin_tag::index_byte_string:
                case builtin_tag::sha2_256:
                case builtin_tag::sha3_256:
                case builtin_tag::blake2b_256:
                case builtin_tag::decode_utf8:
                case builtin_tag::bls12_381_g1_hash_to_group:
                case builtin_tag::bls12_381_g2_hash_to_group:
                case builtin_tag::keccak_256:
                case builtin_tag::blake2b_224:
                case builtin_tag::complement_byte_string:
                case builtin_tag::read_bit:
                case builtin_tag::count_set_bits:
                case builtin_tag::find_first_set_bit:
                case builtin_tag::ripemd_160:
                    _check_cardano_bytestring(_arg(args, 0));
                    break;
                case builtin_tag::verify_ed25519_signature:
                case builtin_tag::verify_schnorr_secp_256k1_signature:
                case builtin_tag::byte_string_to_integer:
                    _check_cardano_bytestring(_arg(args, 1));
                    break;
                case builtin_tag::and_byte_string:
                case builtin_tag::or_byte_string:
                case builtin_tag::xor_byte_string:
                    _check_cardano_bytestring(_arg(args, 1));
                    _check_cardano_bytestring(_arg(args, 2));
                    break;
                case builtin_tag::constr_data: {
                    static const cpp_int upper = (cpp_int { 1 } << 64) - 1;
                    _check_integer_range(_arg(args, 0), 0, upper, "data constructor tag");
                    break;
                }
                case builtin_tag::bls12_381_g1_scalar_mul:
                case builtin_tag::bls12_381_g2_scalar_mul: {
                    static const cpp_int lower = -(cpp_int { 1 } << 4095);
                    static const cpp_int upper = (cpp_int { 1 } << 4095) - 1;
                    _check_integer_range(_arg(args, 0), lower, upper, "BLS scalar");
                    break;
                }
                case builtin_tag::write_bits:
                    if (_arg(args, 0).as_bstr()->size() > 4096)
                        throw error(fmt::format("writeBits input size {} exceeds the protocol limit of 4096",
                            _arg(args, 0).as_bstr()->size()));
                    break;
                case builtin_tag::shift_byte_string:
                case builtin_tag::rotate_byte_string:
                    _check_integer_range(_arg(args, 1), std::numeric_limits<int64_t>::min(),
                        std::numeric_limits<int64_t>::max(), "bit shift");
                    break;
                default: break;
            }
        }

        void _spend(const builtin_tag tag, const value_args &args)
        {
            _check_ensurable_args(tag, args);
            const auto &builtin_cost = _cost_model.builtin_costs.at(tag);
            const bool text_costed_by_byte_length =
                _semantics_variant == builtin_semantics::d || _semantics_variant == builtin_semantics::e;
            cardano::ex_units cost {};
            try {
                cost = costs::cost_builtin(builtin_cost, tag, args, text_costed_by_byte_length);
            } catch (const std::exception &ex) {
                throw error(fmt::format("failed to cost builtin {} with {} arguments: {}", tag, args.size(), ex.what()));
            }
            _spend(cost.mem, cost.steps);
        }

        const value *_lookup_opt(const environment &env, size_t idx) const
        {
            auto node = env.get();
            while (node && idx > 0) {
                node = node->parent.get();
                --idx;
            }
            return node ? &node->val : nullptr;
        }

        value _lookup(const environment &env, const size_t idx) const
        {
            if (const auto *val = _lookup_opt(env, idx); val)
                return *val;
            throw error(fmt::format("reference to a free variable: v{}", idx));
        }

        term _discharge_term(const environment &env, const term &t, const size_t local_depth,
                const size_t outer_depth) const
        {
            return t.visit([&](const auto &v) -> term {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, variable>) {
                    if (v.idx < local_depth)
                        return t;
                    if (const auto *val = _lookup_opt(env, v.idx - local_depth); val)
                        return _discharge(*val, outer_depth + local_depth);
                    return term { _alloc, variable { v.idx - env.size() + outer_depth } };
                } else if constexpr (std::is_same_v<T, t_lambda>) {
                    return term { _alloc, t_lambda { _discharge_term(env, v.expr, local_depth + 1, outer_depth) } };
                } else if constexpr (std::is_same_v<T, apply>) {
                    return term { _alloc, apply { _discharge_term(env, v.func, local_depth, outer_depth),
                        _discharge_term(env, v.arg, local_depth, outer_depth) } };
                } else if constexpr (std::is_same_v<T, t_builtin_spine>) {
                    boost::container::static_vector<term, builtin_args::max_size> args {};
                    for (const auto &arg: v.args)
                        args.emplace_back(_discharge_term(env, arg, local_depth, outer_depth));
                    return term::builtin_spine(_alloc, v.b.tag, v.forces,
                        { args.data(), args.size() });
                } else if constexpr (std::is_same_v<T, force>) {
                    return term { _alloc, force { _discharge_term(env, v.expr, local_depth, outer_depth) } };
                } else if constexpr (std::is_same_v<T, t_delay>) {
                    return term { _alloc, t_delay { _discharge_term(env, v.expr, local_depth, outer_depth) } };
                } else if constexpr (std::is_same_v<T, t_case>) {
                    term_list::value_type l { _alloc };
                    l.reserve(v.cases->size());
                    for (auto &c: *v.cases)
                        l.emplace_back(_discharge_term(env, c, local_depth, outer_depth));
                    return term { _alloc, t_case { _discharge_term(env, v.arg, local_depth, outer_depth),
                        term_list { _alloc, std::move(l) } } };
                } else if constexpr (std::is_same_v<T, t_constr>) {
                    term_list::value_type l { _alloc };
                    l.reserve(v.args->size());
                    for (auto &a: *v.args)
                        l.emplace_back(_discharge_term(env, a, local_depth, outer_depth));
                    return term { _alloc, t_constr { v.tag, term_list { _alloc, std::move(l) } } };
                } else {
                    return t;
                }
            });
        }

        term _discharge(const value &val, const size_t outer_depth=0) const
        {
            return val.visit([&](const auto &v) -> term {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, constant>) {
                    return term { _alloc, v };
                } else if constexpr (std::is_same_v<T, v_delay>) {
                    return term { _alloc, t_delay { _discharge_term(v.env, v.expr, 0, outer_depth) } };
                } else if constexpr (std::is_same_v<T, v_lambda>) {
                    return term { _alloc, t_lambda { _discharge_term(v.env, v.body, 1, outer_depth) } };
                } else if constexpr (std::is_same_v<T, v_builtin>) {
                    auto t = term { _alloc, v.b };
                    for (size_t i = 0; i < v.forces; ++i)
                        t = term { _alloc, force { std::move(t) } };
                    for (const auto &arg: v.args.values())
                        t = term { _alloc, apply { std::move(t), _discharge(arg, outer_depth) } };
                    return t;
                } else if constexpr (std::is_same_v<T, v_constr>) {
                    term_list::value_type args { _alloc };
                    args.reserve(v.args->size());
                    for (const auto &arg: *v.args)
                        args.emplace_back(_discharge(arg, outer_depth));
                    t_constr pc { v.tag, term_list { _alloc, std::move(args) } };
                    return term { _alloc, std::move(pc) };
                } else {
                    throw error(fmt::format("an unsupported value type to discharge: {}", typeid(v).name()));
                }
            });
        }

        template<size_t Arity, auto Function>
        value _invoke_builtin(const value_args &args)
        {
            if constexpr (Arity == 1) {
                return Function(_alloc, args[0]);
            } else if constexpr (Arity == 2) {
                return Function(_alloc, args[0], args[1]);
            } else if constexpr (Arity == 3) {
                return Function(_alloc, args[0], args[1], args[2]);
            } else if constexpr (Arity == 4) {
                return Function(_alloc, args[0], args[1], args[2], args[3]);
            } else if constexpr (Arity == 6) {
                return Function(_alloc, args[0], args[1], args[2], args[3], args[4], args[5]);
            } else {
                static_assert(Arity == 1 || Arity == 2 || Arity == 3 || Arity == 4 || Arity == 6,
                    "unsupported builtin arity");
            }
        }

        template<builtin_tag Tag, size_t Arity, size_t PolymorphicArgs, auto Function>
        value _apply_builtin_impl(const value_args args, const size_t forces)
        {
            if (args.size() != Arity) [[unlikely]]
                throw error(fmt::format("can't apply builtin {} to {} arguments: {} arguments are required!",
                    Tag, args.size(), Arity));
            if (forces != PolymorphicArgs) [[unlikely]]
                throw error(fmt::format("can't apply builtin {} with {} forces: {} forces are required!",
                    Tag, forces, PolymorphicArgs));
            _spend(Tag, args);
            if constexpr (Tag == builtin_tag::cons_byte_string) {
                if (_use_v2_semantics)
                    return _invoke_builtin<Arity, builtins::cons_byte_string_v2>(args);
            }
            return _invoke_builtin<Arity, Function>(args);
        }

        value _apply_builtin(const t_builtin &builtin, const value_args args, const size_t forces)
        {
            switch (builtin.tag) {
#define TURBO_PLUTUS_BUILTIN(tag, arity, function, name, polymorphic, batch) \
                case builtin_tag::tag: \
                    return _apply_builtin_impl<builtin_tag::tag, arity, polymorphic, builtins::function>(args, forces);
#include <turbo/plutus/builtin-registry.inc>
#undef TURBO_PLUTUS_BUILTIN
                default: throw error(fmt::format("not implemented: {}", static_cast<uint64_t>(builtin.tag)));
            }
        }

        value _apply(const value &func, const value &arg)
        {
            return func.visit([&arg, this](const auto &f) {
                using T = std::decay_t<decltype(f)>;
                if constexpr (std::is_same_v<T, v_lambda>) {
                    const environment new_env { _alloc, f.env, arg };
                    return _compute(new_env, f.body);
                }
                if constexpr (std::is_same_v<T, v_builtin>) {
                    v_builtin new_b { f.b, { _alloc, f.args, arg }, f.forces };
                    const auto &descriptor = builtins::descriptor(new_b.b.tag);
                    if (descriptor.polymorphic_args != new_b.forces)
                        throw error(fmt::format("an application of an polymorphic builtin with an incorrect number of forces: {}", new_b.b.tag));
                    if (new_b.args.size() < descriptor.num_args) [[likely]]
                        return value { _alloc, std::move(new_b) };
                    //logger::info("{} {}", new_b.b.tag, new_b.args);
                    auto res = _apply_builtin(new_b.b, new_b.args.values(), new_b.forces);
                    //logger::info("{} => {}", new_b.b.tag, res);
                    return res;
                }
                throw error(fmt::format("only lambdas and builtins can be applied but got: {}", typeid(T).name()));
                return value { _alloc, constant { _alloc, std::monostate {} } };
            });
        }

        value _force(const value &val)
        {
            return val.visit([this](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, v_delay>)
                    return _compute(v.env, v.expr);
                if constexpr (std::is_same_v<T, v_builtin>) {
                    const auto polymorphic_args = builtins::descriptor(v.b.tag).polymorphic_args;
                    if (v.forces < polymorphic_args) {
                        auto new_b = v;
                        ++new_b.forces;
                        return value { _alloc, std::move(new_b) };
                    }
                    throw error(fmt::format("an unexpected force of a builtin: {} polymorphic_args: {} num_forces: {}",
                        v.b.tag, polymorphic_args, v.forces));
                }
                throw error(fmt::format("unsupported value for force: {}", typeid(T).name()));
                return value { _alloc, constant { _alloc, std::monostate {} } };
            });
        }

        value _compute(const environment &env, const variable &e)
        {
            _step<step_kind::variable>();
            return _lookup(env, e.idx);
        }

        value _compute(const environment &, const constant &e)
        {
            _step<step_kind::constant>();
            return { _alloc, e };
        }

        value _compute(const environment &env, const t_lambda &e)
        {
            _step<step_kind::lambda>();
            return { _alloc, v_lambda { env, e.expr } };
        }

        value _compute(const environment &env, const t_delay &e)
        {
            _step<step_kind::delay>();
            return { _alloc, v_delay { env, e.expr } };
        }

        value _compute(const environment &, const t_builtin &e)
        {
            _step<step_kind::builtin>();
            return { _alloc, v_builtin { e, {} } };
        }

        value _compute(const environment &env, const t_builtin_spine &e)
        {
            _steps<step_kind::apply>(e.args.size());
            _steps<step_kind::force>(e.forces);
            _step<step_kind::builtin>();

            const auto &descriptor = builtins::descriptor(e.b.tag);
            boost::container::static_vector<value, builtin_args::max_size> args {};
            for (const auto &arg: e.args)
                args.emplace_back(_compute(env, arg));
            const value_args arg_view { args.data(), args.size() };
            if (e.forces != descriptor.polymorphic_args) [[unlikely]]
                throw error(fmt::format(
                    "an application of an polymorphic builtin with an incorrect number of forces: {}", e.b.tag));
            if (args.size() < descriptor.num_args) {
                return value { _alloc, v_builtin {
                    e.b, builtin_args { _alloc, arg_view }, e.forces
                } };
            }
            return _apply_builtin(e.b, arg_view, e.forces);
        }

        value _compute(const environment &env, const force &e)
        {
            _step<step_kind::force>();
            return _force(_compute(env, e.expr));
        }

        value _compute(const environment &env, const apply &e)
        {
            _step<step_kind::apply>();
            const auto fun = _compute(env, e.func);
            const auto arg = _compute(env, e.arg);
            return _apply(fun, arg);
        }

        value _compute(const environment &env, const t_constr &e)
        {
            _step<step_kind::constr>();
            value_list::value_type v_args { _alloc };
            v_args.reserve(e.args->size());
            for (const auto &arg: *e.args)
                v_args.emplace_back(_compute(env, arg));
            return value { _alloc, v_constr { e.tag, { _alloc, std::move(v_args) } } };
        }

        value _case_branch(const environment &env, const t_case &e, const size_t idx)
        {
            if (idx >= e.cases->size()) [[unlikely]]
                throw error(fmt::format("case branch index {} is out of bounds for {} branches", idx, e.cases->size()));
            return _compute(env, *std::next(e.cases->begin(), idx));
        }

        value _case_branch_const(const environment &env, const t_case &e, const constant &c)
        {
            if (_protocol_major < machine::builtin_case_protocol_major) [[unlikely]] {
                throw error(fmt::format(
                    "case on builtin constants requires protocol version {} or later but got {}",
                    machine::builtin_case_protocol_major, _protocol_major));
            }
            const auto num_branches = e.cases->size();
            return std::visit<value>([&](const auto &v) -> value {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    if (num_branches == 1) [[likely]]
                        return _case_branch(env, e, 0);
                    throw error(fmt::format("casing on unit requires exactly one branch but got {}", num_branches));
                } else if constexpr (std::is_same_v<T, bool>) {
                    if (!v && (num_branches == 1 || num_branches == 2))
                        return _case_branch(env, e, 0);
                    if (v && num_branches == 2)
                        return _case_branch(env, e, 1);
                    throw error(fmt::format("builtin bool value {} has no matching case branch", v));
                } else if constexpr (std::is_same_v<T, bint_type>) {
                    if (*v >= 0 && *v < num_branches)
                        return _case_branch(env, e, static_cast<size_t>(*v));
                    throw error(fmt::format("builtin integer value {} has no matching case branch", *v));
                } else if constexpr (std::is_same_v<T, constant_list>) {
                    if (num_branches != 1 && num_branches != 2) [[unlikely]]
                        throw error(fmt::format("casing on list requires exactly one or two branches but got {}", num_branches));
                    if (v.empty()) {
                        if (num_branches == 2)
                            return _case_branch(env, e, 1);
                        throw error("expected a non-empty list when casing with one branch");
                    }
                    auto res = _case_branch(env, e, 0);
                    res = _apply(res, value { _alloc, v.front() });
                    const auto tail_val = value { _alloc, constant { _alloc, v.drop(_alloc, 1) } };
                    return _apply(res, tail_val);
                } else if constexpr (std::is_same_v<T, constant_pair>) {
                    if (num_branches != 1) [[unlikely]]
                        throw error(fmt::format("casing on pair requires exactly one branch but got {}", num_branches));
                    auto res = _case_branch(env, e, 0);
                    res = _apply(res, value { _alloc, v->first });
                    return _apply(res, value { _alloc, v->second });
                } else {
                    throw error(fmt::format("builtin constant type {} is not supported in case", typeid(T).name()));
                }
            }, *c);
        }

        value _compute(const environment &env, const t_case &e)
        {
            _step<step_kind::acase>();
            const auto v_arg = _compute(env, e.arg);
            return v_arg.visit([&](const auto &v) -> value {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, v_constr>) {
                    auto res = _case_branch(env, e, v.tag);
                    for (const auto &arg: *v.args)
                        res = _apply(res, arg);
                    return res;
                } else if constexpr (std::is_same_v<T, constant>) {
                    return _case_branch_const(env, e, v);
                } else {
                    throw error(fmt::format("case requires a constructor or builtin constant but got {}", typeid(T).name()));
                }
            });
        }

        value _compute(const environment &, const failure &)
        {
            throw error("the plutus script reported an error!");
        }

        value _compute(const environment &env, const term &t)
        {
            return t.visit([&env, this](const auto &e) {
                return _compute(env, e);
            });
        }
    };

    machine::machine(allocator &alloc, const cardano::script_type typ, const optional_budget &budget,
            const uint64_t protocol_major):
        machine { alloc, costs::defaults().for_script(typ, builtins::semantics_variant(typ, protocol_major)), typ, budget, protocol_major }
    {
    }

    machine::machine(allocator &alloc, const costs::runtime_model &model, const cardano::script_type typ,
            const optional_budget &budget, const uint64_t protocol_major):
        _impl { std::make_unique<impl>(alloc, model, budget, protocol_major,
            builtins::semantics_variant(typ, protocol_major)) }
    {
    }

    machine::~machine() =default;

    machine::result machine::evaluate(const term &expr)
    {
        return _impl->evaluate(expr);
    }

    cardano::ex_units machine::evaluate_no_res(const term &expr)
    {
        return _impl->evaluate_no_res(expr);
    }
}

#undef TURBO_PLUTUS_NOINLINE
