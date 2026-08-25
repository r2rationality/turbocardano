/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/conway/transaction.hpp>

namespace turbo::cardano::conway {
    using namespace plutus;

    const cert_list &tx::certs() const
    {
        return _body.certs;
    }

    const input_set &tx::collateral_inputs() const
    {
        return _body.collateral_inputs;
    }

    const std::optional<tx_output> &tx::collateral_return() const
    {
        return _body.collateral_return;
    }

    const std::optional<uint64_t> &tx::collateral_value() const
    {
        return _body.collateral_value;
    }

    uint64_t tx::fee() const
    {
        return _body.fee;
    }

    const tx_hash &tx::hash() const
    {
        if (!_body.hash)
            _body.hash.emplace(crypto::blake2b::digest<tx_hash>(_body.raw));
        return *_body.hash;
    }

    const input_set &tx::inputs() const
    {
        return _body.inputs;
    }

    const tx_output_list &tx::outputs() const
    {
        return _body.outputs;
    }

    buffer tx::raw() const
    {
        return _body.raw;
    }

    const input_set &tx::ref_inputs() const
    {
        return _body.ref_inputs;
    }

    const signer_set &tx::required_signers() const
    {
        return _body.required_signers;
    }

    std::optional<uint64_t> tx::current_treasury() const
    {
        return _body.current_treasury;
    }

    uint64_t tx::donation() const
    {
        return _body.donation.value_or(0);
    }

    const multi_mint_map &tx::mints() const
    {
        return _body.mints;
    }

    const proposal_set &tx::proposals() const
    {
        return _body.proposals;
    }

    const vote_set &tx::votes() const
    {
        return _body.votes;
    }

    const param_update_proposal_list &tx::updates() const
    {
        return _body.updates;
    }

    std::optional<uint64_t> tx::validity_start() const
    {
        return _body.validity_start;
    }

    std::optional<uint64_t> tx::validity_end() const
    {
        return _body.validity_end;
    }

    const withdrawal_map &tx::withdrawals() const
    {
        return _body.withdrawals;
    }
}
