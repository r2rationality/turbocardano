#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <cstddef>
#include <turbo/cardano/common/common.hpp>

namespace turbo::cardano {
    struct header_container {
        // prohibit copying and moving
        // since the nested value refers to the parent by a const reference
        header_container() =delete;
        header_container(const header_container &) =delete;
        header_container(header_container &&);
        header_container(cbor::zero2::value &v, const config &cfg=cardano::config::get());
        header_container(uint8_t era, cbor::zero2::value &v, const config &cfg=cardano::config::get());
        ~header_container();
        const block_header_base &operator*() const;
        const block_header_base *operator->() const;
    private:
        struct impl;
        alignas(std::max_align_t) byte_array<1152> _impl_storage;
    };

    struct parsed_block {
        std::shared_ptr<uint8_vector> data;
        block_container blk;

        parsed_block(const std::shared_ptr<uint8_vector> &bytes, cbor::zero2::value &v, const config &cfg=config::get()):
            data { bytes },
            blk { 0, v, cfg }
        {
        }

        parsed_block(const buffer bytes, const config &cfg=config::get()):
            data { std::make_shared<uint8_vector>(bytes) },
            blk { 0, cbor::zero2::parse(*data).get(), cfg }
        {   
        }
    };

    struct parsed_header {
        uint8_vector data;
        header_container hdr;

        static parsed_header from_cbor(cbor::zero2::value &, const config &cfg=config::get());

        parsed_header(const buffer bytes, const config &cfg=config::get()):
            data { bytes },
            hdr { cbor::zero2::parse(data).get(), cfg }
        {
        }

        parsed_header(const uint8_t era, const buffer header_bytes, const cardano::config &cfg=cardano::config::get()):
            data { _make_header_data(era, header_bytes) },
            hdr { cbor::zero2::parse(data).get(), cfg }
        {
        }

        parsed_header(parsed_header &&o) noexcept:
            data { std::move(o.data) },
            hdr { std::move(o.hdr) }
        {
        }

        parsed_header(const parsed_header &o) noexcept:
            parsed_header { o.data, o.hdr->config() }
        {
        }

        const block_header_base *operator->() const
        {
            return hdr.operator->();
        }

        void to_cbor(cbor::encoder &enc) const;
    private:
#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ == 13 || __GNUC__ == 15)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif
        static uint8_vector _make_header_data(const uint8_t era, const buffer header_bytes)
        {
            cbor::encoder enc {};
            enc.cbor().reserve(3 + header_bytes.size());
            enc.array(2);
            enc.uint(era);
            enc.array(1);
            enc << header_bytes;
            return std::move(enc.cbor());
        }
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 13
#   pragma GCC diagnostic pop
#endif
    };
    static_assert(std::is_move_constructible_v<parsed_header>);
}
