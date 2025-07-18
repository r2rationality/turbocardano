/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/ledger/babbage.hpp>
#include <turbo/common/test.hpp>

using namespace turbo;
using namespace cardano;
using namespace ledger;

suite cardano_ledger_babbage_vrf_state_suite = [] {
    "cardano::ledger::babbage::vrf_state"_test = [] {
        "max_epoch_slot"_test = [] {
            ledger::babbage::vrf_state st { ledger::shelley::vrf_state {} };
            expect_equal(432000 - 129600, st.max_epoch_slot());
        };
        "cbor load/save"_test = [] {
            ledger::babbage::vrf_state st { ledger::shelley::vrf_state {} };
            const auto exp_cbor = file::read(install_path("data/ledger/babbage-vrf-state.cbor"));
            st.from_cbor(cbor::zero2::parse(exp_cbor).get());
            cbor_encoder enc { []{ return era_encoder { era_t::babbage }; } };
            st.to_cbor(enc);
            enc.run(scheduler::get(), "vrf_state::to_cbor");
            const auto act_cbor = enc.flat();
            expect_equal(exp_cbor.size(), act_cbor.size());
            expect_equal(static_cast<buffer>(exp_cbor), act_cbor);
        };
    };
};