/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <filesystem>
#include <string>
#include <unordered_map>
#include <turbo/cardano/common/cert.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/babbage/cbor/encode/transaction-output.hpp>
#include <turbo/cardano/conway/block.hpp>
#include <turbo/cardano/dijkstra/block.hpp>
#include <turbo/cbor/encoder.hpp>
#include <turbo/cli/common.hpp>
#include <turbo/plutus/types-core.hpp>

namespace turbo::cli::test_cbor_dataset {
    namespace fs = std::filesystem;

    using decode_func = void (*)(buffer);
    using reserialize_func = uint8_vector (*)(buffer);

    struct codec {
        decode_func deserialize;
        reserialize_func reserialize;
    };

    using codec_map = std::unordered_map<std::string, codec>;

    enum class test_mode {
        deserialize,
        reserialize,
        expected
    };

    uint8_vector copy(const buffer data) {
        return { data.begin(), data.end() };
    }

    template<typename T>
    T decode_cbor_value(const buffer source) {
        cbor::zero2::decoder dec{source};
        T res = T::from_cbor(dec.read());
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
        return res;
    }

    template<typename T>
    void decode_cbor(const buffer source) {
        decode_cbor_value<T>(source);
    }

    template<cardano::era_t ERA, typename T>
    uint8_vector reserialize_cbor(const buffer source) {
        const auto value = decode_cbor_value<T>(source);
        cardano::era_encoder enc { ERA };
        value.to_cbor(enc);
        return std::move(enc.cbor());
    }

    template<cardano::era_t ERA, typename BLOCK>
    void decode_block(const buffer source) {
        cbor::zero2::decoder dec { source };
        static_cast<void>(BLOCK {
            static_cast<uint64_t>(ERA) + 1, 0, 0, dec.read(), cardano::config::get()
        });
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
    }

    template<cardano::era_t ERA, typename HEADER>
    void decode_header(const buffer raw) {
        cbor::zero2::decoder dec{raw};
        static_cast<void>(HEADER {
            static_cast<uint64_t>(ERA) + 1, dec.read(), cardano::config::get()
        });
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
    }

    template<cardano::era_t ERA, typename HEADER>
    void decode_header_body(const buffer source) {
        cbor::encoder enc {};
        enc.array(2).raw_cbor(source).bytes(cardano::cardano_kes_signature_data {});
        decode_header<ERA, HEADER>(enc.cbor());
    }

    template<cardano::era_t ERA, typename BLOCK>
    uint8_vector reserialize_block(const buffer source) {
        cbor::zero2::decoder dec { source };
        const BLOCK block {
            static_cast<uint64_t>(ERA) + 1, 0, 0, dec.read(), cardano::config::get()
        };
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
        cardano::era_encoder enc { ERA };
        block.to_cbor(enc);
        return std::move(enc.cbor());
    }

    template<cardano::era_t ERA, typename HEADER>
    uint8_vector reserialize_header(const buffer source) {
        cbor::zero2::decoder dec { source };
        const HEADER header {
            static_cast<uint64_t>(ERA) + 1, dec.read(), cardano::config::get()
        };
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
        cardano::era_encoder enc { ERA };
        header.to_cbor(enc);
        return std::move(enc.cbor());
    }

    template<cardano::era_t ERA, typename HEADER>
    uint8_vector reserialize_header_body(const buffer source) {
        cbor::encoder wrapper {};
        wrapper.array(2).raw_cbor(source).bytes(cardano::cardano_kes_signature_data {});
        const auto header = reserialize_header<ERA, HEADER>(wrapper.cbor());
        cbor::zero2::decoder dec { header };
        auto &it = dec.read().array();
        return copy(it.read().data_raw());
    }

    template<typename T>
    void decode_native_script(const buffer source) {
        cbor::zero2::decoder dec { source };
        T::validate_cbor(dec.read());
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
    }

    template<cardano::era_t ERA, typename T>
    uint8_vector reserialize_native_script(const buffer source) {
        decode_native_script<T>(source);
        const auto script = T::from_cbor(source);
        cardano::era_encoder enc { ERA };
        script.to_cbor(enc);
        return std::move(enc.cbor());
    }

    void decode_plutus_data(const buffer source) {
        plutus::data::validate_cbor(source);
    }

    template<cardano::era_t ERA>
    uint8_vector reserialize_plutus_data(const buffer source) {
        plutus::data::validate_cbor(source);
        plutus::allocator alloc {};
        const auto data = plutus::data::from_cbor(alloc, source);
        cardano::era_encoder enc { ERA };
        data.to_cbor(enc);
        return std::move(enc.cbor());
    }

