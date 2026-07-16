/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include "peer-selection.hpp"

namespace {
    using namespace turbo;
    using namespace turbo::cardano::network;
}

suite peer_selection_suite = [] {
    using namespace std::literals::string_literals;
    "peer_selection"_test = [] {
        auto &ps = peer_selection_simple::get();
        expect(ps.next_cardano().host.starts_with("backbone")) << ps.next_cardano().host;
    };
};