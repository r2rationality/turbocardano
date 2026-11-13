#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <cstring>
#include <limits>
#include <optional>
#include <boost/beast/core/flat_buffer.hpp>
#include <turbo/cbor/zero2.hpp>

namespace turbo::cardano::network {
    struct message_buffer_t {
        static constexpr size_t max_segment_size = 65535;

        explicit message_buffer_t(const size_t limit=max_segment_size):
            _limit { limit },
            _bytes { std::min(limit, std::numeric_limits<size_t>::max() - max_segment_size) + max_segment_size }
        {
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return _begin == _bytes.size();
        }

        write_buffer prepare(const size_t size)
        {
            if (size > max_segment_size || _bytes.size() - _begin > _limit) [[unlikely]]
                throw error{"CBOR ingress buffer limit exceeded"};
            _bytes.consume(_begin);
            _begin = 0;
            const auto bytes = _bytes.prepare(size);
            return { static_cast<uint8_t *>(bytes.data()), bytes.size() };
        }

        void commit(const size_t size) noexcept
        {
            _bytes.commit(size);
        }

        void append(const buffer bytes)
        {
            const auto dest = prepare(bytes.size());
            if (!bytes.empty())
                std::memcpy(dest.data(), bytes.data(), bytes.size());
            commit(bytes.size());
        }

        // Returned views survive next(); prepare(), commit(), and append() invalidate them.
        std::optional<buffer> next(const size_t limit=std::numeric_limits<size_t>::max())
        {
            if (empty())
                return {};
            const auto data = _bytes.cdata();
            const auto bytes = buffer { static_cast<const uint8_t *>(data.data()), data.size() }.subbuf(_begin);
            const auto max_size = std::min(limit, _limit);
            buffer message;
            try {
                auto parsed = cbor::zero2::parse(bytes.subbuf(0, std::min(bytes.size(), max_size)));
                message = parsed.get().data_raw();
            } catch (const cbor::zero2::incomplete_error &) {
                if (bytes.size() >= max_size) [[unlikely]]
                    throw error{"CBOR message size limit exceeded: {} bytes", max_size};
                return {};
            }
            _begin += message.size();
            return message;
        }

        static void check_handshake(const buffer bytes)
        {
            if (!bytes.empty() && bytes.size() <= max_segment_size) {
                try {
                    auto parsed = cbor::zero2::parse(bytes);
                    if (parsed.get().data_raw().size() == bytes.size())
                        return;
                } catch (const cbor::zero2::incomplete_error &) {
                }
            }
            throw error{"a handshake segment must contain exactly one complete CBOR message"};
        }
    private:
        size_t _limit;
        boost::beast::flat_buffer _bytes;
        size_t _begin = 0;
    };
}
