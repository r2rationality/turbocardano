/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cbor/zero2.hpp>

namespace turbo::cbor::zero2 {
    template std::ostreambuf_iterator<char> format_to(std::ostreambuf_iterator<char> out_it, value &v, const size_t depth, const size_t max_seq_to_expand);
    template std::back_insert_iterator<std::string> format_to(std::back_insert_iterator<std::string> out_it, value &v, const size_t depth, const size_t max_seq_to_expand);
}
