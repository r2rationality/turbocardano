/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <turbo/cardano/common/cert.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/babbage/cbor/encode/transaction-output.hpp>
#include <turbo/cardano/conway/block.hpp>
#include <turbo/cardano/dijkstra/block.hpp>
#include <turbo/cbor/encoder.hpp>
#include <turbo/cli/common.hpp>
#include <turbo/json.hpp>
#include <turbo/plutus/types-core.hpp>

namespace turbo::cli::test_cbor_dataset {
    namespace fs = std::filesystem;

    using codec_func = std::string (*)(buffer, bool, const std::optional<uint8_vector> &);
    using codec_map = std::unordered_map<std::string, codec_func>;

    template<typename T>
    struct cbor_codec {
        static buffer prepare(buffer source) { return source; }
        static buffer extract(buffer encoded) { return encoded; }
        static T decode(cbor::zero2::value &value) { return T::from_cbor(value); }
        static void encode(cardano::era_encoder &enc, const T &value) { value.to_cbor(enc); }
    };

    template<cardano::era_t ERA, typename T>
    struct block_codec: cbor_codec<T> {
        static T decode(cbor::zero2::value &value) {
            return T { static_cast<uint64_t>(ERA) + 1, 0, 0, value, cardano::config::get() };
        }
    };

    template<cardano::era_t ERA, typename T>
    struct header_codec: cbor_codec<T> {
        static T decode(cbor::zero2::value &value) {
            return T { static_cast<uint64_t>(ERA) + 1, value, cardano::config::get() };
        }
    };

    template<cardano::era_t ERA, typename T>
    struct header_body_codec: header_codec<ERA, T> {
        static uint8_vector prepare(buffer source) {
            cbor::encoder enc {};
            enc.array(2).raw_cbor(source).bytes(cardano::cardano_kes_signature_data {});
            return std::move(enc.cbor());
        }
        static buffer extract(buffer encoded) {
            cbor::zero2::decoder dec { encoded };
            return dec.read().array().read().data_raw();
        }
    };

    struct plutus_data_codec: cbor_codec<plutus::data> {
        plutus::allocator alloc {};

        plutus::data decode(cbor::zero2::value &value) {
            return plutus::data::from_cbor(alloc, value.data_raw());
        }
    };

    struct datum_option_codec: cbor_codec<cardano::datum_option_t> {
        static void encode(cardano::era_encoder &enc, const cardano::datum_option_t &value) {
            cardano::babbage::detail::datum_option_to_cbor_semantic(enc, value);
        }
    };

    struct dijkstra_proposal_codec: cbor_codec<cardano::dijkstra::proposal_procedures_t> {
        static uint8_vector prepare(buffer source) {
            cbor::encoder enc {};
            enc.tag(258).array(1).raw_cbor(source);
            return std::move(enc.cbor());
        }
        static buffer extract(buffer encoded) {
            cbor::zero2::decoder dec { encoded };
            return dec.read().tag().read().array().read().data_raw();
        }
    };

    template<uint64_t FIELD>
    struct dijkstra_body_field_codec: cbor_codec<cardano::dijkstra::transaction_body_t> {
        static uint8_vector prepare(buffer source) {
            cbor::encoder enc {};
            enc.map(4)
                .uint(0).array(0)
                .uint(1).array(0)
                .uint(2).uint(0)
                .uint(FIELD).raw_cbor(source);
            return std::move(enc.cbor());
        }
        static buffer extract(buffer encoded) {
            cbor::zero2::decoder dec { encoded };
            auto &it = dec.read().map();
            while (!it.done()) {
                auto &key = it.read_key();
                const auto field = key.uint();
                auto &value = it.read_val(std::move(key));
                if (field == FIELD)
                    return value.data_raw();
            }
            throw error(fmt::format("reserialized Dijkstra transaction body omitted field {}", FIELD));
        }
    };

