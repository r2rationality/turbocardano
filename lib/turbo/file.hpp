#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/zstd.hpp>

namespace turbo::file {
    inline void read_auto(const std::string &path, uint8_vector &buffer) {
        static const std::string_view match { ".zstd" };
        if (path.ends_with(match)) {
            zstd::read(path, buffer);
        } else {
            read(path, buffer);
        }
    }

    inline uint8_vector read_auto(const std::string &path)
    {
        uint8_vector buf {};
        read_auto(path, buf);
        return buf;
    }
}