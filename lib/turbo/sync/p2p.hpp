#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/common/common.hpp>
#include <turbo/sync/base.hpp>

namespace turbo::sync::p2p {
    struct peer_info: sync::peer_info {
        peer_info(std::unique_ptr<cardano::network::client> &&client, const cardano::point3 &tip,
                cardano::optional_point isect):
            _client{std::move(client)},
            _tip{tip},
            _isect{std::move(isect)}
        {
        }

        peer_info(std::unique_ptr<cardano::network::client> &&client, const cardano::point3 &tip):
            _client{std::move(client)},
            _tip{tip}
        {
            if (!_client)
                throw error("client instance must be defined for all p2p peers");
        }

        ~peer_info() override =default;

        std::string id() const override
        {
            return fmt::format("{}", _client->addr());
        }

        const cardano::point3 &tip() const override
        {
            return _tip;
        }

        void intersection(const cardano::optional_point &new_isect) override
        {
            _isect = new_isect;
        }

        const cardano::optional_point &intersection() const override
        {
            return _isect;
        }

        cardano::network::client &client()
        {
            return *_client;
        }
    private:
        std::unique_ptr<cardano::network::client> _client;
        cardano::point3 _tip;
        std::optional<cardano::point> _isect{};
    };

    struct syncer: sync::syncer {
        explicit syncer(chunk_registry &cr, peer_selection &ps=peer_selection_simple::get(),
            cardano::network::client_manager &cnc=cardano::network::client_manager_async::get());
        ~syncer() override;
        [[nodiscard]] std::shared_ptr<sync::peer_info> find_peer(std::optional<network::address> addr={}, const version_config_t &versions={}) const;
        void cancel_tasks(uint64_t max_valid_offset) override;
        void sync_attempt(sync::peer_info &peer, cardano::optional_slot max_slot) override;
    private:
        struct impl;
        std::unique_ptr<impl> _impl;
    };
}

namespace fmt {
    template<>
    struct formatter<turbo::sync::p2p::peer_info>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            return fmt::format_to(ctx.out(), "(addr: {}, tip: {})", v.client->addr(), v.tip);
        }
    };
}