    void encode_dijkstra_test_header(cbor::encoder &enc, const bool contains_leios_certificate) {
        enc.array(2).array(12)
            .uint(0)
            .uint(0)
            .s_null()
            .bytes(cardano::vkey {})
            .bytes(cardano::vrf_vkey {})
            .array(2).bytes(cardano::vrf_result {}).bytes(cardano::vrf_proof {})
            .uint(0)
            .bytes(cardano::block_hash {})
            .array(4)
                .bytes(cardano::kes_vkey {})
                .uint(0)
                .uint(0)
                .bytes(cardano::signature {})
            .array(2).uint(13).uint(0)
            .boolean(contains_leios_certificate)
            .s_null()
            .bytes(cardano::cardano_kes_signature_data {});
    }

    struct dijkstra_block_body_codec: block_codec<cardano::era_t::dijkstra, cardano::dijkstra::block> {
        static uint8_vector prepare(buffer source) {
            cbor::zero2::decoder dec { source };
            auto &body = dec.read().array();
            static_cast<void>(body.read().data_raw());
            static_cast<void>(body.read().data_raw());
            const bool contains_leios_certificate = !body.read().is_null();
            cbor::encoder enc {};
            enc.array(2);
            encode_dijkstra_test_header(enc, contains_leios_certificate);
            enc.raw_cbor(source);
            return std::move(enc.cbor());
        }
        static buffer extract(buffer encoded) {
            cbor::zero2::decoder dec { encoded };
            auto &it = dec.read().array();
            static_cast<void>(it.read().data_raw());
            return it.read().data_raw();
        }
    };

    template<cardano::era_t ERA, typename Codec>
    std::string check_sample(buffer source, bool decode_only, const std::optional<uint8_vector> &expected) {
        try {
            // Keep wrapper bytes and any allocator alive through encoding.
            Codec codec {};
            const auto prepared = codec.prepare(source);
            cbor::zero2::decoder dec { prepared };
            const auto value = codec.decode(dec.read());
            if (!dec.done()) [[unlikely]]
                throw error("the sample contains more than one top-level CBOR value");
            if (!expected)
                return "succeeded when expected to fail";
            if (!decode_only) {
                cardano::era_encoder enc { ERA };
                codec.encode(enc, value);
                if (codec.extract(enc.cbor()) != buffer { *expected }) [[unlikely]]
                    return "reserialization differs from the expected output";
            }
        } catch (const std::exception &ex) {
            if (expected)
                return ex.what();
        }
        return {};
    }

