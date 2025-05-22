#pragma once
#ifndef DAEDALUS_TURBO_ASIO_HPP
#define DAEDALUS_TURBO_ASIO_HPP
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <functional>
#include <memory>
#include <string>

namespace boost::asio {
    struct io_context;
}

namespace daedalus_turbo::asio {
    struct speed_mbps {
        double current = 0.0;
        double max = 0.0;
    };

    struct worker;
    using worker_ptr = std::shared_ptr<worker>;
    struct worker {
        using action_type = std::function<void()>;

        static const worker_ptr &get();
        virtual ~worker() =default;
        virtual void add_before_action(const std::string &name, const action_type &act) =0;
        virtual void del_before_action(const std::string &name) =0;
        virtual void add_after_action(const std::string &name, const action_type &act) =0;
        virtual void del_after_action(const std::string &name) =0;
        virtual boost::asio::io_context &io_context() =0;
        virtual void internet_speed_report(double) =0;
        virtual speed_mbps internet_speed() const =0;
    };

    struct worker_thread: worker {
        explicit worker_thread();
        ~worker_thread() override;
        void add_before_action(const std::string &name, const action_type &act) override;
        void del_before_action(const std::string &name) override;
        void add_after_action(const std::string &name, const action_type &act) override;
        void del_after_action(const std::string &name) override;
        boost::asio::io_context &io_context() override;
        void internet_speed_report(double) override;
        speed_mbps internet_speed() const override;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };

    struct worker_manual: worker {
        explicit worker_manual();
        ~worker_manual() override;
        void add_before_action(const std::string &name, const action_type &act) override;
        void del_before_action(const std::string &name) override;
        void add_after_action(const std::string &name, const action_type &act) override;
        void del_after_action(const std::string &name) override;
        boost::asio::io_context &io_context() override;
        void internet_speed_report(double) override;
        speed_mbps internet_speed() const override;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}

#endif // !DAEDALUS_TURBO_ASIO_HPP
