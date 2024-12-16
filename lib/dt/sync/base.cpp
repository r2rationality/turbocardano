/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2024 Alex Sierkov (alex dot sierkov at gmail dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <dt/sync/base.hpp>
#include <dt/txwit/validator.hpp>

namespace daedalus_turbo::sync {
    validation_mode_t validation_mode_from_text(const std::string_view s)
    {
        if (s == "none")
            return validation_mode_t::none;
        if (s == "turbo")
            return validation_mode_t::turbo;
        if (s == "full")
            return validation_mode_t::full;
        throw error(fmt::format("unsupported validation mode: {}", s));
    }

    struct syncer::impl {
        impl(syncer &parent, chunk_registry &cr, peer_selection &ps)
            : _parent { parent }, _cr { cr }, _ps { ps }
        {
            _cr.register_processor(_proc);
        }

        ~impl()
        {
            _cr.remove_processor(_proc);
        }

        bool sync(peer_info &peer, cardano::optional_slot max_slot, const validation_mode_t mode)
        {
            logger::info("attempting to sync with {} with the tip {}", peer.id(), peer.tip());
            const auto start_tip = _cr.tip();
            static constexpr size_t max_retries = 1;
            auto start_point = peer.intersection();
            optional_progress_point target { peer.tip() };
            if (target) {
                // explicitly set the max slot to ensure that the progress is computed correctly
                if (!max_slot)
                    max_slot = target->slot;
                if (max_slot && *max_slot < target->slot) {
                    logger::info("user override of the target: up to {}", *max_slot);
                    target = max_slot;
                }
            }

            if (peer.intersection() < target && peer.intersection() < peer.tip()) {
                for (size_t num_retries = max_retries; num_retries; --num_retries) {
                    logger::info("syncing from {} to {}", start_point, target);
                    const auto ex_ptr = _cr.accept_progress(start_point, target, [&] {
                        _cr.validation_failure_handler([this](auto max_valid_offset) {
                            _parent.cancel_tasks(max_valid_offset);
                        });
                        _parent.sync_attempt(peer, max_slot);
                    });
                    if (!ex_ptr) {
                        _cr.remover().remove();
                        break;
                    }
                    // reset the retry count if made progress
                    if (const auto end_tip = _cr.tip(); end_tip && start_point < end_tip) {
                        num_retries = max_retries;
                        start_point = end_tip;
                    }
                    logger::info("retrying after a failure, number of planned attempts: {}", num_retries);
                }
            }
            auto new_local_tip = _cr.tip();
            if (new_local_tip != start_point && mode != validation_mode_t::none) {
                timer t { fmt::format("{} transaction witness validation", mode), logger::level::info };
                logger::info("the post-download tip: {}", new_local_tip);
                cardano::optional_point validate_from = start_point;
                if (mode == validation_mode_t::turbo) {
                    const auto tail = _cr.tail_relative_stake();
                    if (!tail.empty() && tail.begin()->second > 0.5 && start_point < tail.begin()->first)
                        validate_from = tail.begin()->first;
                }
                const auto new_valid_tip = txwit::validate(_cr, validate_from, new_local_tip, txwit::witness_type::all);
                logger::debug("the new valid tip: {}", new_valid_tip);
                if (new_valid_tip != new_local_tip)
                    _cr.truncate(new_valid_tip);
            }

            logger::info("the post-txwit tip: {}", new_local_tip);
            // the new chain's tip can be smaller but have a better chain, so compare for equality here
            return start_tip != _cr.tip();
        }

        chunk_registry &local_chain() noexcept
        {
            return _cr;
        }

        peer_selection &peer_list() noexcept
        {
            return _ps;
        }

        void on_progress(const std::string &name, uint64_t rel_pos, const uint64_t rel_target)
        {
            progress::get().update(name, rel_pos, rel_target);
        }
    private:
        syncer &_parent;
        chunk_registry &_cr;
        peer_selection &_ps;
        chunk_processor _proc {
            .on_progress = [this](const auto name, const auto rel_pos, const auto rel_target) {
                _parent.on_progress(name, rel_pos, rel_target);
            }
        };
    };

    syncer::syncer(chunk_registry &cr, peer_selection &ps)
        : _impl { std::make_unique<impl>(*this, cr, ps) }
    {
    }

    syncer::~syncer() =default;

    bool syncer::sync(const std::shared_ptr<peer_info> &peer, const cardano::optional_slot max_slot, const validation_mode_t mode)
    {
        if (!peer)
            throw error("peer must be initialized!");
        return _impl->sync(*peer, max_slot, mode);
    }

    chunk_registry &syncer::local_chain() noexcept
    {
        return _impl->local_chain();
    }

    peer_selection &syncer::peer_list() noexcept
    {
        return _impl->peer_list();
    }

    void syncer::on_progress(const std::string_view name, const uint64_t rel_pos, const uint64_t rel_target)
    {
        _impl->on_progress(std::string { name }, rel_pos, rel_target);
    }
}