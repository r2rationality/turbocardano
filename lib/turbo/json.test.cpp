/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/common/test.hpp>
#include <turbo/json.hpp>

using namespace turbo;

suite json_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "json"_test = [] {
        "load genesis"_test = [] {
            auto genesis = json::load("./etc/mainnet/shelley-genesis.json");
            expect(genesis.at("genDelegs").as_object().size() == 7_u);
        };
        "save_pretty object"_test = [] {
            file::tmp t { "json-save-pretty-object-test.json" };
            auto j = json::object {
                { "name", "abc" },
                { "version", 123 }
            };
            json::save_pretty(t.path(), j);
            auto buf = file::read(t.path());
            expect(buf == std::string_view { "{\n  \"name\": \"abc\",\n  \"version\": 123\n}" }) << std::string_view { reinterpret_cast<char *>(buf.data()), buf.size() };
        };
        "save_pretty array"_test = [] {
            file::tmp t { "json-save-pretty-array-test.json" };
            auto j = json::array {
                "name",
                123
            };
            json::save_pretty(t.path(), j);
            auto act = file::read(t.path());
            std::string_view exp { "[\n  \"name\",\n  123\n]" };
            expect(act.size() == exp.size()) << act.size() << exp.size();
            expect(act == exp) << static_cast<buffer>(act);
        };
        "save_pretty_signed"_test = [] {
            file::tmp t { "json-save-pretty-signed-test.json" };
            crypto::ed25519::skey sk {};
            crypto::ed25519::vkey vk {};
            crypto::ed25519::create(sk, vk);
            json::object j_val {
                { "key1", "val2" },
                { "key2", 22 }
            };
            json::save_pretty_signed(t.path(), j_val, sk);
            auto j_dec = json::load(t.path()).as_object();
            expect(j_dec.contains("signature"));
            expect(nothrow([&] { json::parse_signed(file::read(t.path()), vk); }));
            auto j_parsed = json::parse_signed(file::read(t.path()), vk);
            expect(j_parsed == j_val) << json::serialize(j_parsed);
        };
        "array copy constructor"_test = [] {
            auto meta = json::load("./etc/mainnet/turbo.json").as_object();
            expect(meta.contains("hosts"));
            const auto &exp_arr = meta.at("hosts").as_array();
            expect(exp_arr.size() >= 2);
            json::array copy_arr(meta.at("hosts").as_array());
            expect(copy_arr.size() == exp_arr.size());
        };
    };  
};