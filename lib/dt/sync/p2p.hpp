/* This file is part of Daedalus Turbo project: https://github.com/sierkov/daedalus-turbo/
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2025 R2 Rationality OÜ (info at r2rationality dot com)
 * This code is distributed under the license specified in:
 * https://github.com/sierkov/daedalus-turbo/blob/main/LICENSE */
#ifndef DAEDALUS_TURBO_SYNC_P2P_HPP
#define DAEDALUS_TURBO_SYNC_P2P_HPP

#include <dt/cardano/common/common.hpp>
#include <dt/sync/base.hpp>

namespace daedalus_turbo::sync::p2p {
    struct peer_info: sync::peer_info {
        peer_info(std::unique_ptr<cardano::network::client> &&client, const std::optional<cardano::point> &tip,
            const std::optional<cardano::point> &isect)
            : _client { std::move(client) }, _tip { tip }, _isect { isect }
        {
        }

        peer_info(std::unique_ptr<cardano::network::client> &&client, const std::optional<cardano::point> &tip)
            : _client { std::move(client) }, _tip { tip }
        {
            if (!_client)
                throw error("client instance must be defined for all p2p peers");
        }

        ~peer_info() override =default;

        std::string id() const override
        {
            return fmt::format("{}", _client->addr());
        }

        const cardano::optional_point &tip() const override
        {
            return _tip;
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
        std::optional<cardano::point> _tip {};
        std::optional<cardano::point> _isect {};
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
    struct formatter<daedalus_turbo::sync::p2p::peer_info>: formatter<int> {
        template<typename FormatContext>
        auto format(const auto &v, FormatContext &ctx) const -> decltype(ctx.out())
        {
            return fmt::format_to(ctx.out(), "(addr: {}, tip: {})", v.client->addr(), v.tip);
        }
    };
}

#endif // !DAEDALUS_TURBO_SYNC_P2P_HPP