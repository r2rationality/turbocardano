/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>
#include <turbo/common/test.hpp>
#include <turbo/zpp.hpp>

using namespace turbo;

suite zpp_bits_suite = [] {
    "zpp::bits"_test = [] {
        "init"_test = [] {
            auto [data, in, out] = ::zpp::bits::data_in_out();
            out(cardano::amount { 114232482 }, cardano::amount { 54232482 }).or_throw();
            expect(data.size() == 16_u);
            cardano::amount s1 {}, s2 {};
            in(s1, s2).or_throw();
            expect(s1 == 114232482_u);
            expect(s2 == 54232482_u);
        };
        "vector"_test = [] {
            using test_item = std::vector<cardano::amount>;
            auto [data, in, out] = ::zpp::bits::data_in_out();
            test_item items {};
            items.emplace_back(cardano::amount { 114232482 });
            items.emplace_back(cardano::amount { 54232482 });
            out(items).or_throw();
            expect(data.size() == 20_u);
            test_item out_items {};
            in(out_items).or_throw();
            expect(out_items.size() == 2_u);
            expect(out_items.at(0) == 114232482_u);
            expect(out_items.at(1) == 54232482_u);
        };
        "map"_test = [] {
            using test_item = std::map<std::string, cardano::amount>;
            auto [data, in, out] = ::zpp::bits::data_in_out();
            test_item items {};
            items.emplace("item1", cardano::amount { 114232482 });
            items.emplace("item2", cardano::amount { 54232482 });
            out(items).or_throw();
            expect(data.size() == 38_u);
            test_item out_items {};
            in(out_items).or_throw();
            expect(out_items.size() == 2_u);
            expect(out_items.at("item1") == 114232482_u);
            expect(out_items.at("item2") == 54232482_u);
        };
        "map of sets"_test = [] {
            using test_item = std::map<std::string, std::set<uint64_t>>;
            auto [data, in, out] = ::zpp::bits::data_in_out();
            test_item items {};
            {
                auto &set = items["item1"];
                set.emplace(1234);
                set.emplace(12);
            }
            {
                auto &set = items["item2"];
                set.emplace(0);
            }
            out(items).or_throw();
            expect(data.size() == 54_u);
            test_item out_items {};
            in(out_items).or_throw();
            expect(out_items.size() == 2_u);
            expect(out_items.at("item1").size() == 2_u);
            expect(out_items.at("item1").contains(12));
            expect(out_items.at("item1").contains(1234));
            expect(out_items.at("item2").size() == 1_u);
            expect(out_items.at("item2").contains(0));
        };
    };
};
