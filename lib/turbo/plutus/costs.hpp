#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <turbo/cardano/common/types.hpp>
#include <turbo/plutus/builtins.hpp>

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

    // Runtime cost models contain only execution-ready formulas. Parsing maps and strings are
    // confined to the ingestion API in costs-config.hpp.
    enum class runtime_cost_kind: uint8_t {
        invalid,
        constant,
        linear_in_x,
        linear_in_y,
        linear_in_z,
        linear_in_u,
        linear_in_x_and_y,
        linear_in_y_and_z,
        linear_in_max_yz,
        with_interaction_in_x_and_y,
        quadratic_in_x,
        quadratic_in_y,
        quadratic_in_z,
        quadratic_in_x_and_y,
        literal_in_y_or_linear_in_z,
        added_sizes,
        subtracted_sizes,
        max_size,
        min_size,
        multiplied_sizes,
        const_above_diagonal,
        const_below_diagonal,
        above_and_below_diagonal,
        linear_on_diagonal,
        exp_mod
    };

    struct runtime_cost {
        // Nested formula parameters occupy args[0..6]. Constant-diagonal formulas keep their
        // eighth independent value in builtin_cost, outside the common 64-byte formula block.
        runtime_cost_kind kind = runtime_cost_kind::invalid;
        runtime_cost_kind nested_kind = runtime_cost_kind::invalid;
        std::array<uint64_t, 7> args {};

        bool operator==(const runtime_cost &) const =default;
    };

    enum class runtime_size_kind: uint8_t {
        default_size,
        num_bytes_as_num_words,
        literal_in_x,
        value_max_depth,
        data_node_count
    };

    struct builtin_cost {
        runtime_cost cpu {};
        runtime_cost mem {};
        uint64_t cpu_diagonal_constant = 0;
        uint64_t mem_diagonal_constant = 0;
        runtime_size_kind size = runtime_size_kind::default_size;
        uint8_t size_index = 0;

        bool operator==(const builtin_cost &) const =default;
    };

    struct builtin_cost_table {
        const builtin_cost &at(const builtin_tag tag) const
        {
            const auto &model = _models[static_cast<size_t>(tag)];
            if (model.cpu.kind == runtime_cost_kind::invalid)
                throw std::out_of_range("builtin has no cost model");
            return model;
        }

        std::pair<builtin_cost *, bool> try_emplace(const builtin_tag tag, builtin_cost &&model)
        {
            auto &slot = _models[static_cast<size_t>(tag)];
            if (slot.cpu.kind != runtime_cost_kind::invalid)
                return { &slot, false };
            slot = std::move(model);
            return { &slot, true };
        }

    private:
        std::array<builtin_cost, builtin_tag_count> _models {};
    };

    extern cardano::ex_units cost_builtin(const builtin_cost &, builtin_tag, const value_args &,
        bool text_costed_by_byte_length);

    struct runtime_model {
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
        builtin_cost_table builtin_costs {};
    };

    struct runtime_models {
        using variants = std::array<std::optional<runtime_model>, 5>;

        variants v1 {};
        variants v2 {};
        variants v3 {};

        const runtime_model &for_script(cardano::script_type typ, builtin_semantics semantics) const;
    };

    extern const runtime_models &defaults();
}
