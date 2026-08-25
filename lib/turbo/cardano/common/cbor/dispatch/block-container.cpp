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
#include <turbo/cardano/mary/block.hpp>
#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano {
    using cbor_block_storage_type = std::variant<
        byron::boundary_block, byron::block,
        shelley::block, allegra::block, mary::block,
        alonzo::block, babbage::block,
        conway::block,
        mocks::block
    >;

    block_container::block_container(const uint64_t offset, cbor::zero2::value &v, const config &cfg):
        block_container { offset, v.array(), v, cfg }
    {
    }

    block_container::block_container(const uint64_t offset, cbor::zero2::array_reader &it,
            cbor::zero2::value &block_tuple, const config &cfg):
        _era { numeric_cast<uint8_t>(it.read().uint()) }
    {
        auto &block = it.read();
        // Some nested values keep a reference to their parent block, so the variant must
        // be constructed at its final address rather than returned in temporary byte storage.
        _make(_val, _era, offset, block_tuple, block, cfg);
        _raw.emplace(block_tuple.data_raw());
    }

    void block_container::_make(storage_type &storage, const uint8_t era, const uint64_t offset,
        cbor::zero2::value &block_tuple, cbor::zero2::value &block, const config &cfg)
    {
        static_assert(sizeof(cbor_block_storage_type) <= sizeof(block_container::storage_type));
        const auto header_offset = numeric_cast<uint64_t>(block.data_begin() - block_tuple.data_begin());
        switch (era) {
            case 0: new (&storage) cbor_block_storage_type { std::in_place_type<byron::boundary_block>, era, offset, header_offset, block, cfg }; break;
            case 1: new (&storage) cbor_block_storage_type { std::in_place_type<byron::block>, era, offset, header_offset, block, cfg }; break;
            case 2: new (&storage) cbor_block_storage_type { std::in_place_type<shelley::block>, era, offset, header_offset, block, cfg }; break;
            case 3: new (&storage) cbor_block_storage_type { std::in_place_type<allegra::block>, era, offset, header_offset, block, cfg }; break;
            case 4: new (&storage) cbor_block_storage_type { std::in_place_type<mary::block>, era, offset, header_offset, block, cfg }; break;
            case 5: new (&storage) cbor_block_storage_type { std::in_place_type<alonzo::block>, era, offset, header_offset, block, cfg }; break;
            case 6: new (&storage) cbor_block_storage_type { std::in_place_type<babbage::block>, era, offset, header_offset, block, cfg }; break;
            case 7: new (&storage) cbor_block_storage_type { std::in_place_type<conway::block>, era, offset, header_offset, block, cfg }; break;
            default: throw error(fmt::format("unsupported era {}!", era));
        }
    }
}
