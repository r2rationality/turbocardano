#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/chunk-registry.hpp>

namespace turbo::txwit {
    enum class witness_type { all, vkey, script, none };
    using error_handler_func = std::function<void(const std::string &)>;

    extern witness_type witness_type_from_str(std::string_view);

    struct processor {
        processor(const chunk_registry &, cardano::ledger::state &,
            const error_handler_func &on_error=[](const std::string &what) { throw error(what); });
        ~processor();
        uint64_t end_offset() const;
        void reset();
        void cache(const cardano::block_hash &, buffer);
        void apply(storage::chunk_cptr_list, const cardano::optional_point &from={},
            const cardano::optional_point &to={}, witness_type type=witness_type::all);
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };

    extern cardano::optional_point validate(const chunk_registry &cr, const cardano::optional_point &from={},
          const cardano::optional_point &to={}, witness_type type=witness_type::all,
          const error_handler_func &error_handler=[](const std::string &what) {
              throw error(fmt::format("txwit: error: {}", what));
          });
}