    template<cardano::era_t ERA>
    uint8_vector reserialize_datum_option(const buffer source) {
        const auto datum = decode_cbor_value<cardano::datum_option_t>(source);
        cardano::era_encoder enc { ERA };
        cardano::babbage::detail::datum_option_to_cbor_semantic(enc, datum);
        return std::move(enc.cbor());
    }

    void decode_dijkstra_proposal_procedure(const buffer source) {
        cbor::encoder enc {};
        enc.tag(258).array(1).raw_cbor(source);
        decode_cbor<cardano::dijkstra::proposal_procedures_t>(enc.cbor());
    }

    uint8_vector reserialize_dijkstra_proposal_procedure(const buffer source) {
        cbor::encoder wrapper {};
        wrapper.tag(258).array(1).raw_cbor(source);
        const auto proposals = reserialize_cbor<
            cardano::era_t::dijkstra, cardano::dijkstra::proposal_procedures_t>(wrapper.cbor());
        cbor::zero2::decoder dec { proposals };
        auto &items = dec.read().tag().read().array();
        return copy(items.read().data_raw());
    }

    template<uint64_t FIELD>
    void decode_dijkstra_transaction_body_field(const buffer source) {
        cbor::encoder enc {};
        enc.map(4)
            .uint(0).array(0)
            .uint(1).array(0)
            .uint(2).uint(0)
            .uint(FIELD).raw_cbor(source);
        decode_cbor<cardano::dijkstra::transaction_body_t>(enc.cbor());
    }

    template<uint64_t FIELD>
    uint8_vector reserialize_dijkstra_transaction_body_field(const buffer source) {
        cbor::encoder wrapper {};
        wrapper.map(4)
            .uint(0).array(0)
            .uint(1).array(0)
            .uint(2).uint(0)
            .uint(FIELD).raw_cbor(source);
        const auto body = reserialize_cbor<
            cardano::era_t::dijkstra, cardano::dijkstra::transaction_body_t>(wrapper.cbor());
        cbor::zero2::decoder dec { body };
        auto &it = dec.read().map();
        while (!it.done()) {
            auto &key = it.read_key();
            const auto field = key.uint();
            auto &value = it.read_val(std::move(key));
            if (field == FIELD)
                return copy(value.data_raw());
        }
        throw error(fmt::format("reserialized Dijkstra transaction body omitted field {}", FIELD));
    }

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

    void decode_dijkstra_block_body(const buffer source) {
        cbor::zero2::decoder dec { source };
        auto &body_it = dec.read().array();
        static_cast<void>(body_it.read().data_raw());
        static_cast<void>(body_it.read().data_raw());
        const bool contains_leios_certificate = !body_it.read().is_null();

        cbor::encoder enc {};
        enc.array(2);
        encode_dijkstra_test_header(enc, contains_leios_certificate);
        enc.raw_cbor(source);
        decode_block<cardano::era_t::dijkstra, cardano::dijkstra::block>(enc.cbor());
    }

    uint8_vector reserialize_dijkstra_block_body(const buffer source) {
        cbor::zero2::decoder source_dec { source };
        auto &body_it = source_dec.read().array();
        static_cast<void>(body_it.read().data_raw());
        static_cast<void>(body_it.read().data_raw());
        const bool contains_leios_certificate = !body_it.read().is_null();

        cbor::encoder wrapper {};
        wrapper.array(2);
        encode_dijkstra_test_header(wrapper, contains_leios_certificate);
        wrapper.raw_cbor(source);
        const auto block = reserialize_block<
            cardano::era_t::dijkstra, cardano::dijkstra::block>(wrapper.cbor());
        cbor::zero2::decoder dec { block };
        auto &it = dec.read().array();
        static_cast<void>(it.read().data_raw());
        return copy(it.read().data_raw());
    }

