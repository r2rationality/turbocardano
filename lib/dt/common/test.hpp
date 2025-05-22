/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_COMMON_TEST_HPP
#define DAEDALUS_TURBO_COMMON_TEST_HPP

#include <cmath>
#include <source_location>
#define BOOST_UT_DISABLE_MODULE 1
#include <boost/ut.hpp>
#include "format.hpp"
#include "file.hpp"

namespace daedalus_turbo {
    using namespace boost::ut;

    struct test_printer: boost::ut::printer {
        template<class T>
        test_printer& operator<<(T &&t) {
            if constexpr (std::is_convertible_v<T, std::span<const uint8_t>>) {
                std::cerr << fmt::format("{}", t);
            } else {
                std::cerr << std::forward<T>(t);
            }
            return *this;
        }

        test_printer& operator<<(const std::string_view sv) {
            std::cerr << sv;
            return *this;
        }
    };

    template<typename T>
    void test_close(const T &exp, const T &act, T eps=1e-4, const reflection::source_location &loc=reflection::source_location::current())
    {
        if (exp) {
            const auto e = std::fabs(act - exp) / act;
            expect(e <= eps, loc) << fmt::format("eps {} is too big for {} and {}", e, exp, act);
        } else {
            const auto d = std::fabs(act - exp);
            expect(d <= eps, loc) << fmt::format("delta {} is too big for {} and {}", d, exp, act);
        }
    }

    template<typename X, typename Y>
    concept convertible_to_y = requires (X x, Y y)
    {
        { std::is_trivially_constructible_v<X, Y> };
    };

    template<typename T, typename Y>
    bool expect_equal(const T &exp, const Y &act, const reflection::source_location &loc=reflection::source_location::current())
    {
        const auto res = act == exp;
        expect(res, loc) << fmt::format("{} != {}", exp, act);
        return res;
    }

    template<typename T>
    bool test_same(const T &x, const T &y, const reflection::source_location &loc=reflection::source_location::current())
    {
        return expect_equal(x, y, loc);
    }

    template<typename X, convertible_to_y<X> Y>
    bool test_same(const X &x, const Y &y, const reflection::source_location &loc=reflection::source_location::current())
    {
        const auto res = x == static_cast<X>(y);
        expect(res, loc) << fmt::format("{} != {}", x, y);
        return res;
    }

    template<typename T, typename Y>
    bool test_same(const std::string &name, const T &x, const Y &y, const reflection::source_location &loc=reflection::source_location::current())
    {
        const auto res = x == static_cast<T>(y);
        expect(res, loc) << fmt::format("{}: {} != {}", name, x, y);
        return res;
    }
}

template <class... Ts>
inline auto boost::ut::cfg<boost::ut::override, Ts...> = boost::ut::runner<boost::ut::reporter<daedalus_turbo::test_printer>> {};

#endif // !DAEDALUS_TURBO_COMMON_TEST_HPP