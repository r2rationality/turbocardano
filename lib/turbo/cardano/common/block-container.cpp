/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano.hpp>
#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/alonzo/block.hpp>
#include <turbo/cardano/babbage/block.hpp>
#include <turbo/cardano/byron/block.hpp>
#include <turbo/cardano/common/mocks.hpp>
#include <turbo/cardano/conway/block.hpp>
#include <turbo/cardano/dijkstra/block.hpp>
#include <turbo/cardano/mary/block.hpp>
#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano {
    using block_storage_type = std::variant<
        byron::boundary_block, byron::block,
        shelley::block, allegra::block, mary::block,
        alonzo::block, babbage::block,
        conway::block, dijkstra::block,
        mocks::block
    >;

    block_container::block_container(uint64_t offset, const block_info &meta, const config &cfg):
        _era { meta.era }
    {
        new (&_val) block_storage_type { std::in_place_type<mocks::block>, offset, meta, cfg };
    }

    block_container::~block_container()
    {
        reinterpret_cast<block_storage_type *>(&_val)->~block_storage_type();
    }

    const block_base &block_container::base() const
    {
        return std::visit([&](auto &blk_v) -> const block_base & {
            return blk_v;
        }, *reinterpret_cast<const block_storage_type *>(&_val));
    }
}