    const codec_map &codecs_for(const fs::path &sample_dir) {
        static const codec_map conway_codecs = {
            {"auxiliary_data", {
                decode_cbor<cardano::conway::auxiliary_data_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::auxiliary_data_t>
            }},
            {"block", {
                decode_block<cardano::era_t::conway, cardano::conway::block>,
                reserialize_block<cardano::era_t::conway, cardano::conway::block>
            }},
            {"certificate", {
                decode_cbor<cardano::conway::certificate_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::certificate_t>
            }},
            {"cost_models", {
                decode_cbor<cardano::conway::cost_models_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::cost_models_t>
            }},
            {"credential", {
                decode_cbor<cardano::credential_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::credential_t>
            }},
            {"datum_option", {
                decode_cbor<cardano::datum_option_t>,
                reserialize_datum_option<cardano::era_t::conway>
            }},
            {"drep", {
                decode_cbor<cardano::drep_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::drep_t>
            }},
            {"gov_action", {
                decode_cbor<cardano::gov_action_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::gov_action_t>
            }},
            {"header", {
                decode_header<cardano::era_t::conway, cardano::conway::block_header>,
                reserialize_header<cardano::era_t::conway, cardano::conway::block_header>
            }},
            {"header_body", {
                decode_header_body<cardano::era_t::conway, cardano::conway::block_header>,
                reserialize_header_body<cardano::era_t::conway, cardano::conway::block_header>
            }},
            {"mint", {
                decode_cbor<cardano::conway::mint_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::mint_t>
            }},
            {"native_script", {
                decode_native_script<cardano::allegra::native_script_t>,
                reserialize_native_script<cardano::era_t::conway, cardano::allegra::native_script_t>
            }},
            {"plutus_data", {
                decode_plutus_data,
                reserialize_plutus_data<cardano::era_t::conway>
            }},
            {"proposal_procedure", {
                decode_cbor<cardano::proposal_procedure_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::proposal_procedure_t>
            }},
            {"protocol_param_update", {
                decode_cbor<cardano::param_update_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::param_update_t>
            }},
            {"redeemer", {
                decode_cbor<cardano::conway::redeemer_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::redeemer_t>
            }},
            {"redeemers", {
                decode_cbor<cardano::conway::redeemers_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::redeemers_t>
            }},
            {"relay", {
                decode_cbor<cardano::relay_info>,
                reserialize_cbor<cardano::era_t::conway, cardano::relay_info>
            }},
            {"script", {
                decode_cbor<cardano::conway::script_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::script_t>
            }},
            {"transaction", {
                decode_cbor<cardano::conway::transaction_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::transaction_t>
            }},
            {"transaction_body", {
                decode_cbor<cardano::conway::transaction_body_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::transaction_body_t>
            }},
            {"transaction_input", {
                decode_cbor<cardano::shelley::transaction_input_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::shelley::transaction_input_t>
            }},
            {"transaction_output", {
                decode_cbor<cardano::conway::transaction_output_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::transaction_output_t>
            }},
            {"transaction_witness_set", {
                decode_cbor<cardano::conway::transaction_witness_set_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::transaction_witness_set_t>
            }},
            {"value", {
                decode_cbor<cardano::conway::value_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::conway::value_t>
            }},
            {"voting_procedure", {
                decode_cbor<cardano::voting_procedure_t>,
                reserialize_cbor<cardano::era_t::conway, cardano::voting_procedure_t>
            }}
        };
        static const codec_map dijkstra_codecs = {
            {"account_balance_interval", {
                decode_cbor<cardano::dijkstra::account_balance_interval_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::account_balance_interval_t>
            }},
            {"account_balance_intervals", {
                decode_dijkstra_transaction_body_field<26>,
                reserialize_dijkstra_transaction_body_field<26>
            }},
            {"auxiliary_data", {
                decode_cbor<cardano::dijkstra::auxiliary_data_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::auxiliary_data_t>
            }},
            {"block", {
                decode_block<cardano::era_t::dijkstra, cardano::dijkstra::block>,
                reserialize_block<cardano::era_t::dijkstra, cardano::dijkstra::block>
            }},
            {"block_body", {
                decode_dijkstra_block_body,
                reserialize_dijkstra_block_body
            }},
            {"certificate", {
                decode_cbor<cardano::dijkstra::certificate_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::certificate_t>
            }},
            {"certificates", {
                decode_cbor<cardano::dijkstra::certificates_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::certificates_t>
            }},
            {"cost_models", {
                decode_cbor<cardano::conway::cost_models_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::conway::cost_models_t>
            }},
            {"credential", {
                decode_cbor<cardano::credential_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::credential_t>
            }},
            {"datum_option", {
                decode_cbor<cardano::datum_option_t>,
                reserialize_datum_option<cardano::era_t::dijkstra>
            }},
            {"drep", {
                decode_cbor<cardano::drep_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::drep_t>
            }},
            {"gov_action", {
                decode_cbor<cardano::dijkstra::governance_action_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::governance_action_t>
            }},
            {"header", {
                decode_header<cardano::era_t::dijkstra, cardano::dijkstra::block_header>,
                reserialize_header<cardano::era_t::dijkstra, cardano::dijkstra::block_header>
            }},
            {"header_body", {
                decode_header_body<cardano::era_t::dijkstra, cardano::dijkstra::block_header>,
                reserialize_header_body<cardano::era_t::dijkstra, cardano::dijkstra::block_header>
            }},
            {"native_script", {
                decode_native_script<cardano::dijkstra::native_script_t>,
                reserialize_native_script<cardano::era_t::dijkstra, cardano::dijkstra::native_script_t>
            }},
            {"plutus_data", {
                decode_cbor<cardano::dijkstra::plutus_data_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::plutus_data_t>
            }},
            {"proposal_procedure", {
                decode_dijkstra_proposal_procedure,
                reserialize_dijkstra_proposal_procedure
            }},
            {"proposal_procedures", {
                decode_cbor<cardano::dijkstra::proposal_procedures_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::proposal_procedures_t>
            }},
            {"protocol_param_update", {
                decode_cbor<cardano::dijkstra::protocol_param_update_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::protocol_param_update_t>
            }},
            {"redeemers", {
                decode_cbor<cardano::dijkstra::redeemers_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::redeemers_t>
            }},
            {"relay", {
                decode_cbor<cardano::relay_info>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::relay_info>
            }},
            {"script", {
                decode_cbor<cardano::dijkstra::script_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::script_t>
            }},
            {"sub_transaction_body", {
                decode_cbor<cardano::dijkstra::sub_transaction_body_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::sub_transaction_body_t>
            }},
            {"transaction", {
                decode_cbor<cardano::dijkstra::transaction_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::transaction_t>
            }},
            {"transaction_body", {
                decode_cbor<cardano::dijkstra::transaction_body_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::transaction_body_t>
            }},
            {"transaction_input", {
                decode_cbor<cardano::shelley::transaction_input_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::shelley::transaction_input_t>
            }},
            {"transaction_output", {
                decode_cbor<cardano::dijkstra::transaction_output_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::transaction_output_t>
            }},
            {"transaction_witness_set", {
                decode_cbor<cardano::dijkstra::transaction_witness_set_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::transaction_witness_set_t>
            }},
            {"value", {
                decode_cbor<cardano::dijkstra::value_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::dijkstra::value_t>
            }},
            {"voting_procedure", {
                decode_cbor<cardano::voting_procedure_t>,
                reserialize_cbor<cardano::era_t::dijkstra, cardano::voting_procedure_t>
            }},
            {"voting_procedures", {
                decode_dijkstra_transaction_body_field<19>,
                reserialize_dijkstra_transaction_body_field<19>
            }}
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

    test_mode parse_mode(const std::string_view mode) {
        if (mode == "deserialize")
            return test_mode::deserialize;
        if (mode == "reserialize")
            return test_mode::reserialize;
        if (mode == "expected")
            return test_mode::expected;
        throw error(fmt::format(
            "unsupported test mode '{}': expected deserialize, reserialize, or expected", mode));
    }

    std::string_view mode_name(const test_mode mode) {
        switch (mode) {
            case test_mode::deserialize: return "deserialize";
            case test_mode::reserialize: return "reserialize";
            case test_mode::expected: return "expected";
        }
        std::unreachable();
    }

    bool path_is_within(const fs::path &parent, const fs::path &child) {
        const auto relative = child.lexically_relative(parent);
        if (relative.empty())
            return true;
        const auto first = relative.begin();
        return first != relative.end() && *first != ".." && !relative.is_absolute();
    }

    struct cmd: command {
        void configure(config &cmd) const override {
            cmd.name = "test-cbor-dataset";
            cmd.desc = "evaluate era-specific CBOR codecs against a generated dataset";
            cmd.args.expect({ "<mode>", "<dataset-dir>", "[<expected-dir-or-type>...]" });
            cmd.usage = "test-cbor-dataset deserialize <dataset-dir> [<type>...]\n"
                "       test-cbor-dataset reserialize <dataset-dir> [<type>...]\n"
                "       test-cbor-dataset expected <dataset-dir> <expected-dir> [<type>...]";
        }

        void run(const arguments &args) const override {
            const auto mode = parse_mode(args.at(0));
            const fs::path requested_sample_dir { args.at(1) };
            if (!fs::is_directory(requested_sample_dir)) [[unlikely]]
                throw error(fmt::format("expected a dataset directory: {}", requested_sample_dir));
            const auto sample_dir = fs::canonical(requested_sample_dir);
            const auto &codecs = codecs_for(sample_dir);

            std::optional<fs::path> expected_dir {};
            size_t first_type_arg = 2;
            if (mode == test_mode::expected) {
                if (args.size() < 3) [[unlikely]]
                    _throw_usage();
                const fs::path requested_expected_dir { args.at(2) };
                if (!fs::is_directory(requested_expected_dir)) [[unlikely]]
                    throw error(fmt::format("expected an expected-output directory: {}", requested_expected_dir));
                expected_dir.emplace(fs::canonical(requested_expected_dir));
                if (path_is_within(sample_dir, *expected_dir)
                        || path_is_within(*expected_dir, sample_dir)) [[unlikely]]
                    throw error("expected-output and dataset directories must not overlap");
                first_type_arg = 3;
            }
            const auto types = args | std::views::drop(first_type_arg);

            int64_t total = 0;
            int64_t expected_valid = 0;
            int64_t expected_invalid = 0;
            int64_t failed = 0;
            int64_t unsupported = 0;
            for (const auto &e: fs::recursive_directory_iterator(sample_dir)) {
                if (!e.is_regular_file() || e.path().extension() != ".cbor")
                    continue;
                const auto path = e.path().string();
                const auto relative_path = e.path().lexically_relative(sample_dir);
                auto part_it = relative_path.begin();
                if (part_it == relative_path.end()) [[unlikely]]
                    throw error(fmt::format("can't determine the type name from path {}", relative_path));
                const auto type_name = (part_it++)->string();
                if (part_it == relative_path.end()) [[unlikely]]
                    throw error(fmt::format("can't determine the test name from path {}", relative_path));
                const auto test_name = (part_it++)->string();
                if (part_it == relative_path.end()) [[unlikely]]
                    throw error(fmt::format("sample path {} does not have two directory levels", relative_path));
                ++part_it;
                if (part_it != relative_path.end()) [[unlikely]]
                    throw error(fmt::format("sample path {} has more than two directory levels", relative_path));
                if (!types.empty() && std::find(types.begin(), types.end(), type_name) == types.end())
                    continue;

                const bool must_decode = test_name == "valid";
                const bool must_reject = test_name == "zap-1"
                    || test_name == "zap-2"
                    || test_name == "zap-3";
                if (!must_decode && !must_reject) [[unlikely]]
                    throw error(fmt::format("unsupported dataset category '{}' in {}", test_name, relative_path));
                const auto original = file::read(path);
                ++total;
                expected_valid += must_decode;
                expected_invalid += must_reject;

                const auto codec_it = codecs.find(type_name);
                if (codec_it == codecs.end()) {
                    logger::warn("{}: unsupported type", relative_path);
                    ++unsupported;
                    continue;
                }

                try {
                    std::optional<uint8_vector> reserialized {};
                    if (mode == test_mode::deserialize)
                        codec_it->second.deserialize(original);
                    else
                        reserialized.emplace(codec_it->second.reserialize(original));

                    if (must_reject) {
                        logger::warn("{}: deserialization unexpectedly succeeded", relative_path);
                        ++failed;
                        continue;
                    }

                    if (mode == test_mode::reserialize && *reserialized != original) {
                        throw error(fmt::format(
                            "reserialization differs from input (input: {} bytes, output: {} bytes)",
                            original.size(), reserialized->size()));
                    }
                    if (mode == test_mode::expected) {
                        const auto expected_path = *expected_dir / relative_path;
                        const auto expected = file::read(expected_path);
                        if (*reserialized != expected) [[unlikely]]
                            throw error(fmt::format(
                                "reserialization differs from expected output {} (expected: {} bytes, output: {} bytes)",
                                expected_path, expected.size(), reserialized->size()));
                    }
                } catch (const std::exception &ex) {
                    if (must_decode) {
                        logger::warn("{}: {}", relative_path, ex.what());
                        ++failed;
                    }
                }
            }
            if (!total) [[unlikely]]
                throw error(fmt::format("dataset contains no selected CBOR files: {}", sample_dir));

            const auto passed = total - failed - unsupported;
            logger::info(
                "checked {} CBOR files: mode: {}, expected valid: {}, expected invalid: {}, "
                "passed: {}, failed: {}, unsupported: {}, pass rate: {:0.3f}%",
                total, mode_name(mode), expected_valid, expected_invalid, passed, failed, unsupported,
                static_cast<double>(passed) * 100 / total);
            if (failed || unsupported) [[unlikely]]
                logger::error("{} CBOR dataset checks failed", failed + unsupported);
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}
