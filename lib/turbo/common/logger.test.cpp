/* Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include <turbo/common/test.hpp>
#include "logger.hpp"

using namespace turbo;

suite common_logger_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "turbo::common::logger"_test = [] {
        "api"_test = [] {
            // checks that the code compiles and does not fail
            logger::trace("OK - trace");
            logger::trace("OK - {}", "trace");
            logger::debug("OK - debug");
            logger::debug("OK - {}", "debug");
            logger::info("OK - info");
            logger::info("OK - {}", "info");
            logger::warn("OK - warn");
            logger::warn("OK - {}", "warn");
            logger::error("OK - error");
            logger::error("OK - {}", "error");
            expect(true);
        };
        "run_and_log_errors"_test = [] {
            const auto ex1 = logger::run_log_errors([] { return true; });
            expect(!ex1);
            const auto ex2 = logger::run_log_errors([] { throw error("Something bad!"); });
            expect(static_cast<bool>(ex2));
        };
        "run_log_errors_and_rethrow"_test = [] {
            expect(nothrow([] { logger::run_log_errors_rethrow([] { return true; }); }));
            expect(throws([] { logger::run_log_errors_rethrow([] { throw error("Something bad!"); }); }));
        };
    };
};