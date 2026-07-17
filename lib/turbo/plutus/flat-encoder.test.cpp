/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cbor/zero2.hpp>
#include <turbo/common/test.hpp>
#include <turbo/file.hpp>
#include <turbo/plutus/conformance-data.hpp>
#include <turbo/plutus/flat-encoder.hpp>
#include <turbo/plutus/uplc.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::plutus;
    using namespace turbo::plutus::flat;
    using allocator = plutus::allocator;

    std::string stringify_diff(const buffer b1, const buffer b2)
    {
        std::string res {};
        auto out_it = std::back_inserter(res);
        if (b1.size() != b2.size())
            out_it = fmt::format_to(out_it, "sizes mismatch: {} vs {}\n", b1.size(), b2.size());
        const auto min_sz = std::min(b1.size(), b2.size());
        for (size_t i = 0; i < min_sz; ++i) {
            if (b1[i] != b2[i]) {
                out_it = fmt::format_to(out_it, "diff at byte {}: {:02x} != {:02x}\n", i, b1[i], b2[i]);
            }
        }
        return res;
    }

    void test_flat(const std::string &path)
    {
        auto cbor = file::read(path);
        if (path.ends_with(".hex")) {
            cbor = uint8_vector::from_hex(cbor.str());
        }
        const uint8_vector wo_cbor { cbor::zero2::parse(cbor).get().bytes() };
        allocator alloc {};
        const script s { alloc, cbor };
        const auto new_cbor = encode(s.version(), s.program());
        if (!expect_equal(wo_cbor, new_cbor, path)) {
            logger::warn("{}", stringify_diff(wo_cbor, new_cbor));
        }
    }

    void test_uplc_code(const std::string &path, const buffer code)
    {
        try {
            allocator alloc {};
            const uplc::script s_uplc { alloc, code };
            const auto cbor = encode_cbor(s_uplc.version(), s_uplc.program());
            const script s_flat { alloc, cbor };
            expect_equal(s_uplc.version(), s_flat.version(), path);
            expect_equal(s_uplc.program(), s_flat.program(), path);
        } catch (const std::exception &ex) {
            if (!std::string { ex.what() } .starts_with("bls12"))
                expect(false) << path << "exception:" << ex.what();
        }
    }

    void test_uplc(const std::string &path)
    {
        test_uplc_code(path, file::read(path));
    }

    void test_uplc_hex(const std::string &code, const std::string &exp_hex)
    {
        allocator alloc {};
        const uplc::script s_uplc { alloc, buffer { code } };
        const auto cbor = encode_cbor(s_uplc.version(), s_uplc.program());
        expect_equal(uint8_vector::from_hex(exp_hex), cbor, code);
    }
}

suite plutus_flat_encoder_suite = [] {
    "plutus::flat::encoder"_test = [] {
        "constr"_test = [] {
            test_uplc_hex("(program 1.1.0 [ (builtin serialiseData) (con data (Constr 1 [I 0, I 1])) ])", "500101003766980106d87a9f0001ff0001");
            test_uplc_hex("(program 1.1.0 (constr 1))", "450101008011");
            test_uplc_hex("(program 1.1.0 (constr 1 (con integer 0) (con integer 1)))", "4a010100801a4001480081");
        };
        "terms"_test = [] {
            for (const auto &path: file::files_with_ext_path(conformance_data_dir().string(), ".uplc")) {
                const auto exp_path = (path.parent_path() / path.stem()).string() + ".uplc.expected";
                if (file::read(exp_path).str() != "parse error")
                    test_uplc(path.string());
            }
        };
        "conformance"_test = [] {
            for (const auto &path: file::files_with_ext("./data/plutus/term", ".hex")) {
                test_flat(path);
            }
        };
        "scripts"_test = [] {
            for (const auto &path: file::files_with_ext("./data/plutus/script-v2", ".bin")) {
                test_flat(path);
            }
        };
    };
};