    const codec_map &codecs_for(const fs::path &sample_dir) {
        static const codec_map conway_codecs = {
            {"auxiliary_data", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::auxiliary_data_t>>},
            {"block", check_sample<cardano::era_t::conway, block_codec<cardano::era_t::conway, cardano::conway::block>>},
            {"certificate", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::certificate_t>>},
            {"cost_models", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::cost_models_t>>},
            {"credential", check_sample<cardano::era_t::conway, cbor_codec<cardano::credential_t>>},
            {"datum_option", check_sample<cardano::era_t::conway, datum_option_codec>},
            {"drep", check_sample<cardano::era_t::conway, cbor_codec<cardano::drep_t>>},
            {"gov_action", check_sample<cardano::era_t::conway, cbor_codec<cardano::gov_action_t>>},
            {"header", check_sample<cardano::era_t::conway, header_codec<cardano::era_t::conway, cardano::conway::block_header>>},
            {"header_body", check_sample<cardano::era_t::conway, header_body_codec<cardano::era_t::conway, cardano::conway::block_header>>},
            {"mint", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::mint_t>>},
            {"native_script", check_sample<cardano::era_t::conway, cbor_codec<cardano::allegra::native_script_t>>},
            {"plutus_data", check_sample<cardano::era_t::conway, plutus_data_codec>},
            {"proposal_procedure", check_sample<cardano::era_t::conway, cbor_codec<cardano::proposal_procedure_t>>},
            {"protocol_param_update", check_sample<cardano::era_t::conway, cbor_codec<cardano::param_update_t>>},
            {"redeemer", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::redeemer_t>>},
            {"redeemers", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::redeemers_t>>},
            {"relay", check_sample<cardano::era_t::conway, cbor_codec<cardano::relay_info>>},
            {"script", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::script_t>>},
            {"transaction", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::transaction_t>>},
            {"transaction_body", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::transaction_body_t>>},
            {"transaction_input", check_sample<cardano::era_t::conway, cbor_codec<cardano::shelley::transaction_input_t>>},
            {"transaction_output", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::transaction_output_t>>},
            {"transaction_witness_set", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::transaction_witness_set_t>>},
            {"value", check_sample<cardano::era_t::conway, cbor_codec<cardano::conway::value_t>>},
            {"voting_procedure", check_sample<cardano::era_t::conway, cbor_codec<cardano::voting_procedure_t>>}
        };
        static const codec_map dijkstra_codecs = {
            {"account_balance_interval", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::account_balance_interval_t>>},
            {"account_balance_intervals", check_sample<cardano::era_t::dijkstra, dijkstra_body_field_codec<26>>},
            {"auxiliary_data", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::auxiliary_data_t>>},
            {"block", check_sample<cardano::era_t::dijkstra, block_codec<cardano::era_t::dijkstra, cardano::dijkstra::block>>},
            {"block_body", check_sample<cardano::era_t::dijkstra, dijkstra_block_body_codec>},
            {"certificate", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::certificate_t>>},
            {"certificates", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::certificates_t>>},
            {"cost_models", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::conway::cost_models_t>>},
            {"credential", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::credential_t>>},
            {"datum_option", check_sample<cardano::era_t::dijkstra, datum_option_codec>},
            {"drep", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::drep_t>>},
            {"gov_action", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::governance_action_t>>},
            {"header", check_sample<cardano::era_t::dijkstra, header_codec<cardano::era_t::dijkstra, cardano::dijkstra::block_header>>},
            {"header_body", check_sample<cardano::era_t::dijkstra, header_body_codec<cardano::era_t::dijkstra, cardano::dijkstra::block_header>>},
            {"native_script", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::native_script_t>>},
            {"plutus_data", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::plutus_data_t>>},
            {"proposal_procedure", check_sample<cardano::era_t::dijkstra, dijkstra_proposal_codec>},
            {"proposal_procedures", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::proposal_procedures_t>>},
            {"protocol_param_update", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::protocol_param_update_t>>},
            {"redeemers", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::redeemers_t>>},
            {"relay", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::relay_info>>},
            {"script", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::script_t>>},
            {"sub_transaction_body", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::sub_transaction_body_t>>},
            {"transaction", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::transaction_t>>},
            {"transaction_body", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::transaction_body_t>>},
            {"transaction_input", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::shelley::transaction_input_t>>},
            {"transaction_output", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::transaction_output_t>>},
            {"transaction_witness_set", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::transaction_witness_set_t>>},
            {"value", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::dijkstra::value_t>>},
            {"voting_procedure", check_sample<cardano::era_t::dijkstra, cbor_codec<cardano::voting_procedure_t>>},
            {"voting_procedures", check_sample<cardano::era_t::dijkstra, dijkstra_body_field_codec<19>>}
        };

        auto dataset_path = sample_dir;
        if (!dataset_path.has_filename())
            dataset_path = dataset_path.parent_path();
        const auto dataset_name = dataset_path.filename().string();
        const auto separator = dataset_name.find('-');
        const auto era_name = dataset_name.substr(0, separator);
        if (era_name == "conway")
            return conway_codecs;
        if (era_name == "dijkstra")
            return dijkstra_codecs;
        throw error(fmt::format("can't determine a supported era from dataset directory {}", sample_dir));
    }

