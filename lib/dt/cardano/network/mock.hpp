#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include "common.hpp"
#include "types.hpp"
#include "miniprotocol/handshake/types.hpp"

namespace daedalus_turbo::cardano::network {
    template<typename MSG>
    struct mock_response_processor_t {
        using msg_list_t = std::vector<MSG>;
        using decoder_t = std::function<MSG(buffer)>;

        mock_response_processor_t() =delete;
        mock_response_processor_t(const mock_response_processor_t &) =delete;

        explicit mock_response_processor_t(const decoder_t &decoder):
            _decoder { decoder }
        {
        }

        void operator()(data_generator_t &&gen)
        {
            while (gen.resume()) {
                _msgs.emplace_back(_decoder(gen.take()));
            }
        }

        size_t size() const
        {
            return _msgs.size();
        }

        const MSG &at(const size_t idx) const
        {
            return _msgs.at(idx);
        }

        const msg_list_t &messages() const noexcept
        {
            return _msgs;
        }
    private:
        decoder_t _decoder;
        msg_list_t _msgs {};
    };
}
