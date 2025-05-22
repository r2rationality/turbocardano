/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#include <dt/file.hpp>
#include <dt/chunk-registry.hpp>
#include <dt/index/common.hpp>
#include <dt/scheduler.hpp>

namespace daedalus_turbo {
    thread_local uint8_vector chunk_registry::_read_buffer {};
    const size_t index::two_step_merge_num_files = file::max_open_files / (scheduler::default_worker_count() * 2);
}