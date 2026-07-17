/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/benchmark.hpp>
#include <turbo/plutus/conformance-data.hpp>
#include <turbo/plutus/uplc.hpp>

using namespace turbo;

suite plutus_uplc_suite = [] {
    "plutus::uplc"_test = [] {
        const auto example_dir = plutus::conformance_data_dir() / "example";
        const auto paths = file::files_with_ext_path(example_dir.string(), ".uplc");
        benchmark("uplc parse speed", [&] {
            uint64_t total_size = 0;
            for (const auto &path: paths) {
              try {
                  auto bytes = file::read(path.string());
                  total_size += bytes.size();
                  plutus::allocator alloc {};
                  plutus::uplc::script s { alloc, std::move(bytes) };
              } catch (...) {
                  const auto exp_path = (path.parent_path() / (path.stem().string() + ".uplc.expected")).string();
                  if (std::filesystem::exists(exp_path)) {
                      const std::string exp_res { file::read(exp_path).str() };
                      if (exp_res == "parse error")
                          continue;
                  }
                  throw error(fmt::format("unable to parse script: {}", path));
              }
            }
            return total_size;
        }, file::dir_size_recursive(example_dir.string(), ".uplc"));
    };
};
