/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/common/test.hpp>
#include <dt/cardano/ledger/conway.hpp>
#include <dt/cardano/ledger/state.hpp>

namespace {
    using namespace daedalus_turbo;
    using namespace cardano;
    using namespace ledger::conway;
}

suite cardano_ledger_conway_suite = [] {
    using boost::ext::ut::v2_1_0::nothrow;
    "cardano::ledger::conway::state"_test = [] {
        const credential_t id0 { blake2b<key_hash>(std::string_view { "0" }), false };
        "num_dormant_epochs"_test = [&] {
            state st {};
            st.process_cert(reg_drep_cert { id0 }, cert_loc_t { 0, 0, 1 });
            test_same(0, st.num_dormant_epochs());
            st.start_epoch({});
            test_same(1, st.num_dormant_epochs());
            test_same(20, st.drep_state().at(id0).expire_epoch);
            proposal_t p {};
            p.id.tx_id = blake2b<tx_hash>(std::string_view { "A" });
            p.procedure.action.val = gov_action_t::info_action_t {};
            st.process_proposal(p, cert_loc_t { 0, 0, 0 });
            test_same(1, st.num_dormant_epochs());
            st.start_epoch({});
            test_same(0, st.num_dormant_epochs());
            test_same(21, st.drep_state().at(id0).expire_epoch);
        };
        "reg/unreg drep"_test = [&] {
            state st {};

            expect(!st.has_drep(id0));
            st.process_cert(reg_drep_cert { id0 }, cert_loc_t { 0, 0, 1 });
            expect(st.has_drep(id0));
            st.process_cert(unreg_drep_cert {id0 }, cert_loc_t { 1, 0, 0 });
            expect(!st.has_drep(id0));
        };
        "reg/unreg stake"_test = [&] {
            state st {};
            expect(!st.has_stake(id0));
            st.process_cert(reg_cert { id0 }, cert_loc_t { 0, 0, 1 });
            expect(st.has_stake(id0));
            st.process_cert(unreg_cert { id0 }, cert_loc_t { 1, 0, 0 });
            expect(!st.has_drep(id0));
        };
        "update_committee"_test = [] {
            state st {};
            expect(st.committee().has_value());
            if (const auto &cc = st.committee(); cc) {
                test_same(7, cc->members.size());
                test_same(rational_u64 { 2, 3 }, cc->threshold);
            }
            {
                const stake_ident return_addr { blake2b<key_hash>(std::string_view { "A" }), false };
                const gov_action_id_t gid { blake2b<tx_hash>(std::string_view { "A" }), 0 };
                proposal_procedure_t pp {};
                pp.return_addr = return_addr;
                {
                    gov_action_t::update_committee_t c_upd {};
                    c_upd.members_to_remove.emplace(credential_t { script_hash::from_hex("df0e83bde65416dade5b1f97e7f115cc1ff999550ad968850783fe50"), true });
                    c_upd.new_threshold = { 5, 6 };
                    pp.action = gov_action_t { std::move(c_upd) };
                }
                const proposal_t p { gid, std::move(pp) };
                st.process_proposal(p, cert_loc_t { 0, 0, 0 });
            }
            expect(st.committee().has_value());
            if (const auto &cc = st.committee(); cc) {
                test_same(7, cc->members.size());
                test_same(rational_u64 { 2, 3 }, cc->threshold);
            }
        };
        "parameter update"_test = [] {
            state st {};
            gov_action_id_t gid {};
            gid.tx_id = blake2b<tx_hash>(std::string_view { "A" });
            proposal_t p {};
            p.id = gid;
            const cert_loc_t p_loc { 0, 0, 0 };
            st.process_proposal(p, p_loc);
            {
                const auto &ga_st = st.gov_action(gid);
                test_same(0, ga_st.drep_votes.size());
            }

            const credential_t drep_id { blake2b<key_hash>(std::string_view { "B" }), false };
            voter_t voter {};
            voter.type = voter_t::drep_key;
            voter.hash = drep_id.hash;
            const cert_loc_t v_loc { 1, 0, 0 };
            const voting_procedure_t vote_proc { vote_t::yes, {} };
            const vote_info_t vp { voter, gid, vote_proc };
            expect(throws([&] { st.process_vote(vp, v_loc); }));

            st.process_cert(reg_drep_cert { drep_id }, cert_loc_t { 0, 0, 1 });
            expect(nothrow([&] { st.process_vote(vp, v_loc); }));
            {
                const auto &ga_st = st.gov_action(gid);
                test_same(1, ga_st.drep_votes.size());
            }
            const auto ga_lifetime = st.params().gov_action_lifetime;
            test_same(6, ga_lifetime);
            st.process_block(100, 1); // needed so that start_epoch accepts progress
            for (size_t e = 0; e < ga_lifetime; ++e) {
                st.start_epoch({});
                expect(st.has_gov_action(gid));
            }
            st.start_epoch({});
            expect(!st.has_gov_action(gid));
        };
        "committee voting"_test = [] {
            state st {};
            gov_action_id_t gid { blake2b<tx_hash>(std::string_view { "A" }) };
            st.process_proposal(
                proposal_t {
                    gid,
                    proposal_procedure_t {
                        0,
                        stake_ident {},
                        { gov_action_t::info_action_t {} }
                    }
                },
                cert_loc_t { 0, 0, 0 }
            );
            const credential_t drep_id { blake2b<key_hash>(std::string_view { "B" }), false };
            st.process_cert(reg_drep_cert { drep_id }, cert_loc_t { 0, 0, 1 });
            st.process_vote(
                vote_info_t {
                    voter_t { voter_t::drep_key, drep_id.hash },
                    gid,
                    voting_procedure_t { vote_t::yes }
                },
                cert_loc_t { 1, 0, 0 }
            );
            st.start_epoch({});
            const auto &gas = st.pulser_data().proposals.at(gid);
            test_same(false, st.committee_accepted(gas));
            test_same(false, st.pools_accepted(gas));
            test_same(false, st.dreps_accepted(gas));
        };
    };
    "cardano::ledger::conway::vrf_state"_test = [] {
        "max_epoch_slot"_test = [] {
            ledger::conway::vrf_state st { ledger::babbage::vrf_state { ledger::shelley::vrf_state {} } };
            test_same(432000 - 172800, st.max_epoch_slot());
        };
        "cbor load/save"_test = [] {
            ledger::conway::vrf_state st { ledger::babbage::vrf_state { ledger::shelley::vrf_state {} } };
            const auto exp_cbor = file::read(install_path("data/ledger/conway-vrf-state.cbor"));
            st.from_cbor(cbor::zero2::parse(exp_cbor).get());
            ledger::cbor_encoder enc { []{ return era_encoder { era_t::conway }; } };
            st.to_cbor(enc);
            enc.run(scheduler::get(), "vrf_state::to_cbor");
            const auto act_cbor = enc.flat();
            test_same(exp_cbor.size(), act_cbor.size());
            test_same(static_cast<buffer>(exp_cbor), act_cbor);
        };
    };
};