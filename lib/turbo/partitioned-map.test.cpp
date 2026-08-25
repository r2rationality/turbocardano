/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/types.hpp>
#include <turbo/common/test.hpp>
#include <turbo/partitioned-map.hpp>
#include <boost/container/flat_map.hpp>

using namespace turbo;
using namespace cardano::ledger;

suite partitioned_map_suite = [] {
    "partitioned_map"_test = [] {
        using my_pmap = partitioned_map<cardano::stake_ident, reward_update_list>;
        using std_partition = std::map<cardano::stake_ident, reward_update_list>;
        using std_pmap = partitioned_map<cardano::stake_ident, reward_update_list, std_partition>;
        using flat_partition = boost::container::flat_map<cardano::stake_ident, reward_update_list>;
        using flat_pmap = partitioned_map<cardano::stake_ident, reward_update_list, flat_partition>;
        static_assert(std::forward_iterator<my_pmap::iterator>);
        static_assert(std::forward_iterator<my_pmap::const_iterator>);
        static_assert(std::ranges::forward_range<my_pmap>);
        static_assert(std::ranges::forward_range<const my_pmap>);
        my_pmap pm {};
        expect(pm.empty());
        my_pmap::partition_type part {};
        cardano::stake_ident stake1 { cardano::key_hash::from_hex("42FBE3C7DE5853FC74DA3C27DC583E7A660CCFF4042FBF12F223E53A"), false };
        cardano::stake_ident stake2 { cardano::key_hash::from_hex("00000000000000000000000000000000000000000000000000000000"), false };
        cardano::stake_ident stake3 { cardano::key_hash::from_hex("22222222222222222222222222222222222222222222222222222222"), false };
        cardano::stake_ident stake_missing { cardano::key_hash::from_hex("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), false };
        auto pool1 = cardano::pool_hash::from_hex("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        auto &rl = part[stake1];
        rl.emplace(reward_type::member, pool1, 12345);
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
                expect(pm.erase(stake_missing) == 0);
                expect(pm.size() == 3_ul);
            }
            {
                auto it = pm.find(stake3);
                it = pm.erase(it);
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
        "clear and reuse"_test = [&] {
            pm.clear();
            expect(pm.empty());
            auto [it, created] = pm.try_emplace(stake1);
            expect(created);
            expect(it == pm.find(stake1));
        };
        "partition container policy"_test = [&] {
            std::map<cardano::stake_ident, reward_update_list> source {};
            source.try_emplace(stake1);
            std_pmap std_pm { source };
            auto std_it = std_pm.find(stake1);
            expect(std_it != std_pm.end());
            expect(std_it == std_pm.find(stake1));
            std_pm.clear();
            expect(std_pm.empty());

            flat_pmap flat_pm { source };
            auto flat_it = flat_pm.find(stake1);
            expect(flat_it != flat_pm.end());
            expect(flat_it == flat_pm.find(stake1));
            flat_pm.clear();
            expect(flat_pm.empty());
        };
    };
};
