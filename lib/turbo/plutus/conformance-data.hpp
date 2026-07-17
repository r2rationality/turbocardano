#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <filesystem>
#include <stdexcept>
#include <turbo/config.hpp>

namespace turbo::plutus {
    inline const std::filesystem::path &conformance_data_dir()
    {
        static const auto dir = [] {
            const std::filesystem::path upstream { install_path(
                "3rdparty/plutus/plutus-conformance/test-cases/uplc/evaluation") };
            if (std::filesystem::is_directory(upstream))
                return upstream;
            throw std::runtime_error(
                "Plutus conformance data is unavailable: initialize the 3rdparty/plutus submodule");
        }();
        return dir;
    }
}