    struct cmd: command {
        void configure(config &cmd) const override {
            cmd.name = "test-cbor-dataset";
            cmd.desc = "evaluate era-specific CBOR codecs against a generated dataset";
            cmd.args.expect({ "<dataset-dir>", "[<type>...]" });
            cmd.opts.try_emplace("decode-only", "only deserialize samples; skip reserialization and expected-output comparison");
            cmd.opts.try_emplace("results-file", "write JSON results to the specified file");
            cmd.usage = "test-cbor-dataset [--decode-only] [--results-file=<file>] <dataset-dir> [<type>...]";
        }

        void run(const arguments &args, const options &opts) const override {
            const bool decode_only = opts.contains("decode-only");
            const auto results_file = opts.find("results-file");
            if (results_file != opts.end() && (!results_file->second || results_file->second->empty()))
                throw error("--results-file requires a non-empty filename");
            const fs::path requested_sample_dir { args.at(0) };
            if (!fs::is_directory(requested_sample_dir)) [[unlikely]]
                throw error(fmt::format("expected a dataset directory: {}", requested_sample_dir));
            const auto sample_dir = fs::canonical(requested_sample_dir);
            const auto &codecs = codecs_for(sample_dir);

            const auto types = args | std::views::drop(1);

            std::map<std::string, std::string> res{};
            for (const auto &e: fs::recursive_directory_iterator(sample_dir)) {
                if (!e.is_regular_file() || e.path().extension() != ".cbor")
                    continue;
                const auto path = e.path().string();
                const auto relative_path = e.path().lexically_relative(sample_dir);
                const auto relative_dir = relative_path.parent_path();
                auto part_it = relative_dir.begin();
                if (part_it == relative_dir.end()) [[unlikely]]
                    throw error(fmt::format("can't determine the type name from path {}", relative_path));
                const auto type_name = (part_it++)->string();
                if (part_it == relative_dir.end()) [[unlikely]]
                    throw error(fmt::format("can't determine the test name from path {}", relative_path));
                auto test_name = (part_it++)->string();
                for (; part_it != relative_dir.end(); ++part_it)
                    test_name += "-" + part_it->string();
                if (!types.empty() && std::find(types.begin(), types.end(), type_name) == types.end())
                    continue;
                if (test_name == "expected")
                    continue;

                auto &result = res[relative_path];
                const auto codec_it = codecs.find(type_name);
                if (codec_it == codecs.end()) {
                    result = "unsupported";
                    continue;
                }
                try {
                    const auto original = file::read(path);
                    std::optional<uint8_vector> expected {};
                    if (test_name == "valid") {
                        const auto expected_path = e.path().parent_path().parent_path() / "expected" / e.path().filename();
                        expected = file::read(expected_path.string());
                    }
                    result = codec_it->second(original, decode_only, expected);
                } catch (const std::exception &ex) {
                    result = ex.what();
                }
            }
            if (res.empty()) [[unlikely]]
                throw error(fmt::format("dataset contains no selected CBOR files: {}", sample_dir));
            if (results_file != opts.end()) {
                json::object results {};
                for (const auto &[path, message]: res)
                    results.emplace(path, message.empty() ? json::value { true } : json::value { message });
                json::save_pretty(*results_file->second, results);
            }
            const auto failed = std::ranges::count_if(res, [](const auto &it) {
                return !it.second.empty();
            });
            logger::log(failed ? logger::level::err : logger::level::info,
                "failed: {} out of: {} pass rate: {:0.3f}%",
                res.size(), failed, static_cast<double>(res.size() - failed) * 100 / res.size());
            {
                std::map<std::string, size_t> error_counts {};
                for (const auto &[path, err]: res) {
                    if (!err.empty())
                        ++error_counts[err];
                }
                std::vector<std::pair<std::string, size_t>> error_stats { error_counts.begin(), error_counts.end() };
                std::ranges::stable_sort(error_stats, std::ranges::greater {}, &decltype(error_stats)::value_type::second);
                for (const auto &[err, count]: error_stats)
                    logger::error("\t{}: {}", count, err);
            }
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}
