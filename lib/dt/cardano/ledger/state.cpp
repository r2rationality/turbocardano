/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/cardano/ledger/state.hpp>
#include <dt/cardano/ledger/babbage.hpp>
#include <dt/cardano/ledger/conway.hpp>
#include <dt/cardano/ledger/updates.hpp>
#include <dt/cbor/encoder.hpp>
#include <dt/progress.hpp>
#include <dt/scheduler.hpp>
#include <dt/timer.hpp>
#include <dt/zpp.hpp>

// Disable a windows macro
#ifdef small
#   undef small
#endif

namespace daedalus_turbo::cardano::ledger {
    state::state(const cardano::config &cfg, scheduler &sched):
        _cfg { cfg }, _sched { sched },
        _state { std::make_unique<shelley::state>(_cfg, _sched) },
        _vrf_state { std::make_unique<shelley::vrf_state>(_cfg) }
    {
    }

    point state::_deserialize_node_vrf_state(cbor::zero2::value &v)
    {
        auto &it = v.array();
        const auto tip = point::from_cbor(it.read().at(0).at(1));
        {
            auto &forks = it.read();
            auto &it2 = forks.array();
            if (forks.indefinite()) [[unlikely]]
                throw error("forks must be encoded as a non-indefinite CBOR array!");
            const auto num_forks = forks.special_uint();
            for (size_t era = 1; !it2.done(); ++era) {
                auto &fork = it2.read();
                if (era == num_forks) {
                    _transition_vrf_era(0, era);
                    _vrf_state->from_cbor(fork.at(1));
                }
            }
        }
        return tip;
    }

    void state::_deserialize_node_ledger_state(cbor::zero2::value &v)
    {
        auto &it = v.array_sized();
        const auto num_forks = v.special_uint();

        for (size_t era = 1; !it.done(); ++era) {
            auto &fork = it.read();
            _transition_ledger_era(_eras.size(), era);
            if (era == num_forks) {
                auto &fork_it = fork.array();
                _eras.emplace_back(fork_it.read().at(1).uint());
                _state->from_cbor(fork_it.read().at(1));
                return;
            } else {
                const auto next_era_slot = fork.at(0).at(1).uint();
                _eras.emplace_back(next_era_slot ? next_era_slot : 0);
            }
        }
        throw error("empty ledger state!");
    }

    point state::deserialize_node(const buffer data)
    {
        auto item = cbor::zero2::parse(data);
        const auto tip = decode_versioned(item.get(), [&](auto &v) {
            auto &it = v.array();
            _deserialize_node_ledger_state(it.read());
            return _deserialize_node_vrf_state(it.read());
        });
        return tip;
    }

    point state::load_node(const std::string &path)
    {
        const auto buf = file::read(path);
        return deserialize_node(buf);
    }

    void state::_serialize_node_state(cbor_encoder &ser, const point &tip) const
    {
        ser.add([&](auto enc) {
            enc.array(_eras.size());
            for (size_t era = 0; era < _eras.size(); ++era) {
                enc.array(2);
                slot { _eras[era], _cfg }.to_cbor(enc);
                if (era + 1 < _eras.size()) {
                    slot { _eras[era + 1], _cfg }.to_cbor(enc);
                } else {
                    enc.array(2)
                        .uint(2)
                        .array(3)
                            .array(1).array(3).uint(tip.slot).uint(tip.height).bytes(tip.hash);
                }
            }
            return std::move(enc.cbor());
        });
        _state->to_cbor(ser);
    }

    void state::_serialize_node_vrf_state(cbor_encoder &ser, const point &tip) const
    {
        ser.add([this, tip](auto enc) {
            enc.array(2);
            if (!_eras.empty()) {
                enc.array(1)
                    .array(2)
                        .uint(_eras.size() - 1)
                        .array(3)
                            .uint(tip.slot)
                            .bytes(tip.hash)
                            .uint(tip.height);
            } else {
                enc.array(0);
            }
            enc.array(_eras.size());
            for (size_t era = 0; era < _eras.size(); ++era) {
                enc.array(2);
                slot { _eras[era], _cfg }.to_cbor(enc);
                if (era + 1 < _eras.size()) {
                    slot { _eras[era + 1], _cfg }.to_cbor(enc);
                }
            }
            return std::move(enc.cbor());
        });
        _vrf_state->to_cbor(ser);
    }

