/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/sync/base.hpp>
#include <turbo/common/scope-exit.hpp>

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
            // An empty upstream must not cause a local rollback.
            if (!peer.tip()) return false;
            const auto near_tip = peer.intersection() && peer.tip().height >= peer.intersection()->height
                && peer.tip().height - peer.intersection()->height <= 256;
            const auto previous_mode = _cr.validation(mode == validation_mode_t::turbo && near_tip ? validation_mode_t::full : mode);
            scope_exit restore_mode { [&] {
                if (!_cr.tx())
                    _cr.validation(previous_mode);
            }};
            const auto start_tip = _cr.tip();
            static constexpr size_t max_retries = 3;
            static constexpr size_t repack_fragment_threshold = 128;
            const auto peer_tip = cardano::point::from_point3(static_cast<cardano::point3>(peer.tip()));
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
                    if (made_progress)
                        logger::run_log_errors([&] {
                            _cr.repack(chunk_registry::repack_mode_t::merge_closed, repack_fragment_threshold);
                        });
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
            logger::info("the validated tip: {}", _cr.tip());
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
