/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/sync/base.hpp>
#include <turbo/txwit/validator.hpp>

namespace turbo::sync {
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
        impl(syncer &parent, chunk_registry &cr, cardano::network::peer_selection &ps)
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
            logger::info("attempting to sync with {} with the tip {}; validation mode: {}", peer.id(), peer.tip(), mode);
            const auto start_tip = _cr.tip();
            static constexpr size_t max_retries = 3;
            const auto peer_tip = cardano::point::from_point3(peer.tip());
            progress_point target{peer_tip};
            // explicitly set the max slot to ensure that the progress is computed correctly
            if (!max_slot)
                max_slot = target.slot;
            if (max_slot && *max_slot < target.slot) {
                logger::info("user override of the target: up to {}", *max_slot);
                target = progress_point{*max_slot};
            }
            if (!peer.intersection() || (peer.intersection() < target && peer.intersection() < peer_tip)) {
                for (size_t num_retries = max_retries; num_retries; --num_retries) {
                    logger::info("syncing from {} to {}", peer.intersection(), target);
                    const auto ex_ptr = _cr.accept_progress(peer.intersection(), target, [&] {
                        _cr.validation_failure_handler([this](auto max_valid_offset) {
                            logger::debug("sync::base: validation_failure_handler");
                            _parent.cancel_tasks(max_valid_offset);
                        });
                        _parent.sync_attempt(peer, max_slot);
                    });
                    const auto end_tip = _cr.tip();
                    const auto made_progress = end_tip && peer.intersection() < end_tip;
                    // Recoverable action errors can still commit progress. Rolled-back attempts may leave
                    // restore-needed files marked until a later successful transaction unmarks them.
                    if (!ex_ptr || made_progress)
                        _cr.remover().remove();
                    if (!ex_ptr) {
                        break;
                    }
                    // reset the retry count if made progress
                    if (made_progress) {
                        num_retries = max_retries + 1; // +1 to adjust for the post-cycle-body decrement
                        peer.intersection(end_tip);
                    }
                    if (num_retries > 1) {
                        logger::info("retrying after a failure, attempts left: {}", num_retries - 1);
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    }
                }
            }
            auto new_local_tip = _cr.tip();
            if (new_local_tip != peer.intersection() && mode != validation_mode_t::none) {
                timer t { fmt::format("{} transaction witness validation", mode), logger::level::info };
                logger::info("the post-download tip: {}", new_local_tip);
                cardano::optional_point validate_from = peer.intersection();
                if (mode == validation_mode_t::turbo) {
                    if (const auto core = _cr.core_tip(); core
                            && (!peer.intersection() || peer.intersection()->end_offset < core->end_offset))
                        validate_from = core;
                }
                const auto new_valid_tip = txwit::validate(_cr, validate_from, new_local_tip, txwit::witness_type::all);
                logger::debug("the new valid tip: {}", new_valid_tip);
                if (new_valid_tip != new_local_tip) {
                    _cr.truncate(new_valid_tip);
                    new_local_tip = _cr.tip();
                }
            }

            logger::info("the post-txwit tip: {}", new_local_tip);
            // the new chain's tip can be smaller but have a better chain, so compare for equality here
            return start_tip != _cr.tip();
        }

        chunk_registry &local_chain() noexcept
        {
            return _cr;
        }

        cardano::network::peer_selection &peer_list() noexcept
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
        cardano::network::peer_selection &_ps;
        chunk_processor _proc {
            .on_progress = [this](const auto name, const auto rel_pos, const auto rel_target) {
                _parent.on_progress(name, rel_pos, rel_target);
            }
        };
    };

    syncer::syncer(chunk_registry &cr, cardano::network::peer_selection &ps)
        : _impl { std::make_unique<impl>(*this, cr, ps) }
    {
    }

    syncer::~syncer() =default;

    bool syncer::sync(const std::shared_ptr<peer_info> &peer, const cardano::optional_slot max_slot, const validation_mode_t mode)
    {
        if (!peer) [[unlikely]]
            throw error("peer must be initialized!");
        return _impl->sync(*peer, max_slot, mode);
    }

    chunk_registry &syncer::local_chain() noexcept
    {
        return _impl->local_chain();
    }

    cardano::network::peer_selection &syncer::peer_list() noexcept
    {
        return _impl->peer_list();
    }

    void syncer::on_progress(const std::string_view name, const uint64_t rel_pos, const uint64_t rel_target)
    {
        _impl->on_progress(std::string { name }, rel_pos, rel_target);
    }
}