    cbor_encoder state::to_cbor(const point &tip, const int prio) const
    {
        timer t { "serialize the state into the Cardano Node format", logger::level::info };
        cbor_encoder ser { [&] { return era_encoder { era_from_number(_eras.size()) }; } };
        ser.add([](auto enc) {
            enc.array(2); // versioned encoding tuple
            enc.uint(1); // version number
            enc.array(2);
            return std::move(enc.cbor());
        });
        _serialize_node_state(ser, tip);
        _serialize_node_vrf_state(ser, tip);
        ser.run(_sched, "ledger-export", prio, true);
        return ser;
    }

    void state::save_node(const std::string &path, const point &tip, const int prio) const
    {
        const auto ser = to_cbor(tip, prio);
        timer t { "write the serialized node state to a file", logger::level::info };
        ser.save(path, false);
        progress::get().update("ledger-export", 1, 1);
    }

    void state::load_zpp(const std::string &path)
    {
        try {
            parallel_decoder dec { path };
            zpp::deserialize(_subchains, dec.at(0));
            dec.add([&](const auto) {
                // do nothing as the field has been already decoded above
            });
            zpp::deserialize(_eras, dec.at(1));
            dec.add([&](const auto) {
                // do nothing as the field has been already decoded above
            });
            _transition_era(0, _eras.size());
            _state->from_zpp(dec);
            _vrf_state->from_zpp(dec);
            dec.run(_sched, "parallel_decoder::run", 1000);
            if (!_subchains.empty()) {
                // Reserve snapshots can be saved to disk before the validation is fully finished.
                // However, they will be renamed into their proper names only when they become valid.
                // Thus, if we load a snapshot we need to merge the subchains.
                for (auto &[offset, sc]: _subchains) {
                    if (!sc)
                        sc.valid_blocks = sc.num_blocks;
                }
                _subchains.merge_valid();
                if (_subchains.size() > 1)
                    throw error(fmt::format("inconsistent subschain list: {}", _subchains));
                const auto &sc = _subchains.rbegin()->second;
                if (sc.offset != 0 || sc.end_offset() != end_offset())
                    throw error(fmt::format("the local subchain range: [{}:{}] does not match the chain: [0:{}]",
                        sc.offset, sc.end_offset(), end_offset()));
            }
            if (end_offset() != valid_end_offset())
                throw error(fmt::format("validator state from {} is in inconsistent state valid_end_offset: {} vs end_offset: {}",
                    path, valid_end_offset(), end_offset()));
        } catch (const std::exception &ex) {
            const auto err_path = path + ".err";
            std::filesystem::rename(path, err_path);
            throw error(fmt::format("loading state failed: {} moved the invalid state file to {}", ex.what(), err_path));
        }
    }

    void state::save_zpp(const std::string &path, const std::unique_ptr<subchain_list> tmp_sc)
    {
        zpp_encoder ser {};
        ser.add([&](auto) {
            mutex::scoped_lock lk { _subchains_mutex };
            std::unique_ptr<subchain_list> orig_sc {};
            if (tmp_sc) {
                orig_sc = std::make_unique<subchain_list>(_subchains);
                _subchains = std::move(*tmp_sc);
            }
            auto res = zpp::serialize(_subchains);
            if (orig_sc)
                _subchains = std::move(*orig_sc);
            return res;
        });
        ser.add([&](auto) {
            return zpp::serialize(_eras);
        });
        _state->to_zpp(ser);
        _vrf_state->to_zpp(ser);
        ser.run(_sched, "ledger-state:save-state-snapshot");
        ser.save(path, true);
    }

    void state::clear()
    {
        _subchains.clear();
        _eras.clear();
        _state = std::make_unique<shelley::state>(_cfg, _sched);
        _vrf_state = std::make_unique<shelley::vrf_state>(_cfg);
    }

