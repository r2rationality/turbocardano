#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/common/common.hpp>

namespace daedalus_turbo::cardano {
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
        byte_array<736> _impl_storage;
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

        static parsed_header from_cbor(cbor::zero2::value &v, const config &cfg=config::get())
        {
            auto &it = v.array();
            auto typ = it.read().uint();
            cbor::encoder block_tuple {};
            if (typ == 0) {
                auto &it2 = it.read().array();
                auto hdr_era = numeric_cast<uint8_t>(it2.read().array().read().uint());
                block_tuple.array(2).uint(hdr_era).array(1);
                return { hdr_era, it2.read().tag().read().bytes(), cfg };
            }
            return { numeric_cast<uint8_t>(typ + 1), it.read().tag().read().bytes(), cfg };
        }

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
    };
    static_assert(std::is_move_constructible_v<parsed_header>);
}
