#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano.hpp>
#include <dt/common/error.hpp>
#include "types.hpp"

namespace daedalus_turbo::cardano::network::miniprotocol::handshake
{
    struct msg_propose_versions_t {
        version_map versions {};

        static msg_propose_versions_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &enc) const;
    };

    struct msg_accept_version_t {
        uint64_t version;
        node_to_node_version_data_t config;

        static msg_accept_version_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &enc) const;
    };

    struct msg_refuse_t {
        struct version_mismatch_t {
            vector_t<uint64_t, cbor::encoder> versions {};

            static version_mismatch_t from_cbor(cbor::zero2::array_reader &);
            void to_cbor(cbor::encoder &enc) const;
        };

        struct decode_error_t {
            uint64_t version;
            std::string msg;

            static decode_error_t from_cbor(cbor::zero2::array_reader &);
            void to_cbor(cbor::encoder &enc) const;
        };

        struct refused_t {
            uint64_t version;
            std::string msg;

            static refused_t from_cbor(cbor::zero2::array_reader &);
            void to_cbor(cbor::encoder &enc) const;
        };

        using reason_type = std::variant<version_mismatch_t, decode_error_t, refused_t>;

        reason_type reason;

        static msg_refuse_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &enc) const;
    };

    struct msg_query_reply_t {
        version_map versions {};

        static msg_query_reply_t from_cbor(cbor::zero2::array_reader &);
        void to_cbor(cbor::encoder &enc) const;
    };

    using msg_base_t = std::variant<msg_propose_versions_t, msg_accept_version_t, msg_refuse_t, msg_query_reply_t>;
    struct msg_t: msg_base_t {
        using base_type = msg_base_t;
        using base_type::base_type;

        static msg_t from_cbor(cbor::zero2::value &);
        void to_cbor(cbor::encoder &) const;
    };
}