    bool state::operator==(const state &o) const
    {
        return _subchains == o._subchains
            && _eras == o._eras
            && *_state == *o._state
            && *_vrf_state == *o._vrf_state;
    }

    void state::track_era(const uint64_t era, const uint64_t slot)
    {
        if (era > 0) [[likely]] {
            if (!_eras.empty() && slot < _eras.back()) [[unlikely]]
                throw error(fmt::format("era blocks have reported out of order slot {} came after {}", slot, _eras.back()));
            if (era > _eras.size()) [[unlikely]] {
                const auto era_start_slot = !_eras.empty() && era > 2 ? slot - (slot - _eras.back()) % _cfg.shelley_epoch_length : slot;
                while (era > _eras.size()) {
                    _eras.emplace_back(era_start_slot);
                }
            } else if (era < _eras.size()) [[unlikely]] {
                throw error(fmt::format("a block of era {} came in era {}", era, _eras.size()));
            }
        }
    }

    void state::_transition_ledger_era(const uint64_t from_era, const uint64_t to_era)
    {
        logger::debug("transition_ledger_era from: {} to: {}", from_era, to_era);
        // Protocol versions may be upgraded in large increments.
        // The loop insures that all the necessary internal transitions still happen
        for (size_t new_era = from_era + 1; new_era <= to_era; ++new_era) {
            switch (new_era) {
                case 1:
                case 2:
                case 3:
                case 4:
                    break;
                case 5:
                    _state = std::make_unique<alonzo::state>(std::move(*_state));
                    break;
                case 6:
                    _state = std::make_unique<babbage::state>(std::move(dynamic_cast<alonzo::state &>(*_state)));
                    break;
                case 7:
                    _state = std::make_unique<conway::state>(std::move(dynamic_cast<babbage::state &>(*_state)));
                    break;
                default: throw error(fmt::format("unsupported era: {}", new_era));
            }
        }
    }

    void state::_transition_vrf_era(const uint64_t from_era, const uint64_t to_era)
    {
        logger::debug("transition_vrf_era from: {} to: {}", from_era, to_era);
        for (size_t new_era = from_era + 1; new_era <= to_era; ++new_era) {
            switch (new_era) {
                case 1:
                case 2:
                case 3:
                case 4:
                    break;
                case 5:
                    _vrf_state = std::make_unique<alonzo::vrf_state>(std::move(*_vrf_state));
                    break;
                case 6:
                    _vrf_state = std::make_unique<babbage::vrf_state>(std::move(dynamic_cast<alonzo::vrf_state &>(*_vrf_state)));
                    break;
                case 7:
                    _vrf_state = std::make_unique<conway::vrf_state>(std::move(dynamic_cast<babbage::vrf_state &>(*_vrf_state)));
                    break;
                default: throw error(fmt::format("unsupported era: {}", new_era));
            }
        }
    }

    void state::_transition_era(const uint64_t from_era, const uint64_t to_era)
    {
        _transition_vrf_era(from_era, to_era);
        _transition_ledger_era(from_era, to_era);
    }

    void state::start_epoch(const std::optional<uint64_t> new_epoch)
    {
        const auto prev_pv = _state->_params.protocol_ver;
        _state->start_epoch(new_epoch);
        if (!_vrf_state->kes_counters().empty())
            _vrf_state->finish_epoch(_state->_params.extra_entropy);
        const auto new_pv = _state->_params.protocol_ver;
        if (new_pv != prev_pv) {
            if (new_pv < prev_pv) [[unlikely]]
                throw error(fmt::format("protocol downgrades are not supported: went from {} to {}", prev_pv, new_pv));
            _transition_era(prev_pv.era(), new_pv.era());
            const auto tip_slot = slot::from_epoch(_state->_epoch, _state->_epoch_slot, _cfg);
            track_era(new_pv.era(), tip_slot);
        }
    }

    void state::process_cert(const cert_t &cert, const cert_loc_t &loc)
    {
        _state->process_cert(cert, loc);
    }

    void state::process_updates(updates_t &&updates)
    {
        for (const auto &u: updates.blocks)
            track_era(u.era, u.slot);
        _state->process_updates(std::move(updates));
    }
}