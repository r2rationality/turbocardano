/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE
 *
 * This code uses a binary dump of the ledger state produced by Cardano Node.
 * Moreover, at the moment it relies on the relative ordering of the data structures.
 * So, it can be very brittle.
 * The code has been tested with the ledger snapshot at end of epoch 432 (last slot 106012751). */

#include <algorithm>
#include <random>
#include <ranges>
#include <turbo/cli/common.hpp>
#include <turbo/chunk-registry.hpp>
#include <turbo/history.hpp>

namespace turbo::cli::test_stake_balances {
    using namespace turbo::cardano;
    namespace rv = std::ranges::views;

    struct cmd: command {
        void configure(config &cmd) const override
        {
            cmd.name = "test-stake-balances";
            cmd.desc = "compare a random sample of manually reconstructed stake balances with the ones recorded in a Cardano Node's snapshot";
            cmd.args.expect({ "<data-dir>", "<ledger-snapshot>" });
            cmd.opts.try_emplace("sample-ratio-min", "the lower bound of the random interval used to decide if a given stake to be included in the analysis", "0.0");
            cmd.opts.try_emplace("sample-ratio-max", "the upper bound of the random interval used to decide if a given stake to be included in the analysis", "0.001");
        }

        void run(const arguments &args, const options &opts) const override
        {
            const auto &data_dir = args.at(0);
            const auto &ledger_path = args.at(1);
            const double min_pct = std::stod(opts.at("sample-ratio-min").value());
            const double max_pct = std::stod(opts.at("sample-ratio-max").value());
            timer t { "complete test", logger::level::info };
            auto [ledger_stake_dist, ledger_slot] = parse_ledger_snapshot(ledger_path);
            chunk_registry cr { data_dir, chunk_registry::mode::index };
            reconstructor r { cr };
            if (ledger_slot != r.last_slot())
                throw error(fmt::format("ledger last slot: {} does not match raw data last slot: {}", ledger_slot, r.last_slot()));
            verify_sample(r, ledger_stake_dist, min_pct, max_pct);
        }
    private:
        struct test_error {
            stake_ident id {};
            uint64_t balance_expected = 0;
            uint64_t balance_actual = 0;
            int64_t diff = 0;
            size_t tx_count = 0;

            double diff_rel() const
            {
                return std::abs(static_cast<double>(diff)) / balance_expected;
            }
        };

        struct stake_item {
            stake_ident id {};
            uint64_t balance {};
        };
        using stake_list = std::vector<stake_item>;

        static void verify_sample(reconstructor &r, const stake_list &stake_dist, double min_pct, double max_pct)
        {
            timer t { "verify sample" };
            std::seed_seq seed { 0, 1, 2, 3, 4, 5 };
            std::default_random_engine rnd { seed };
            std::uniform_real_distribution<double> dist { 0.0, 1.0 };
            double cum_rel_diff = 0;
            std::vector<double> timings {};
            std::vector<test_error> errors {};
            size_t num_except = 0;

            auto sample_pct = max_pct - min_pct;
            logger::info("distribution size: {} sample size: {:0.2f}% estimated number of ids: {}",
                stake_dist.size(), sample_pct * 100, static_cast<size_t>(stake_dist.size() * sample_pct));
            size_t sample_no = 0;
            for (const auto &item: stake_dist) {
                if (min_pct > 0.0 || max_pct < 1.0) {
                    auto pass = dist(rnd);
                    if (pass < min_pct || pass >= max_pct)
                        continue;
                }
                sample_no++;
                try {
                    timer rt { fmt::format("verify sample {}: {}", sample_no, item.id), logger::level::info };
                    history h = r.find_history(item.id);
                    uint64_t bal = h.utxo_balance();
                    if (bal != item.balance) {
                        test_error te { item.id, item.balance, bal, (int64_t)te.balance_actual - (int64_t)te.balance_expected, h.transactions.size() };
                        logger::warn("{} the computed balance differs by {} ADA", te.id, cardano::balance_change { te.diff });
                        cum_rel_diff += te.diff_rel();
                        errors.emplace_back(std::move(te));
                    }
                    timings.emplace_back(rt.stop());
                } catch (std::exception &ex) {
                    ++num_except;
                    logger::error("sample: {} stake_id: {} message: {}", sample_no, item.id, ex.what());
                }
            }
            std::ostringstream log_msg {};
            log_msg << "correct: " << (sample_no - errors.size() - num_except) << "/" << sample_no;
            if (errors.size() > 0) {
                log_msg << ", among the incorrect the mean rel. diff is " << cum_rel_diff * 100 / errors.size() << "%";
            }
            if (num_except > 0) {
                log_msg << ", failed to reconstruct: " << num_except << " stake addresses";
            }
            log_msg << '\n';
            if (sample_no > 0) {
                log_msg << "reconstruction time" << std::fixed << std::setprecision(6)
                    << " mean: " << accumulate(timings.begin(), timings.end(), (double)0.0) / timings.size() << " secs"
                    << " max: " << *max_element(timings.begin(), timings.end()) << " secs"
                    << " min: " << *min_element(timings.begin(), timings.end()) << " secs"
                    << '\n';
            }
            sort(errors.begin(), errors.end(), [](const auto &a, const auto &b) { return a.tx_count < b.tx_count; });
            for (const auto &te: errors | rv::take(10)) {
                log_msg
                    << fmt::format("{}", te.id)
                    << " tx count: " << te.tx_count
                    << " differs by " << te.diff / 1'000'000u << "." << std::abs(te.diff) % 1'000'000u << " ADA"
                    << " or " << te.diff_rel() * 100 << "%"
                    << '\n';
            }
            logger::info(log_msg.str());
        }

        static std::pair<stake_list, uint64_t> parse_ledger_snapshot(const std::string &ledger_path)
        {
            timer t { "parsing ledger state" };
            auto buf = file::read(ledger_path);
            logger::debug("loaded {} size: {} MB", ledger_path, buf.size() / 1'000'000u);
            auto v = cbor::zero2::parse(buf);
            static std::array<size_t, 7> last_slot_path { 1, 5, 1, 1, 0, 0, 0 };
            auto latest_slot = extract(v.get(), last_slot_path, 0).uint();
            stake_list latest_stake_dist {};
            static std::array<size_t, 10> stake_latest_path { 1, 5, 1, 1, 1, 3, 1, 1, 4, 0 };
            {
                auto &mv = extract(v.get(), stake_latest_path, 0);
                for (auto &it = mv.map(); !it.done(); ) {
                    auto &key_v = it.read_key();
                    auto &key_it = key_v.array();
                    const auto id_typ = key_it.read().uint();
                    const auto id_hash = key_it.read().bytes();
                    const auto coin = it.read_val(std::move(key_v)).uint();
                    latest_stake_dist.emplace_back(stake_ident { id_hash, id_typ == 1 }, coin);
                }
            }
            return std::make_pair(std::move(latest_stake_dist), latest_slot);
        }
    };
    static auto instance = command::reg(std::make_shared<cmd>());
}
