/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/byron/block.hpp>
#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/common/common.hpp>
#include <turbo/cardano/common/mocks.hpp>
#include <turbo/cardano/conway/block.hpp>
#include <turbo/cardano/dijkstra/block.hpp>

namespace turbo::cardano {
    struct tx_container::impl {
        impl(const block_info &meta, const uint64_t tx_abs_off, cbor::zero2::value &v, size_t idx, const cardano::config &cfg):
            _blk { meta.offset, meta, cfg },
            _val { _make(_blk, tx_abs_off, v, idx) }
        {
        }

        impl(const block_info &meta, const uint64_t tx_abs_off, cbor::zero2::value &tx, cbor::zero2::value &wit, const size_t idx, const cardano::config &cfg):
            impl { meta, tx_abs_off, tx, idx, cfg }
        {
            if (_blk.era() != 8) {
                std::visit([&](auto &tx_v) {
                    tx_v.parse_witnesses(wit);
                }, _val);
            }
        }

        const tx_base &base() const
        {
            return std::visit([&](auto &tx_v) -> const tx_base & {
                return tx_v;
            }, _val);
        }
    private:
        using value_type = std::variant<byron::tx, shelley::tx, allegra::tx, mary::tx, alonzo::tx,
            babbage::tx, conway::tx, dijkstra::tx>;

        mocks::block _blk;
        value_type _val;

        static value_type _make(const block_base &blk, const uint64_t tx_abs_off, cbor::zero2::value &tx, const size_t idx)
        {
            const auto blk_off = tx_abs_off - blk.offset() - blk.header_offset();
            switch (blk.era()) {
                case 1: return byron::tx { blk, blk_off, tx, idx };
                case 2: return shelley::tx { blk, blk_off, tx, idx };
                case 3: return allegra::tx { blk, blk_off, tx, idx };
                case 4: return mary::tx { blk, blk_off, tx, idx };
                case 5: return alonzo::tx { blk, blk_off, tx, idx };
                case 6: return babbage::tx { blk, blk_off, tx, idx };
                case 7: return conway::tx { blk, blk_off, tx, idx };
                case 8: return dijkstra::tx { blk, blk_off, tx, idx, false, true };
                [[unlikely]] default: throw error(fmt::format("unsupported era {}!", blk.era()));
            }
        }
    };

    tx_container::tx_container(const block_info &meta, const uint64_t tx_abs_off, cbor::zero2::value &tx, const size_t idx, const config &cfg)
    {
        static_assert(sizeof(impl_storage) >= sizeof(impl));
        static_assert(alignof(impl) <= alignof(std::max_align_t));
        new (reinterpret_cast<impl*>(_impl.data())) impl { meta, tx_abs_off, tx, idx, cfg };
    }

    tx_container::tx_container(const block_info &meta, const uint64_t tx_abs_off, cbor::zero2::value &tx, cbor::zero2::value &wits, const size_t idx, const config &cfg)
    {
        static_assert(sizeof(impl_storage) >= sizeof(impl));
        static_assert(alignof(impl) <= alignof(std::max_align_t));
        new (reinterpret_cast<impl*>(_impl.data())) impl { meta, tx_abs_off, tx, wits, idx, cfg };
    }

    tx_container::~tx_container()
    {
        reinterpret_cast<impl*>(_impl.data())->~impl();
    }

    const tx_base &tx_container::operator*() const
    {
        return reinterpret_cast<const impl*>(_impl.data())->base();
    }

    const tx_base *tx_container::operator->() const
    {
        return &reinterpret_cast<const impl*>(_impl.data())->base();
    }
}
