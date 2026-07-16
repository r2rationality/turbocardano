/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <cstdlib>
#include <turbo/common/test.hpp>
#include <turbo/config.hpp>

using namespace turbo;

namespace {
    static void my_setenv(const char *name, const char *val)
    {
        if (name == nullptr)
            throw error("my_setenv: name cannot be null!");
#if _WIN32
        std::string putexpr { fmt::format("{}={}", name, val != nullptr ? val : "") };
        putenv(putexpr.c_str());
#else
        if (val != nullptr)
            setenv(name, val, 1);
        else
            unsetenv(name);
#endif
    }
}

suite config_suite = [] {
    "config"_test = [] {
        "path_absolute"_test = [] {
#ifdef _WIN32
            expect_equal(true, std::filesystem::path { "D:/home/dev/some-dir/logs" }.is_absolute());
            expect_equal(false, std::filesystem::path { "D:/home/dev/some-dir/logs" }.is_relative());
            expect_equal(true, std::filesystem::path { "D:home/dev/some-dir/logs" }.is_relative());
            expect_equal(true, std::filesystem::path { "/dev/some-dir/logs" }.is_relative());
            expect_equal(true, std::filesystem::path { "dev/some-dir/logs" }.is_relative());
            expect_equal(true, std::filesystem::path { "D:\\home\\dev\\some-dir\\logs" }.is_absolute());
            expect_equal(false, std::filesystem::path { "D:\\home\\dev\\some-dir\\logs" }.is_relative());
            expect_equal(true, std::filesystem::path { "D:home\\dev\\some-dir\\logs" }.is_relative());
            expect_equal(true, std::filesystem::path { "\\dev\\some-dir\\logs" }.is_relative());
            expect_equal(true, std::filesystem::path { "dev\\some-dir\\logs" }.is_relative());
#else
            expect_equal(true, std::filesystem::path { "/home/dev/some-dir/logs" }.is_absolute());
            expect_equal(false, std::filesystem::path { "/home/dev/some-dir/logs" }.is_relative());
#endif
        };
        "required"_test = [] {
            const auto &cfg = configs_dir::get();
            expect(cfg.at("turbo").at("vkey").as_string() == std::string_view { "F961D8754397FA2C39D69C97D598566A5E03C34E40FF71DB792E103380E7C105" });
            expect(cfg.at("topology").at("bootstrapPeers").at(0).at("address").as_string() == std::string_view { "backbone.cardano.iog.io" });
        };
        "non-standard-location"_test = [] {
            expect(std::getenv("DT_ETC") == nullptr);
            expect_equal(install_path("etc/mainnet"), configs_dir::default_path());
            my_setenv("DT_ETC", "./etc-missing");
            expect(std::getenv("DT_ETC") != nullptr);
            expect_equal(std::string { "./etc-missing" }, configs_dir::default_path());
            expect(throws([] { configs_dir cfg { configs_dir::default_path() }; }));
            my_setenv("DT_ETC", nullptr);
            expect(std::getenv("DT_ETC") == nullptr);
        };
        "non-standard-location override"_test = [] {
            expect(std::getenv("DT_ETC") == nullptr);
            expect_equal(install_path("etc/mainnet"), configs_dir::default_path());
            configs_dir::set_default_path("./new-path");
            expect(configs_dir::default_path() == "./new-path") << configs_dir::default_path();
            expect(throws([] { configs_dir cfg { configs_dir::default_path() }; }));
            configs_dir::set_default_path({});
            expect_equal(install_path("etc/mainnet"), configs_dir::default_path());
        };
        "mock"_test = [] {
            configs_mock::map_type cfg_data {};
            cfg_data.emplace("turbo", json::object {
                { "vkey", std::string_view { "F961D8754397FA2C39D69C97D598566A5E03C34E40FF71DB792E103380E7C105" } },
            });
            cfg_data.emplace("cardano", json::object {
                { "networkMagic", 764824073 },
            });
            configs_mock cfg { std::move(cfg_data) };
            expect(cfg.at("turbo").at("vkey").as_string() == std::string_view { "F961D8754397FA2C39D69C97D598566A5E03C34E40FF71DB792E103380E7C105" });
            expect(cfg.at("cardano").at("networkMagic").as_int64() == 764824073_ll);
        };
        "consider_bin_dir"_test = [] {
            const std::string test_file { "tmp/file.txt" };
            const auto orig_path = install_path(test_file);
            // Ignores an attempt to set install_dir to an improper location
            consider_bin_dir("/unknown/dir");
            expect_equal(orig_path, install_path(test_file));
        };
    };
};