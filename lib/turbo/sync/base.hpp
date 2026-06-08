#pragma once
/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */

#include <turbo/cardano/network/peer-selection.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/chunk-registry.hpp>

namespace turbo::sync {
    enum class validation_mode_t { turbo, full, none };
    extern validation_mode_t validation_mode_from_text(std::string_view);

    struct peer_info {
        virtual ~peer_info() =default;
        virtual std::string id() const =0;
        virtual const cardano::point3 &tip() const =0;
        virtual const cardano::optional_point &intersection() const =0;
        virtual void intersection(const cardano::optional_point &) =0;
    };

    struct syncer {
        syncer(chunk_registry &cr, cardano::network::peer_selection &pr=cardano::network::peer_selection_simple::get());
        virtual ~syncer();
        virtual bool sync(const std::shared_ptr<peer_info> &peer, cardano::optional_slot max_slot={}, validation_mode_t mode=validation_mode_t::turbo);
        chunk_registry &local_chain() noexcept;
        cardano::network::peer_selection &peer_list() noexcept;
    protected:
        virtual void cancel_tasks(uint64_t max_valid_offset) =0;
        virtual void sync_attempt(peer_info &peer, cardano::optional_slot max_slot) =0;
        virtual void on_progress(std::string_view name, uint64_t rel_pos, uint64_t rel_target);
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::sync::validation_mode_t>: formatter<int32_t> {
        template<typename FormatContext>
        auto format(const turbo::sync::validation_mode_t &v, FormatContext &ctx) const -> decltype(ctx.out()) {
            using turbo::sync::validation_mode_t;
            switch (v) {
                case validation_mode_t::full: return fmt::format_to(ctx.out(), "full");
                case validation_mode_t::turbo: return fmt::format_to(ctx.out(), "turbo");
                case validation_mode_t::none: return fmt::format_to(ctx.out(), "none");
                default: throw turbo::error(fmt::format("unsupported validation_mode_t value: {}", static_cast<int>(v)));
            }
        }
    };
}