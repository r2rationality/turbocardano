/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/test.hpp>
#include <dt/peer-selection.hpp>

using namespace daedalus_turbo;

suite peer_selection_suite = [] {
    using namespace std::literals::string_literals;
    "peer_selection"_test = [] {
        auto &ps = peer_selection_simple::get();
        expect(ps.next_cardano().host.starts_with("backbone")) << ps.next_cardano().host;
    };
};