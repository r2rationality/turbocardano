/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "kes.hpp"
#include <turbo/common/bytes.hpp>
#include <turbo/common/test.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::cardano;
}

suite cardano_kes_suite = [] {
    "cardano::kes"_test = [] {
        const auto vkey_data = file::read("./data/kes-vkey.bin"sv);
        const auto sig_data = file::read("./data/kes-sig.bin"sv);
        const auto msg_data = file::read("./data/kes-msg.bin"sv);
        "construct"_test = [&] {
            expect(boost::ut::nothrow([&]{ kes_signature<6> sig(sig_data); })) << "constructor failed";
        };
        "verify_ok"_test = [&] {
            kes_signature<6> sig { sig_data };
            expect(sig.verify(34, kes_vkey_span(vkey_data), msg_data)) << "key verification failed";
        };
        "verify_fail"_test = [&] {
            kes_signature<6> sig { sig_data };
            expect(!sig.verify(33, kes_vkey_span(vkey_data), msg_data));
            expect(!sig.verify(35, kes_vkey_span(vkey_data), msg_data));
            expect( throws([&] { return sig.verify(10035, kes_vkey_span(vkey_data), msg_data); }));
            auto msg2 = msg_data;
            msg2[0] = msg2[0] ^ msg2[1];
            expect(!sig.verify(34, kes_vkey_span(vkey_data), msg2));
            auto vkey2 = vkey_data;
            vkey2[0] = vkey2[0] ^ vkey2[1];
            expect(!sig.verify(34, kes_vkey_span(vkey2), msg_data));

            auto sig_data2 = sig_data;
            sig_data2[0] = sig_data2[0] ^ sig_data2[1];
            kes_signature<6> sig2 { sig_data2 };
            expect(!sig2.verify(34, kes_vkey_span(vkey_data), msg_data));
        };
        "sign"_test = [] {
            auto seed1 = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
            kes::secret<6> sk1 { seed1 };
            kes::secret<6>::signature sigma1 {};
            static std::string msg { "hello world!" };
            sk1.sign(sigma1, msg);

            kes::signature<6> sigv { sigma1 };
            expect(sigv.verify(0, sk1.vkey(), msg));
        };
    };
};