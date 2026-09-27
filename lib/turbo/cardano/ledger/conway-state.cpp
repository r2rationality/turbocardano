/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/ledger/conway.hpp>
#include <turbo/cardano/ledger/conway/detail.hpp>
#include <turbo/cardano/ledger/updates.hpp>

namespace turbo::cardano::ledger::conway {
    vrf_state::vrf_state(babbage::vrf_state &&o): babbage::vrf_state { std::move(o) }
    {
        _max_epoch_slot = _cfg.shelley_epoch_length - _cfg.shelley_randomness_stabilization_window;
        logger::debug("conway::vrf_state created max_epoch_slot: {}", _max_epoch_slot);
    }

    state::state(): state { babbage::state { shelley::state { cardano::config::get(), scheduler::get() } } }
    {
        const protocol_version pv { 9, 0 };
        _params.protocol_ver = pv;
        _params_prev.protocol_ver = pv;
        _ratify_state.new_state.params.protocol_ver = pv;
        _num_dormant_epochs = 0;
    }

#include <turbo/cardano/ledger/transition/conway.ipp>

#include <turbo/cardano/ledger/rules/certs/conway.ipp>

    bool state::has_drep(const credential_t &id) const
    {
        return _drep_state.contains(id);
    }

#include <turbo/cardano/ledger/rules/utxos/conway.ipp>

#include <turbo/cardano/ledger/rules/ledger/conway.ipp>

    bool state::has_gov_action(const gov_action_id_t &gid) const
    {
        return _proposals.contains(gid);
    }

    const gov_action_state_t &state::gov_action(const gov_action_id_t &gid) const
    {
        return detail::map_nice_at(_proposals, gid);
    }

    const optional_committee_t &state::committee() const
    {
        return _enact_state.committee;
    }
}
