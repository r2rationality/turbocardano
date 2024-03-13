/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/partitioned-map.hpp>
#include <dt/test.hpp>
#include <dt/validator/types.hpp>

using namespace daedalus_turbo;

suite partitioned_map_suite = [] {
    "partitioned_map"_test = [] {
        using my_pmap = partitioned_map<cardano::stake_ident, validator::reward_update_list>;
        my_pmap pm {};
        expect(pm.empty());
        my_pmap::partition_type part {};
        cardano::stake_ident stake1 { cardano::key_hash::from_hex("42FBE3C7DE5853FC74DA3C27DC583E7A660CCFF4042FBF12F223E53A") };
        cardano::stake_ident stake2 { cardano::key_hash::from_hex("00000000000000000000000000000000000000000000000000000000") };
        cardano::stake_ident stake3 { cardano::key_hash::from_hex("22222222222222222222222222222222222222222222222222222222") };
        cardano::stake_ident stake_missing { cardano::key_hash::from_hex("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") };
        auto pool1 = cardano::pool_hash::from_hex("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        auto &rl = part[stake1];
        rl.emplace(validator::reward_type::member, pool1, 12345);
        pm.partition(pm.partition_idx(stake1), std::move(part));
        expect(!pm.empty());
        expect(pm.size() == 1_ul);
        expect(pm.contains(stake1));
        expect(pm.at(stake1).size() == 1_ul);
        expect(pm[stake2].size() == 0_ul);
        expect(pm.size() == 2_ul);
        expect(pm.contains(stake2));
        expect(pm.at(stake2).size() == 0_ul);
        "iterate"_test = [&] {
            auto it = pm.begin();
            expect(it != pm.end());
            expect(it->first == stake2);
            expect(it->second.size() == 0_ul);
            ++it;
            expect(it->first == stake1);
            expect(it->second.size() == 1_ul);
            ++it;
            expect(it == pm.end());
        };
        "try_emplace"_test = [&] {
            {
                auto [it, created] = pm.try_emplace(stake3);
                expect(it->first == stake3);
                expect(it->second.size() == 0_ul);
                expect(created);
            }
            {
                auto [it, created] = pm.try_emplace(stake3);
                expect(it->first == stake3);
                expect(it->second.size() == 0_ul);
                expect(!created);
            }
            expect(pm.size() == 3_ul);
        };
        "range"_test = [&] {
            size_t cnt = 0;
            for (auto it = pm.begin(); it != pm.end(); ++it)
                cnt++;
            expect(cnt == 3_ul);
        };
        "erase"_test = [&] {
            {
                auto it = pm.erase(stake_missing);
                expect(it == pm.end());
                expect(pm.size() == 3_ul);
            }
            {
                auto it = pm.erase(stake3);
                expect(it != pm.end());
                expect(it == pm.find(stake1));
                expect(pm.size() == 2_ul);
            }
        };
        "find"_test = [&] {
            expect(pm.find(stake_missing) == pm.end());
            auto it = pm.find(stake1);
            expect(pm.find(stake1) != pm.end());
            expect(it->first == stake1);
            expect(it->second.size() == 1_ul);
        };
    };
};