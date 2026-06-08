/* Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com) */

#include "test.hpp"
#include "serializable.hpp"

namespace {
    using namespace turbo;
    using namespace turbo::codec;

    struct point_t {
        uint32_t x = 0;
        uint32_t y = 0;

        void serialize(auto &archive)
        {
            archive.process("x", x);
            archive.process("y", y);
        }
    };

    struct plain_t {
        int val = 0;
    };

    struct scalar_wrapper_t {
        static constexpr bool single_line_serialization = true;

        uint32_t val = 0;

        void serialize(auto &archive)
        {
            archive.process(val);
        }
    };

    struct scalar_wrapper_line_t {
        scalar_wrapper_t a{};
        scalar_wrapper_t b{};

        void serialize(auto &archive)
        {
            archive.process("a", a);
            archive.process("b", b);
        }
    };

    struct line_t {
        point_t a{};
        point_t b{};

        void serialize(auto &archive)
        {
            archive.process("a", a);
            archive.process("b", b);
        }
    };

    struct point_array_t: std::array<point_t, 2> {
        static constexpr bool is_element_sequence = true;
    };

    struct points_t {
        point_array_t values{};

        void serialize(auto &archive)
        {
            archive.process("values", values);
        }
    };

    struct value_map_t {
        using key_type = uint32_t;
        using mapped_type = uint32_t;

        struct config_t {
            std::string_view key_name = "key";
            std::string_view val_name = "value";
        };

        static config_t config()
        {
            return {};
        }

        std::array<std::pair<uint32_t, uint32_t>, 2> values{};

        auto begin() const { return values.begin(); }
        auto end() const { return values.end(); }
        size_t size() const { return values.size(); }
    };

    struct mapped_t {
        value_map_t values{};

        void serialize(auto &archive)
        {
            archive.process("values", values);
        }
    };
}

suite turbo_common_serializable_suite = [] {
    "turbo::common::serializable"_test = [] {
        "concepts"_test = [] {
            expect(serializable_c<point_t>);
            expect(!serializable_c<plain_t>);
            expect(!serializable_c<uint32_t>);
            expect(single_line_serializable_c<scalar_wrapper_t>);
            expect(not_serializable_c<plain_t>);
        };
        "formatter::scalar"_test = [] {
            std::string out{};
            formatter frmtr{ std::back_inserter(out) };
            frmtr.format(uint32_t{42});
            expect_equal(std::string{"42"}, out);
        };
        "formatter::serializable"_test = [] {
            point_t p{3, 7};
            const auto out = fmt::format("{}", p);
            expect_equal(std::string{
                "x: 3\n"
                "y: 7\n"
            }, out);
        };
        "formatter::single_line_serializable"_test = [] {
            scalar_wrapper_line_t l{{ 11 }, { 22 }};
            const auto out = fmt::format("{}", l);
            expect_equal(std::string{
                "a: 11\n"
                "b: 22\n"
            }, out);
        };
        "formatter::nested"_test = [] {
            line_t l{{ 1, 2 }, { 3, 4 }};
            const auto out = fmt::format("{}", l);
            expect_equal(std::string{
                "a:\n"
                "    x: 1\n"
                "    y: 2\n"
                "b:\n"
                "    x: 3\n"
                "    y: 4\n"
            }, out);
        };
        "formatter::sequence"_test = [] {
            points_t p{};
            p.values = point_array_t{{ point_t{1, 2}, point_t{3, 4} }};
            const auto out = fmt::format("{}", p);
            expect_equal(std::string{
                "values: [\n"
                "    x: 1\n"
                "    y: 2\n"
                "    x: 3\n"
                "    y: 4\n"
                "](size: 2)"
            }, out);
        };
        "formatter::map"_test = [] {
            mapped_t m{};
            m.values.values = {{ std::pair{1U, 2U}, std::pair{3U, 4U} }};
            const auto out = fmt::format("{}", m);
            expect_equal(std::string{
                "values: {\n"
                "    1: 2\n"
                "    3: 4\n"
                "}(size: 2)"
            }, out);
        };
    };
};
