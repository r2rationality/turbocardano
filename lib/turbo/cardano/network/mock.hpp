#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "common.hpp"
#include "types.hpp"
#include "miniprotocol/handshake/types.hpp"

namespace turbo::cardano::network {
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
                _msgs.emplace_back(_decoder(gen.result()));
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
