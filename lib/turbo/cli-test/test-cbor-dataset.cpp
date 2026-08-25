/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <filesystem>
#include <string>
#include <unordered_map>
#include <turbo/cardano/common/cert.hpp>
#include <turbo/cardano/common/types.hpp>
#include <turbo/cardano/allegra/block.hpp>
#include <turbo/cardano/conway/block.hpp>
#include <turbo/cli/common.hpp>
#include <turbo/plutus/types-core.hpp>

namespace turbo::cli::test_cbor_dataset {
    namespace fs = std::filesystem;

    using decode_func = void (*)(buffer);

    template<typename T>
    T decode_cbor_value(const buffer source) {
        cbor::zero2::decoder dec{source};
        T res = T::from_cbor(dec.read());
        if (!dec.done())
            throw error("the sample contains more than one top-level CBOR value");
        return res;
    }

    template<typename T>
    void decode_cbor(const buffer source) {
        decode_cbor_value<T>(source);
    }

    void decode_conway_transaction(const buffer source) {
        decode_cbor<cardano::conway::transaction_t>(source);
    }

    void decode_conway_block(const buffer source) {
        cbor::zero2::decoder dec { source };
        static_cast<void>(cardano::conway::block {
            static_cast<uint64_t>(cardano::era_t::conway) + 1, 0, 0, dec.read(), cardano::config::get()
        });
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
    }

    void decode_conway_header(const buffer raw) {
        cbor::zero2::decoder dec{raw};
        static_cast<void>(cardano::conway::block_header {
            static_cast<uint64_t>(cardano::era_t::conway) + 1, dec.read(), cardano::config::get()
        });
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
    }

    void decode_redeemer(const buffer raw) {
        cbor::zero2::decoder dec{raw};
        cardano::conway::redeemer_t::from_cbor(dec.read());
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
    }

    void decode_plutus_data(const buffer source) {
        plutus::allocator alloc{};
        plutus::data::from_cbor(alloc, source);
    }

    void decode_native_script(const buffer raw) {
        cbor::zero2::decoder dec{raw};
        cardano::allegra::native_script_t::from_cbor(dec.read());
        if (!dec.done()) [[unlikely]]
            throw error("the sample contains more than one top-level CBOR value");
    }

    void decode_script(const buffer raw) {
        const auto s = decode_cbor_value<cardano::conway::script_t>(raw);
        if (s.value.type() == cardano::script_type::native) {
            decode_native_script(s.value.script());
        }
    }

    void decode_conway_auxiliary_data(const buffer source) {
        const auto aux = decode_cbor_value<cardano::conway::auxiliary_data_t>(source);
        if (std::holds_alternative<cardano::conway::auxiliary_data_array_t>(aux.value)) {
            const auto &aux_array = std::get<cardano::conway::auxiliary_data_array_t>(aux.value);
            for (const auto &script: aux_array.auxiliary_scripts)
                decode_native_script(script.script());
        } else if (std::holds_alternative<cardano::conway::auxiliary_data_map_t>(aux.value)) {
            const auto &aux_map = std::get<cardano::conway::auxiliary_data_map_t>(aux.value);
            for (const auto &script: aux_map.native_scripts)
                decode_native_script(script.script());
        }
    }

    struct cmd: command {
        void configure(config &cmd) const override {
            cmd.name = "test-cbor-dataset";
            cmd.desc = "deserialize and reserialize a generated Conway CBOR dataset";
            cmd.args.expect({ "<dataset-dir>", "[<type>...]" });
        }

        void run(const arguments &args) const override {
            const auto &sample_dir = args.at(0);
            const auto types = args | std::views::drop(1);
            int64_t total = 0, err = 0, unsupported = 0;
            for (const auto &e: std::filesystem::recursive_directory_iterator(sample_dir)) {
                if (!e.is_regular_file() || e.path().extension() != ".cbor")
                    continue;
                const auto path = e.path().string();
                const auto relative_path = e.path().lexically_relative(sample_dir);
                auto part_it = relative_path.begin();
                if (part_it == relative_path.end())
                    throw error(fmt::format("can't determine the type name from path {}", relative_path));
                const auto type_name = (part_it++)->string();
                if (part_it == relative_path.end())
                    throw error(fmt::format("can't determine the test name from path {}", relative_path));
                const auto test_name = (part_it++)->string();
                if (part_it == relative_path.end())
                    throw error(fmt::format("sample path {} does not have two directory levels", relative_path));
                if (!types.empty() && std::find(types.begin(), types.end(), type_name) == types.end())
                    continue;
                const auto original = file::read(path);
                ++total;
                try {
                    static std::unordered_map<std::string, decode_func> decoders = {
                        {"auxiliary_data", decode_conway_auxiliary_data},
                        {"block", decode_conway_block},
                        {"certificate", decode_cbor<cardano::conway::certificate_t>},
                        {"cost_models", decode_cbor<cardano::conway::cost_models_t>},
                        {"credential", decode_cbor<cardano::credential_t>},
                        {"datum_option", decode_cbor<cardano::datum_option_t>},
                        {"drep", decode_cbor<cardano::drep_t>},
                        {"gov_action", decode_cbor<cardano::gov_action_t>},
                        {"header", decode_conway_header},
                        {"mint", decode_cbor<cardano::conway::mint_t>},
                        {"native_script", decode_native_script},
                        {"proposal_procedure", decode_cbor<cardano::proposal_procedure_t>},
                        {"protocol_param_update", decode_cbor<cardano::param_update_t>},
                        {"plutus_data", decode_plutus_data},
                        {"redeemer", decode_redeemer},
                        {"relay", decode_cbor<cardano::relay_info>},
                        {"script", decode_script},
                        {"transaction", decode_conway_transaction},
                        {"transaction_input", decode_cbor<cardano::shelley::transaction_input_t>},
                        {"transaction_output", decode_cbor<cardano::conway::transaction_output_t>},
                        {"value", decode_cbor<cardano::conway::value_t>},
                        {"voting_procedure", decode_cbor<cardano::voting_procedure_t>}
                    };
                    if (const auto it = decoders.find(type_name); it != decoders.end()) {
                        it->second(original);
                        if (test_name.starts_with("zap")) {
                            logger::warn("{}: parsed successfully but it should not!", relative_path);
                            ++err;
                        }
                    } else {
                        logger::warn("{}: unsupported!", relative_path);
                        ++unsupported;
                    }
                } catch (const std::exception &ex) {
                    if (test_name.starts_with("valid")) {
                        logger::warn("{}: failed to parse: {}", relative_path, ex.what());
                        ++err;
                    }
                }
            }
            logger::info("total: {}, failed: {}, unsupported: {}, pass rate {:0.3f}%", total, err, unsupported, static_cast<double>(total - err - unsupported) * 100 / total);
        }
    };

    static auto instance = command::reg(std::make_shared<cmd>());
}
