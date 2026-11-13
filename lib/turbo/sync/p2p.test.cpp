/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <iterator>
#include <turbo/cardano.hpp>
#include <turbo/common/test.hpp>
#include <turbo/sync/mocks.hpp>
#include <turbo/sync/p2p.hpp>

namespace {
    using namespace turbo;
    using namespace turbo::sync;

    struct unexpected_peer_selection: peer_selection {
        size_t selections = 0;
    private:
        network::address _next_cardano_impl() override
        {
            ++selections;
            return { "unexpected", "0" };
        }
    };
}

suite sync_p2p_suite = [] {
    "sync::p2p"_test = [] {
        const std::string data_dir { "./tmp/test-sync-p2p" };
        auto &ps = peer_selection_simple::get();
        mock_chain_config mock_cfg {};
        const auto good_chain = gen_chain(mock_cfg);
        const cardano::config ccfg { good_chain.cfg };
        cardano_client_manager_mock ccm { good_chain.data };
        "success"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry_settings_t { .ccfg=ccfg } };
            p2p::syncer s { cr, ps, ccm };
            expect(s.sync(s.find_peer()));
            expect_equal(cr.num_blocks(), 9);
        };
        const auto check_compressed_sync = [&](const size_t announced_last, const size_t blocks_per_message,
            const optional_slot max_slot, const size_t expected_blocks) {
            const file::tmp_directory dir { "sync-p2p-compressed-boundary" };
            scheduler sched { 4 };
            file_remover remover;
            chunk_registry cr { dir.path(), chunk_registry_settings_t {
                .validate_vrf=false, .ccfg=ccfg, .fr=remover, .sched=sched } };
            struct compressed_client: cardano_client_mock {
                const mock_chain &chain;
                const size_t blocks_per_message;
                size_t requests = 0;
                size_t interrupted = 0;
                optional_point2 requested_tip;
                std::function<void(scheduler &)> pending;

                compressed_client(const mock_chain &c, const size_t batch)
                    : cardano_client_mock { network::address { "mock", "0" }, c.data, 0.0 },
                        chain { c }, blocks_per_message { batch } {}

                void _fetch_blocks_impl(const point2 &from, const point2 &to, const block_handler &handler) override
                {
                    ++requests;
                    requested_tip = to;
                    pending = [this, from, to, handler](scheduler &sched) {
                        auto it = std::ranges::find_if(chain.blocks, [&](const auto &b) { return b->blk->hash() == from.hash; });
                        expect(it != chain.blocks.end()) << fatal;
                        bool last = false;
                        while (it != chain.blocks.end() && !last) {
                            uint8_vector raw;
                            for (size_t n = 0; n < blocks_per_message && it != chain.blocks.end() && !last; ++n, ++it) {
                                raw << (*it)->blk.raw();
                                last = (*it)->blk->hash() == to.hash;
                            }
                            const auto more = handler(msg_compressed_blocks_t {
                                msg_compressed_blocks_t::encoding_zstd_fast, zstd::compress(raw, 3) });
                            // Make early-stop regressions deterministic: parsing completes
                            // before the next physical fragment in the same logical chunk.
                            sched.process();
                            if (!more) {
                                ++interrupted;
                                break;
                            }
                        }
                    };
                }

                void _process_impl(scheduler *sched, asio::worker *) override
                {
                    if (auto deliver = std::exchange(pending, {})) {
                        expect(sched != nullptr) << fatal;
                        deliver(*sched);
                    }
                }
            };
            auto client = std::make_unique<compressed_client>(good_chain, blocks_per_message);
            const auto *observed = client.get();
            const auto &last = good_chain.blocks.at(announced_last)->blk;
            const point3 announced_tip { point2 { last->slot(), last->hash() }, last->height() };
            auto peer = std::make_shared<p2p::peer_info>(std::move(client), announced_tip);
            p2p::syncer syncer { cr, ps, ccm };
            expect(syncer.sync(peer, max_slot, validation_mode_t::none));
            expect_equal(cr.num_blocks(), expected_blocks);
            expect_equal(observed->requests, 1);
            expect_equal(observed->requested_tip->hash, announced_tip.hash);
            if (!max_slot)
                expect_equal(observed->interrupted, 0);
            expect_equal(cr.tip()->hash, good_chain.blocks.at(expected_blocks - 1)->blk->hash());
        };
        "fragmented tails use one fetch to the original tip even when the header tip advances"_test = [&] {
            check_compressed_sync(4, 1, {}, 5);
        };
        "compressed slot limits accept whole fragments and discard those beyond the limit"_test = [&] {
            const auto limit = good_chain.blocks.at(4)->blk->slot();
            const auto count = std::ranges::count_if(good_chain.blocks, [&](const auto &b) { return b->blk->slot() <= limit; });
            check_compressed_sync(good_chain.blocks.size() - 1, 1, limit, count);
        };
        "a slot limit between blocks trims a compressed message without another fetch"_test = [&] {
            const auto next = std::ranges::find_if(good_chain.blocks, [&](const auto &b) { return b->blk->slot() > 1; });
            expect(next != good_chain.blocks.end()) << fatal;
            const auto limit = (*next)->blk->slot() - 1;
            check_compressed_sync(good_chain.blocks.size() - 1, good_chain.blocks.size(), limit,
                static_cast<size_t>(next - good_chain.blocks.begin()));
        };
        const auto check_follow = [&](const validator::validation_mode validation) {
            const file::tmp_directory dir { "sync-p2p-follow" };
            constexpr size_t num_updates = 2;
            std::stop_source stop;
            struct live_client: cardano_client_mock {
                const mock_chain &chain;
                size_t next = 0;
                bool at_origin = true;
                explicit live_client(const mock_chain &c)
                    : cardano_client_mock { network::address { "mock", "0" }, c.data, 0.0 }, chain { c } {}
                chain_update next_header_sync(std::stop_token, const std::function<void()> &) override
                {
                    if (at_origin) {
                        at_origin = false;
                        return { true, {}, {} }; // Empty upstream, before its first announcement.
                    }
                    const auto &b = chain.blocks.at(next++)->blk;
                    point3 tip { point2 { b->slot(), b->hash() }, b->height() };
                    return { false, static_cast<point2>(tip), tip };
                }
            };
            struct live_manager: client_manager {
                const mock_chain &chain;
                size_t connections = 0;
                std::stop_source &stop;
                live_manager(const mock_chain &c, std::stop_source &s): chain { c }, stop { s } {}
                std::unique_ptr<client> _connect_impl(const network::address &, const version_config_t &,
                    const cardano::config &, const asio::worker_ptr &) override
                {
                    if (++connections > 1) {
                        stop.request_stop();
                        throw error{ "unexpected reconnect in follow test" };
                    }
                    return std::make_unique<live_client>(chain);
                }
            } manager { good_chain, stop };
            file_remover remover;
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .continuous=true, .ccfg=ccfg, .fr=remover, .validation=validation } };
            p2p::syncer syncer { cr, ps, manager };
            size_t updates = 0;
            syncer.follow(stop.get_token(), [&](const auto &intersection) {
                expect_equal(cr.num_blocks(), ++updates);
                expect_equal(cr.valid_end_offset(), cr.num_bytes());
                expect_equal(intersection.has_value(), updates > 1);
                if (validation == validator::validation_mode::full)
                    expect(cr.validator().snapshots().empty());
                if (updates == num_updates)
                    stop.request_stop();
            }, network::address { "mock", "0" }, {}, std::chrono::seconds { 600 }, validation);
            expect_equal(manager.connections, 1);
            expect_equal(updates, num_updates);
            expect(fatal(!cr.validator().snapshots().empty()));
            expect_equal(cr.validator().snapshots().rbegin()->end_offset, cr.num_bytes()); // orderly shutdown
        };
        "continuous imports reuse one connection and publish validated boundaries"_test = [&] {
            for (const auto validation: { validator::validation_mode::full, validator::validation_mode::turbo })
                check_follow(validation);
        };
        enum class rollback_case {
            initial_deep, initial_boundary, origin, repeated, unknown,
            replacement, interrupted, stopped, commit_requested, short_chain_origin, buffered
        };
        const auto check_follow_rollback_mode = [&](const rollback_case scenario,
            const validator::validation_mode validation, const std::source_location &loc) {
            const auto context = fmt::format("rollback case from {}", loc);
            const file::tmp_directory dir { "sync-p2p-rollback-limit" };
            auto shelley = good_chain.cfg.at("shelley-genesis").json();
            shelley["securityParam"] = scenario == rollback_case::short_chain_origin ? 9 : 2;
            auto chain = gen_chain({ .cfg={{"shelley-genesis", config_json { std::move(shelley) }}} });
            const auto &cfg = chain.cardano_cfg;
            const auto as_point = [](const auto &block) {
                return point3 { point2 { block->slot(), block->hash() }, block->height() };
            };
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
            block_producer producer { crypto::ed25519::create_sk_from_seed(seed), seed, vrf03_create_sk_from_seed(seed) };
            producer.vrf_nonce = cfg.shelley_genesis_hash;
            const auto successor = [&](const point3 &previous, const uint64_t slot) {
                producer.prev_hash = previous.hash;
                producer.height = previous.height + 1;
                producer.slot = slot;
                return std::make_unique<parsed_block>(producer.cbor());
            };
            chain.blocks.resize(7);
            const auto anchor = as_point(chain.blocks.back()->blk);
            const auto deep = as_point(chain.blocks.at(5)->blk);
            // Put both old suffix blocks outside the density window and in a
            // different logical chunk. A single early replacement is denser.
            const auto far_slot = anchor.slot + std::max(cfg.byron_slots_per_chunk, density_default_window) + 2;
            chain.blocks.emplace_back(successor(anchor, far_slot));
            chain.blocks.emplace_back(successor(as_point(chain.blocks.back()->blk), far_slot + 2));
            const auto original_tip = as_point(chain.blocks.back()->blk);
            uint8_vector prefix, suffix;
            for (size_t i = 0; i < chain.blocks.size(); ++i)
                (i < 7 ? prefix : suffix) << chain.blocks[i]->blk.raw();
            const auto seed_registry = [&](chunk_registry &cr) {
                cr.accept_anything_or_throw({}, progress_point { original_tip.slot }, [&] {
                    cr.add_buffer(0, prefix);
                    cr.add_buffer(prefix.size(), suffix);
                });
                expect(cr.chunks().size() > 1) << context;
            };
            const bool replacement_attempt = scenario == rollback_case::replacement || scenario == rollback_case::interrupted
                || scenario == rollback_case::stopped || scenario == rollback_case::commit_requested;
            parsed_block_list fork;
            if (replacement_attempt || scenario == rollback_case::buffered) {
                auto previous = scenario == rollback_case::buffered ? original_tip : anchor;
                for (size_t i = 0; i < 4; ++i) {
                    const auto slot = scenario == rollback_case::replacement && i == 3 ? far_slot + 4 : previous.slot + 2;
                    fork.emplace_back(successor(previous, slot));
                    previous = as_point(fork.back()->blk);
                }
            }
            if (scenario == rollback_case::interrupted) {
                // Control: without the follower guard, this shorter prefix really
                // is accepted. The interruption cases must not pass just because
                // ordinary chain selection already rejects their candidate.
                const file::tmp_directory control_dir { "sync-p2p-shorter-density-control" };
                file_remover control_remover;
                chunk_registry control { control_dir.path(), chunk_registry_settings_t {
                    .validate_vrf=false, .continuous=true, .ccfg=cfg, .fr=control_remover } };
                seed_registry(control);
                const auto start = control.get_block_info(anchor).point();
                const auto failure = control.accept_progress(start, progress_point { fork.front()->blk->slot() }, [&] {
                    control.add_buffer(start.end_offset, uint8_vector { fork.front()->blk.raw() });
                });
                expect(!failure) << context;
                expect_equal(control.num_blocks(), 8, context);
                expect_equal(control.tip()->hash, fork.front()->blk->hash(), context);
            }
            struct follow_state {
                rollback_case scenario;
                const parsed_block_list &fork;
                const std::string &context;
                point3 anchor, deep;
                optional_point selected {};
                chunk_registry *cr = nullptr;
                std::stop_source stop {};
                std::vector<point3> peer_points {};
                std::vector<optional_point2_list> queries {};
                size_t updates = 0, fetches = 0, connections = 0, commits = 0;
            } state { scenario, fork, context, anchor, deep };
            const size_t shared = scenario == rollback_case::initial_deep ? 6
                : scenario == rollback_case::initial_boundary ? 7 : chain.blocks.size();
            for (size_t i = 0; i < shared; ++i)
                state.peer_points.emplace_back(as_point(chain.blocks[i]->blk));
            struct rollback_client: client {
                follow_state &state;
                std::function<void()> pending;
                explicit rollback_client(follow_state &s)
                    : client { network::address { "mock", "0" } }, state { s } {}
                static point3 point_of(const block_container &block)
                {
                    return { point2 { block->slot(), block->hash() }, block->height() };
                }
                chain_update next_header_sync(std::stop_token, const std::function<void()> &) override
                {
                    expect(state.cr->tip() == state.selected) << state.context;
                    const auto step = state.updates++;
                    if (state.scenario == rollback_case::buffered) {
                        const point3 remote { point2 { state.selected->slot + 100, block_hash {} }, state.selected->height + 100 };
                        if (step < 4)
                            return { false, point_of(state.fork[step]->blk), remote };
                        if (step == 4 || step == 5)
                            return { true, point_of(state.fork.front()->blk), remote };
                        // The retained point is accepted; the discarded point must fail.
                        if (step == 6)
                            return { true, point_of(state.fork[3]->blk), remote };
                    } else if (state.scenario != rollback_case::initial_boundary) {
                        if (step == 0) {
                            if (state.scenario == rollback_case::origin || state.scenario == rollback_case::short_chain_origin)
                                return { true, {}, state.selected };
                            if (state.scenario == rollback_case::unknown)
                                return { true, point2 { state.anchor.slot, block_hash {} }, state.selected };
                            return { true, state.anchor, state.selected };
                        }
                        if (step == 1 && state.scenario == rollback_case::repeated)
                            return { true, state.deep, state.selected };
                        if (step == 1 && !state.fork.empty())
                            return { false, point_of(state.fork.front()->blk), point_of(state.fork[2]->blk) };
                        if (state.scenario == rollback_case::replacement) {
                            if (step == 2) {
                                expect_equal(state.commits, 1, state.context);
                                const auto p = point_of(state.fork[3]->blk);
                                return { false, p, p };
                            }
                            if (step == 3) {
                                expect_equal(state.commits, 2, state.context);
                                expect(state.cr->chunks().size() > 1) << state.context;
                                // The original anchor is now four blocks behind.
                                return { true, state.anchor, state.selected };
                            }
                        }
                    }
                    state.stop.request_stop();
                    throw error{ "end of rollback script" };
                }
                void _find_intersection_impl(const optional_point2_list &points, const find_handler &handler) override
                {
                    state.queries.emplace_back(points);
                    for (const auto &p: points) {
                        if (p) {
                            const auto it = state.cr->find_block(*p);
                            expect(it != state.cr->cend()) << state.context << fatal;
                            expect(state.cr->cend() - (it + 1) <= static_cast<ptrdiff_t>(state.cr->config().shelley_security_param))
                                << "intersection query precedes the permitted anchor" << state.context;
                        } else {
                            expect(state.cr->num_blocks() <= state.cr->config().shelley_security_param)
                                << "origin offered for an immutable chain" << state.context;
                        }
                    }
                    intersection_info_t result { {}, state.peer_points.back(), false };
                    for (const auto &p: points) {
                        if (!p || std::ranges::any_of(state.peer_points, [&](const auto &candidate) {
                                return static_cast<point2>(candidate) == *p;
                            })) {
                            result.isect = p;
                            result.found = true;
                            break;
                        }
                    }
                    handler(find_response { _addr, result });
                }
                void _fetch_blocks_impl(const point2 &from, const point2 &to, const block_handler &handler) override
                {
                    const auto request = state.fetches++;
                    const size_t first = request == 0 ? 0 : 3;
                    const size_t last = request == 0 ? 2 : 3;
                    expect(request < 2 && !state.fork.empty()) << state.context << fatal;
                    expect_equal(from.hash, state.fork[first]->blk->hash(), state.context);
                    expect_equal(to.hash, state.fork[last]->blk->hash(), state.context);
                    pending = [this, handler, first, last] {
                        for (size_t i = first; i <= last; ++i) {
                            expect(handler(msg_block_t { uint8_vector { state.fork[i]->blk.raw() } })) << state.context << fatal;
                            if (state.scenario == rollback_case::interrupted || state.scenario == rollback_case::stopped
                                    || state.scenario == rollback_case::commit_requested) {
                                if (state.scenario == rollback_case::stopped)
                                    state.stop.request_stop();
                                if (state.scenario == rollback_case::commit_requested) {
                                    state.cr->request_commit();
                                    expect(!handler(msg_block_t { uint8_vector { state.fork[1]->blk.raw() } })) << state.context;
                                } else {
                                    handler(std::make_exception_ptr(network::transient_error{"interrupted replacement fetch"}));
                                }
                                break;
                            }
                        }
                    };
                }
                void _process_impl(scheduler *, asio::worker *) override
                {
                    if (auto deliver = std::exchange(pending, {}))
                        deliver();
                }
            };
            struct rollback_manager: client_manager {
                follow_state &state;
                explicit rollback_manager(follow_state &s): state { s } {}
                std::unique_ptr<client> _connect_impl(const network::address &, const version_config_t &,
                    const cardano::config &, const asio::worker_ptr &) override
                {
                    if (++state.connections > 1) {
                        expect(state.cr->tip() == state.selected) << state.context;
                        state.stop.request_stop();
                        throw error{ "rollback test reconnect" };
                    }
                    return std::make_unique<rollback_client>(state);
                }
            } manager { state };
            file_remover remover;
            optional_point expected;
            {
                chunk_registry cr { dir.path(), chunk_registry_settings_t {
                    .validate_vrf=false, .continuous=true, .ccfg=cfg, .fr=remover, .validation=validation } };
                seed_registry(cr);
                state.selected = cr.tip();
                state.cr = &cr;
                p2p::syncer syncer { cr, ps, manager };
                const auto publish = [&](const auto &) {
                    if (cr.tip() != state.selected) {
                        expect(scenario == rollback_case::replacement) << context << fatal;
                        const auto index = state.commits ? 3 : 2;
                        expect_equal(cr.tip()->hash, fork[index]->blk->hash(), context);
                        state.selected = cr.tip();
                        ++state.commits;
                        state.peer_points.clear();
                        for (auto it = cr.cbegin(); it != cr.cend(); ++it)
                            state.peer_points.emplace_back(static_cast<point3>(it->point()));
                    }
                };
                const auto follow = [&] {
                    syncer.follow(state.stop.get_token(), publish, network::address{"mock", "0"}, {},
                        std::chrono::seconds { 600 }, validation);
                };
                const bool orderly_stop = scenario == rollback_case::initial_boundary
                    || scenario == rollback_case::short_chain_origin || scenario == rollback_case::stopped;
                if (orderly_stop)
                    expect(nothrow(follow)) << context;
                else
                    expect(throws(follow)) << context;
                const bool advanced = scenario == rollback_case::replacement;
                expect_equal(state.commits, advanced ? 2 : 0, context);
                expect_equal(cr.num_blocks(), advanced ? 11 : 9, context);
                expect_equal(cr.valid_end_offset(), cr.num_bytes(), context);
                expect_equal(state.fetches, advanced ? 2 : replacement_attempt ? 1 : 0, context);
                const size_t expected_updates = scenario == rollback_case::buffered ? 7
                    : scenario == rollback_case::replacement ? 4
                    : scenario == rollback_case::initial_deep ? 0
                    : scenario == rollback_case::initial_boundary || scenario == rollback_case::origin
                        || scenario == rollback_case::unknown ? 1 : 2;
                expect_equal(state.updates, expected_updates, context);
                expect(!state.queries.empty()) << context;
                expect_equal(state.connections, 1, context);
                if (!advanced)
                    expect_equal(cr.tip()->hash, original_tip.hash, context);
                expected = cr.tip();
            }
            if (replacement_attempt) {
                chunk_registry reopened { dir.path(), chunk_registry_settings_t {
                    .validate_vrf=false, .continuous=true, .ccfg=cfg, .fr=remover, .validation=validation } };
                expect(reopened.tip() == expected) << context;
                expect_equal(reopened.valid_end_offset(), reopened.num_bytes(), context);
            }
        };
        const auto check_follow_rollback = [&](const rollback_case scenario,
            const std::source_location &loc=std::source_location::current()) {
            for (const auto validation: { validator::validation_mode::full, validator::validation_mode::turbo })
                check_follow_rollback_mode(scenario, validation, loc);
        };
        "following bounds initial intersection queries by k blocks"_test = [&] {
            check_follow_rollback(rollback_case::initial_deep);
            check_follow_rollback(rollback_case::initial_boundary);
        };
        "following rejects origin and unknown rollback points"_test = [&] {
            check_follow_rollback(rollback_case::origin);
            check_follow_rollback(rollback_case::unknown);
        };
        "following permits origin on a populated chain with at most k blocks"_test = [&] {
            check_follow_rollback(rollback_case::short_chain_origin);
        };
        "following accepts the exact boundary then rejects a repeated deeper rollback"_test = [&] {
            check_follow_rollback(rollback_case::repeated);
        };
        "buffered rollback rejects discarded headers without reconnecting"_test = [&] {
            check_follow_rollback(rollback_case::buffered);
        };
        "following advances the rollback boundary across chunks after replacement and extension"_test = [&] {
            check_follow_rollback(rollback_case::replacement);
        };
        "interrupted denser replacements cannot shorten the selected chain through restart"_test = [&] {
            check_follow_rollback(rollback_case::interrupted);
            check_follow_rollback(rollback_case::stopped);
            check_follow_rollback(rollback_case::commit_requested);
        };
        enum class fetch_interruption { shutdown, timeout, incomplete_response, commit };
        const auto check_fetch_recovery = [&](const bool continuous, const fetch_interruption interruption,
            const std::source_location &loc=std::source_location::current()) {
            const file::tmp_directory dir { "sync-p2p-fetch-recovery" };
            struct fetch_state {
                const mock_chain &chain;
                const fetch_interruption interruption;
                const std::string context;
                std::stop_source stop {};
                std::vector<point2> requests {};
                chunk_registry *registry = nullptr;
            } state { good_chain, interruption, fmt::format("check_fetch_recovery called from {}", loc) };
            struct recovering_client: cardano_client_mock {
                fetch_state &state;
                std::function<void()> pending;

                explicit recovering_client(fetch_state &s)
                    : cardano_client_mock { network::address { "mock", "0" }, s.chain.data, 0.0 }, state { s } {}

                chain_update next_header_sync(std::stop_token, const std::function<void()> &) override
                {
                    const auto &first = state.chain.blocks.at(state.registry->num_blocks())->blk;
                    const auto &last = state.chain.blocks.back()->blk;
                    // A distant tip selects the bulk import path.
                    return { false, point2 { first->slot(), first->hash() },
                        point3 { point2 { last->slot(), last->hash() }, 300 } };
                }

                void _fetch_blocks_impl(const point2 &from, const point2 &to, const block_handler &handler) override
                {
                    if (state.interruption == fetch_interruption::commit && state.requests.size() == 1) {
                        expect_equal(state.registry->num_blocks(), 2, state.context);
                        expect_equal(state.registry->valid_end_offset(), state.registry->num_bytes(), state.context);
                    }
                    state.requests.emplace_back(from);
                    pending = [this, from, to, handler] {
                        const auto &chain = state.chain;
                        const auto first = std::ranges::find_if(chain.blocks, [&](const auto &b) {
                            return b->blk->hash() == from.hash;
                        });
                        expect(first != chain.blocks.end()) << state.context << fatal;
                        size_t sent = 0;
                        for (auto it = first; it != chain.blocks.end(); ++it) {
                            const auto &b = (*it)->blk;
                            expect(handler(msg_block_t { uint8_vector { b.raw() } })) << state.context << fatal;
                            if (++sent == 2 && state.requests.size() == 1) {
                                if (state.interruption == fetch_interruption::shutdown) {
                                    state.stop.request_stop();
                                    expect(!handler(msg_block_t { uint8_vector { chain.blocks.at(2)->blk.raw() } })) << state.context;
                                    handler(std::make_exception_ptr(error{"network operation stopped"}));
                                } else if (state.interruption == fetch_interruption::timeout) {
                                    handler(std::make_exception_ptr(network::transient_error{"injected network timeout"}));
                                } else if (state.interruption == fetch_interruption::commit) {
                                    state.registry->request_commit();
                                    expect(!handler(msg_block_t { uint8_vector { chain.blocks.at(2)->blk.raw() } })) << state.context;
                                }
                                break;
                            }
                            if (b->hash() == to.hash)
                                break;
                        }
                    };
                }

                void _process_impl(scheduler *, asio::worker *) override
                {
                    if (auto deliver = std::exchange(pending, {}))
                        deliver();
                }
            };
            struct recovering_manager: client_manager {
                fetch_state &state;
                chunk_registry &cr;
                size_t connections = 0;

                recovering_manager(fetch_state &s, chunk_registry &registry): state { s }, cr { registry } {}

                std::unique_ptr<client> _connect_impl(const network::address &, const version_config_t &,
                    const cardano::config &, const asio::worker_ptr &) override
                {
                    if (++connections > 2) {
                        state.stop.request_stop();
                        throw error{"unexpected extra reconnect: {}", state.context};
                    }
                    if (connections == 2) {
                        expect(!state.stop.stop_requested()) << state.context;
                        expect_equal(cr.num_blocks(), state.interruption == fetch_interruption::timeout ? 2 : 0, state.context);
                        expect_equal(cr.valid_end_offset(), cr.num_bytes(), state.context);
                    }
                    return std::make_unique<recovering_client>(state);
                }
            };
            const auto expected_blocks = interruption == fetch_interruption::shutdown ? 2 : good_chain.blocks.size();
            const size_t expected_requests = interruption == fetch_interruption::shutdown ? 1 : 2;
            file_remover remover;
            {
                chunk_registry cr { dir.path(), chunk_registry_settings_t { .continuous=continuous, .ccfg=ccfg, .fr=remover } };
                state.registry = &cr;
                recovering_manager manager { state, cr };
                p2p::syncer syncer { cr, ps, manager };
                const network::address addr { "mock", "0" };
                if (continuous) {
                    syncer.follow(state.stop.get_token(), [&](const auto &) {
                        expect_equal(cr.valid_end_offset(), cr.num_bytes(), state.context);
                        if (cr.num_blocks() == expected_blocks)
                            state.stop.request_stop();
                    }, addr);
                } else {
                    expect(syncer.sync(syncer.find_peer(addr), {}, validation_mode_t::full)) << state.context;
                }
                expect_equal(manager.connections, continuous && interruption != fetch_interruption::commit ? expected_requests : 1, state.context);
                expect_equal(cr.num_blocks(), expected_blocks, state.context);
                expect_equal(cr.valid_end_offset(), cr.num_bytes(), state.context);
                expect(!cr.validator().snapshots().empty()) << state.context << fatal;
                expect_equal(cr.validator().snapshots().rbegin()->end_offset, cr.num_bytes(), state.context);
                expect(state.requests.size() == expected_requests) << state.context << fatal;
                expect_equal(state.requests.front().hash, good_chain.blocks.front()->blk->hash(), state.context);
                if (expected_requests == 2)
                    expect_equal(state.requests.back().hash,
                        good_chain.blocks.at(interruption == fetch_interruption::incomplete_response ? 0 : 2)->blk->hash(), state.context);
            }
            chunk_registry restored { dir.path(), chunk_registry_settings_t { .continuous=continuous, .ccfg=ccfg, .fr=remover } };
            expect_equal(restored.num_blocks(), expected_blocks, state.context);
            expect_equal(restored.valid_end_offset(), restored.num_bytes(), state.context);
        };
        "stopping a bulk fetch commits accepted blocks and restores its snapshot"_test = [&] {
            check_fetch_recovery(true, fetch_interruption::shutdown);
        };
        "batch sync retains progress after a network error"_test = [&] {
            check_fetch_recovery(false, fetch_interruption::timeout);
        };
        "continuous sync reconnects from progress retained after a network error"_test = [&] {
            check_fetch_recovery(true, fetch_interruption::timeout);
        };
        "continuous sync rejects an incomplete successful response"_test = [&] {
            check_fetch_recovery(true, fetch_interruption::incomplete_response);
        };
        "batch sync resumes from blocks persisted after a commit request"_test = [&] {
            check_fetch_recovery(false, fetch_interruption::commit);
        };
        "continuous bulk sync resumes from blocks committed at a commit interruption"_test = [&] {
            check_fetch_recovery(true, fetch_interruption::commit);
        };
        "explicit peer is preserved through connection and follow failures"_test = [&] {
            const file::tmp_directory dir { "sync-p2p-fixed-follow-peer" };
            const network::address addr { "fixed-peer", "3001" };
            std::stop_source stop;
            struct failing_client: cardano_client_mock {
                using cardano_client_mock::cardano_client_mock;
                chain_update next_header_sync(std::stop_token, const std::function<void()> &) override
                {
                    throw network::transient_error{ "injected follow failure" };
                }
            };
            struct reconnecting_manager: client_manager {
                const mock_chain &chain;
                std::stop_source &stop;
                std::vector<network::address> addresses;
                reconnecting_manager(const mock_chain &c, std::stop_source &s): chain { c }, stop { s } {}
                std::unique_ptr<client> _connect_impl(const network::address &addr, const version_config_t &,
                    const cardano::config &, const asio::worker_ptr &) override
                {
                    addresses.emplace_back(addr);
                    if (addresses.size() >= 3)
                        stop.request_stop();
                    if (addresses.size() != 2)
                        throw network::transient_error{ "injected connection failure" };
                    return std::make_unique<failing_client>(addr, chain.data, 0.0);
                }
            } manager { good_chain, stop };
            unexpected_peer_selection peers;
            file_remover remover;
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .continuous=true, .ccfg=ccfg, .fr=remover } };
            p2p::syncer syncer { cr, peers, manager };
            syncer.follow(stop.get_token(), [](const auto &) {}, addr);
            expect_equal(manager.addresses.size(), 3);
            for (const auto &connected: manager.addresses) expect_equal(connected, addr);
            expect_equal(peers.selections, 0);
        };
        "batch sync retries only transient failures and propagates exhaustion"_test = [&] {
            const file::tmp_directory dir { "sync-p2p-fixed-batch-peer" };
            const network::address addr { "fixed-peer", "3001" };
            size_t attempts = 0;
            std::exception_ptr failure;
            struct failing_client: cardano_client_mock {
                size_t &attempts;
                const std::exception_ptr &failure;
                failing_client(const network::address &addr, buffer data, size_t &n, const std::exception_ptr &ex)
                    : cardano_client_mock { addr, data, 0.0 }, attempts { n }, failure { ex } {}
                void _fetch_blocks_impl(const point2 &, const point2 &, const block_handler &handler) override
                {
                    ++attempts;
                    handler(failure);
                }
            };
            struct failing_manager: client_manager {
                const mock_chain &chain;
                size_t &attempts;
                const std::exception_ptr &failure;
                std::vector<network::address> addresses;
                failing_manager(const mock_chain &c, size_t &n, const std::exception_ptr &ex)
                    : chain { c }, attempts { n }, failure { ex } {}
                std::unique_ptr<client> _connect_impl(const network::address &addr, const version_config_t &,
                    const cardano::config &, const asio::worker_ptr &) override
                {
                    addresses.emplace_back(addr);
                    return std::make_unique<failing_client>(addr, chain.data, attempts, failure);
                }
            } manager { good_chain, attempts, failure };
            unexpected_peer_selection peers;
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=ccfg } };
            p2p::syncer syncer { cr, peers, manager };
            for (const bool transient: { false, true }) {
                attempts = 0;
                const auto sync = [&] { syncer.sync(syncer.find_peer(addr), {}, validation_mode_t::none); };
                if (transient) {
                    failure = std::make_exception_ptr(network::transient_error{"injected block fetch failure"});
                    expect(throws<network::transient_error>(sync));
                } else {
                    failure = std::make_exception_ptr(std::logic_error{"injected local failure"});
                    expect(throws<std::logic_error>(sync));
                }
                expect_equal(attempts, transient ? 3 : 1);
                expect(!cr.tx());
            }
            expect_equal(manager.addresses.size(), 2);
            expect_equal(manager.addresses.front(), addr);
            expect_equal(peers.selections, 0);
            expect_equal(cr.num_blocks(), 0);
        };
        "no work"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry_settings_t { .ccfg=ccfg } };
            p2p::syncer s { cr, ps, ccm };
            expect(s.sync(s.find_peer()));
            expect_equal(cr.num_blocks(), 9);
            expect(!s.sync(s.find_peer()));
            expect_equal(cr.num_blocks(), 9);
        };
        "empty upstream has no batch work and preserves the local chain"_test = [&] {
            const file::tmp_directory dir { "sync-p2p-empty-upstream" };
            chunk_registry cr { dir.path(), chunk_registry_settings_t { .ccfg=ccfg } };
            cardano_client_manager_mock empty_manager { uint8_vector {} };
            p2p::syncer syncer { cr, ps, empty_manager };
            const network::address addr { "mock", "0" };
            const auto [headers, tip] = empty_manager.connect(addr)->fetch_headers_sync(optional_point {}, 1, true);
            expect(headers.empty());
            expect(!tip); // A received origin tip is not a missing response.
            auto peer = syncer.find_peer(addr);
            expect(!peer->tip());
            expect(!syncer.sync(peer));
            expect(!cr.tip());
            cr.accept_anything_or_throw({}, *good_chain.tip, [&] {
                cr.add_buffer(0, uint8_vector { good_chain.blocks.front()->blk.raw() });
            });
            const auto before = cr.tip();
            peer = syncer.find_peer(addr);
            expect(!syncer.sync(peer));
            expect_equal(cr.tip(), before);
        };
        "invalid blocks fail while preserving the accepted prefix"_test = [&] {
            mock_chain_config test_mock_cfg { mock_cfg };
            test_mock_cfg.failure_height = 7;
            const auto chain = gen_chain(test_mock_cfg);
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry_settings_t { .ccfg=ccfg } };
            cardano_client_manager_mock test_ccm { chain.data };
            p2p::syncer s { cr, ps, test_ccm};
            expect(throws([&] { s.sync(s.find_peer()); }));
            expect_equal(cr.num_blocks(), 7);
            expect(throws([&] { s.sync(s.find_peer()); }));
            expect_equal(cr.num_blocks(), 7);
        };
        "max_slot"_test = [&] {
            constexpr uint64_t max_slot = 100;
            const auto expected_end = std::ranges::find_if(good_chain.blocks, [&](const auto &blk) {
                return blk->blk->slot() > max_slot;
            });
            expect(expected_end != good_chain.blocks.begin());
            expect(expected_end != good_chain.blocks.end());
            if (expected_end == good_chain.blocks.begin() || expected_end == good_chain.blocks.end())
                return;
            const auto expected_num_blocks = static_cast<size_t>(std::distance(good_chain.blocks.begin(), expected_end));
            const auto expected_max_slot = (*std::prev(expected_end))->blk->slot();

            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir, chunk_registry_settings_t { .ccfg=ccfg } };
            p2p::syncer s { cr, ps, ccm };
            expect(s.sync(s.find_peer(), max_slot));
            expect_equal(cr.num_blocks(), expected_num_blocks);
            expect_equal(cr.max_slot(), expected_max_slot);
        };
        "multi chunk"_test = [&] {
            std::filesystem::remove_all(data_dir);
            const auto seed = crypto::blake2b::digest<crypto::ed25519::seed>(std::string_view { "1" });
            block_producer block { crypto::ed25519::create_sk_from_seed(seed), seed, vrf03_create_sk_from_seed(seed) };
            block.prev_hash = ccfg.byron_genesis_hash;
            block.vrf_nonce = ccfg.shelley_genesis_hash;
            uint8_vector data;
            for (const uint64_t slot: { 0, 21'600, 50'000, 80'000 }) {
                block.slot = slot;
                parsed_block parsed { block.cbor(), ccfg };
                data << *parsed.data;
                block.prev_hash = parsed.blk->hash();
                ++block.height;
            }
            chunk_registry cr { data_dir, chunk_registry_settings_t { .ccfg=ccfg } };
            cardano_client_manager_mock ccm { data };
            sync::p2p::syncer s { cr, ps, ccm };
            s.sync(s.find_peer(), 50'000, validation_mode_t::none);
            expect(cr.max_slot() == 50'000_ull);
            expect_equal(cr.chunks().size(), 3);
            s.sync(s.find_peer(), {}, validation_mode_t::none);
            expect(cr.max_slot() == 80'000_ull);
            expect_equal(cr.chunks().size(), 4);
            expect_equal(cr.num_blocks(), 4);
        };
        "find_peer"_test = [&] {
            std::filesystem::remove_all(data_dir);
            chunk_registry cr { data_dir };
            sync::p2p::syncer s { cr, ps, ccm };
            const auto peer_ptr = s.find_peer();
            auto &peer = dynamic_cast<sync::p2p::peer_info &>(*peer_ptr);
            expect(fatal(peer.tip().has_value()));
            expect(peer.tip()->slot > 0);
            expect(peer.tip().height > 0);
            expect(!peer.intersection());
        };
    };
};
