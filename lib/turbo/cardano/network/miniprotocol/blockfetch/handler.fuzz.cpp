/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

 #include <dt/chunk-registry.hpp>
 #include "handler.hpp"
 
 namespace {
     using namespace turbo;
     using namespace turbo::cardano::network::miniprotocol;
 }
 
 extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, const size_t size)
 {
     static const chunk_registry cr { install_path("data/chunk-registry"), chunk_registry::mode::store };
     try {
         blockfetch::handler h { cr };
         uint8_vector resp1 {};
         h.data(buffer { data, size }, [&](const auto bytes) { resp1 << bytes; } );
     } catch (const blockfetch::error &) {
         // ignore the library's exceptions
     }
     return 0;
 }