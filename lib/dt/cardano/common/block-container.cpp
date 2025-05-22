/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano.hpp>
#include <dt/cardano/common/mocks.hpp>

namespace daedalus_turbo::cardano {
    using block_storage_type = std::variant<
        byron::boundary_block, byron::block,
        shelley::block, mary::block,
        alonzo::block, babbage::block,
        conway::block,
        mocks::block
    >;

    block_container::storage_type block_container::_make(const uint8_t era, const uint64_t offset, cbor::zero2::value &block_tuple, cbor::zero2::value &block, const config &cfg)
    {
        static_assert(sizeof(block_storage_type) <= sizeof(block_container::storage_type));
        storage_type val;
        switch (era) {
            case 0: new (&val) block_storage_type { std::in_place_type<byron::boundary_block>, era, offset, numeric_cast<uint64_t>(block.data_begin() - block_tuple.data_begin()), block, cfg }; break;
            case 1: new (&val) block_storage_type { std::in_place_type<byron::block>, era, offset, numeric_cast<uint64_t>(block.data_begin() - block_tuple.data_begin()), block, cfg }; break;
            case 2: new (&val) block_storage_type { std::in_place_type<shelley::block>, era, offset, numeric_cast<uint64_t>(block.data_begin() - block_tuple.data_begin()), block, cfg }; break;
            case 3: // same as era=4!
            case 4: new (&val) block_storage_type { std::in_place_type<mary::block>, era, offset, numeric_cast<uint64_t>(block.data_begin() - block_tuple.data_begin()), block, cfg }; break;
            case 5: new (&val) block_storage_type { std::in_place_type<alonzo::block>, era, offset, numeric_cast<uint64_t>(block.data_begin() - block_tuple.data_begin()), block, cfg }; break;
            case 6: new (&val) block_storage_type { std::in_place_type<babbage::block>, era, offset, numeric_cast<uint64_t>(block.data_begin() - block_tuple.data_begin()), block, cfg }; break;
            case 7: new (&val) block_storage_type { std::in_place_type<conway::block>, era, offset, numeric_cast<uint64_t>(block.data_begin() - block_tuple.data_begin()), block, cfg }; break;
            default: throw cardano_error(fmt::format("unsupported era {}!", era));
        }
        return val;
    }

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
