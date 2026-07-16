#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <boost/container/flat_map.hpp>
#include <turbo/common/format.hpp>

namespace turbo {
    template<typename K, typename V>
    using flat_map = boost::container::flat_map<K, V>;
}

namespace fmt {
    template<typename K, typename V>
    struct formatter<boost::container::flat_map<K, V>>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            auto out_it = fmt::format_to(ctx.out(), "{{");
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string sep { std::next(it) == v.end() ? "" : ", " };
                out_it = fmt::format_to(out_it, "{}={}{}", it->first, it->second, sep);
            }
            return fmt::format_to(out_it, "}}");
        }
    };
}
