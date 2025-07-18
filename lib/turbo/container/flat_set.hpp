#pragma once
/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <boost/container/flat_set.hpp>
#include <turbo/common/format.hpp>

namespace turbo {
    template<typename T>
    using flat_set = boost::container::flat_set<T>;
}

namespace fmt {
    template<typename T>
    struct formatter<boost::container::flat_set<T>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "[");
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string sep { std::next(it) == v.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}{}", *it, sep);
            }
            return fmt::format_to(out_it, "]");
        }
    };
}
