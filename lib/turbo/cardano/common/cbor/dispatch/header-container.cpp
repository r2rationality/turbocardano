/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano.hpp>
#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/alonzo/block.hpp>
#include <turbo/cardano/babbage/block.hpp>
#include <turbo/cardano/byron/block.hpp>
#include <turbo/cardano/conway/block.hpp>
#include <turbo/cardano/dijkstra/block.hpp>
#include <turbo/cardano/mary/block.hpp>
#include <turbo/cardano/shelley/block.hpp>

namespace turbo::cardano {
    parsed_header parsed_header::from_cbor(cbor::zero2::value &v, const config &cfg)
    {
        auto &it = v.array();
        const auto type = it.read().uint();
        if (type == 0) {
            auto &byron = it.read().array();
            const auto era = numeric_cast<uint8_t>(byron.read().array().read().uint());
            return { era, byron.read().tag().read().bytes(), cfg };
        }
        return { numeric_cast<uint8_t>(type + 1), it.read().tag().read().bytes(), cfg };
    }

    struct header_container::impl {
        using value_type = std::variant<byron::boundary_block_header, byron::block_header, shelley::block_header,
            allegra::block_header, mary::block_header, alonzo::block_header, babbage::block_header,
            conway::block_header, dijkstra::block_header>;

        static impl from_cbor(cbor::zero2::value &v, const config &cfg=cardano::config::get())
        {
            auto &it = v.array();
            const auto era = numeric_cast<uint8_t>(it.read().uint());
            return { era, it.read().at(0), cfg };
        }

        impl(const uint8_t era, cbor::zero2::value &hdr, const config &cfg=cardano::config::get()):
            _val { _make(era, hdr, cfg) }
        {
        }

        const block_header_base &base() const
        {
            return std::visit([](const auto &v) -> const block_header_base & { return v; }, _val);
        }
    private:
        const value_type _val;

        static value_type _make(const uint8_t era, cbor::zero2::value &hdr, const config &cfg)
        {
            switch (era) {
                case 0: return value_type { byron::boundary_block_header { era, hdr, cfg } };
                case 1: return byron::block_header { era, hdr, cfg };
                case 2: return shelley::block_header { era, hdr, cfg };
                case 3: return allegra::block_header { era, hdr, cfg };
                case 4: return mary::block_header { era, hdr, cfg };
                case 5: return alonzo::block_header { era, hdr, cfg };
                case 6: return babbage::block_header { era, hdr, cfg };
                case 7: return conway::block_header { era, hdr, cfg };
                case 8: return dijkstra::block_header { era, hdr, cfg };
                [[unlikely]] default: throw error(fmt::format("unsupported era {}!", era));
            }
        }
    };

    header_container::header_container(cbor::zero2::value &v, const config &cfg)
    {
        static_assert(sizeof(_impl_storage) >= sizeof(impl));
        static_assert(alignof(impl) <= alignof(std::max_align_t));
        new (reinterpret_cast<impl *>(&_impl_storage)) impl { impl::from_cbor(v, cfg) };
    }

    header_container::header_container(const uint8_t era, cbor::zero2::value &hdr, const config &cfg)
    {
        static_assert(sizeof(_impl_storage) >= sizeof(impl));
        static_assert(alignof(impl) <= alignof(std::max_align_t));
        new (reinterpret_cast<impl *>(&_impl_storage)) impl { era, hdr, cfg };
    }

    header_container::header_container(header_container &&o)
    {
        new (reinterpret_cast<impl *>(&_impl_storage)) impl { std::move(*reinterpret_cast<impl *>(&o._impl_storage)) };
    }

    header_container::~header_container()
    {
        reinterpret_cast<impl *>(&_impl_storage)->~impl();
    }

    const block_header_base &header_container::operator*() const
    {
        return reinterpret_cast<const impl *>(&_impl_storage)->base();
    }

    const block_header_base *header_container::operator->() const
    {
        return &reinterpret_cast<const impl *>(&_impl_storage)->base();
    }
}
