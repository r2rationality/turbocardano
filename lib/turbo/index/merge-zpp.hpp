#pragma once

/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/index/io.hpp>
#include <turbo/zpp-stream.hpp>

namespace turbo::index {
     template<typename T>
     size_t merge_zpp(const std::string &out_path, const std::vector<std::string> &chunks)
     {
         std::vector<zpp_stream::read_stream> inputs {};
         inputs.reserve(chunks.size());
         for (const auto &path: chunks)
             inputs.emplace_back(path);
         zpp_stream::write_stream out { out_path };
         merge_queue<T> items_to_consider {};
         for (size_t i = 0; i < inputs.size(); ++i) {
             auto &in = inputs[i];
             if (!in.eof())
                 items_to_consider.emplace(in.read<T>(), i);
         }
         size_t num_recs = 0;
         while (items_to_consider.size() > 0) {
             merge_item next = std::move(items_to_consider.top());
             items_to_consider.pop();
             out.write(next.val);
             ++num_recs;
             auto &in = inputs[next.stream_idx];
             if (!in.eof()) {
                 next.val = in.template read<T>();
                 items_to_consider.emplace(std::move(next));
             }
         }
         return num_recs;
    }
}