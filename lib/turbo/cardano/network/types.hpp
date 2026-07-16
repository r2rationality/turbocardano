#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/network/common.hpp>
#include <turbo/common/coro.hpp>

namespace turbo::cardano::network {
    using data_generator_t = coro::generator_t<uint8_vector>;

    struct base_observer_t {
        virtual ~base_observer_t() =default;
        virtual void failed(std::string_view) =0;
        virtual void stopped() =0;
    };

    struct op_observer_t: base_observer_t {
        virtual void done() =0;
        // redefine the methods to make clang 17 on Mac happy
        void failed(std::string_view) override =0;
        void stopped() override =0;
    };
    using op_observer_ptr = std::shared_ptr<op_observer_t>;

    struct noop_observer_t: op_observer_t {
        void done() override
        {}

        void failed(std::string_view) override
        {}

        void stopped() override
        {}
    };

    struct connection;

    using async_write_func = std::function<void(buffer)>;

    struct data_observer: base_observer_t {
        virtual void data(buffer, const async_write_func &) =0;
    };
    using data_observer_ptr = std::shared_ptr<data_observer>;
    using data_observer_factory = std::function<data_observer_ptr()>;

    struct connection {
        virtual ~connection() =default;
        virtual size_t available_ingress() const =0;
        virtual void async_read(write_buffer, op_observer_ptr) =0;
        virtual void async_write(buffer, op_observer_ptr) =0;
    };
    using connection_ptr = std::unique_ptr<connection>;

    struct op_result_ok_t {};
    struct op_result_stopped_t {};
    struct op_result_failed_t { std::string reason; };

    using op_result_t = std::variant<op_result_ok_t, op_result_stopped_t, op_result_failed_t>;
}
