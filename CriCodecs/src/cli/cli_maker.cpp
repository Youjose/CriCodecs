#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

[[nodiscard]] std::vector<adx::AdxLoop> wav_loops_as_adx_loops(const wav::WavContainer& wav) {
    std::vector<adx::AdxLoop> loops;
    const auto& sample_loops = wav.sampler().loops;
    loops.reserve(sample_loops.size());
    for (const auto& loop : sample_loops) {
        loops.push_back(adx::AdxLoop{
            .index = static_cast<uint16_t>(loop.cue_point_id),
            .type = static_cast<uint16_t>(loop.type),
            .start_sample = loop.start,
            .start_byte = 0,
            .end_sample = loop.end,
            .end_byte = 0,
        });
    }
    return loops;
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode_hca_from_wav(
    const wav::WavContainer& source,
    const Options& options
) {
    hca::HcaEncodeConfig config;
    config.sample_rate = source.sample_rate();
    config.channel_count = static_cast<uint8_t>(source.channels());

    auto encoded = hca::encode(source, config);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }

    const bool wants_encryption =
        options.cipher_type.has_value() || options.key.has_value() || options.subkey.value_or(0) != 0;
    if (!wants_encryption) {
        return *encoded;
    }

    auto key = hca_keycode(options);
    if (!key) {
        return std::unexpected(key.error());
    }
    const uint16_t cipher_type = options.cipher_type.value_or(56);
    if (cipher_type != 1 && cipher_type != 56) {
        return std::unexpected("HCA encode `--cipher-type` must be 1 or 56");
    }
    if (cipher_type == 56 && *key == 0 && options.subkey.value_or(0) == 0) {
        return std::unexpected("HCA encode type 56 requires `--key` or `--subkey`");
    }

    auto encrypted = hca::encrypt(*encoded, cipher_type, *key, options.subkey.value_or(0));
    if (!encrypted) {
        return std::unexpected(encrypted.error());
    }
    return *encrypted;
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode_adx_like_from_wav(
    const wav::WavContainer& source,
    Format format,
    const Options& options
) {
    adx::AdxEncodeConfig config;
    config.sample_rate = source.sample_rate();
    config.channels = static_cast<uint8_t>(source.channels());
    config.encoding_mode = (format == Format::ahx) ? 0x10 : 3;

    if (options.key.has_value()) {
        const uint16_t encryption_type = options.cipher_type.value_or(8);
        if (encryption_type != 8 && encryption_type != 9) {
            return std::unexpected(
                std::string(format == Format::ahx ? "AHX" : "ADX") + " encode `--cipher-type` must be 8 or 9"
            );
        }
        config.encryption_type = static_cast<uint8_t>(encryption_type);
        if (format == Format::ahx && options.key->find(',') != std::string::npos) {
            auto triplet = parse_key_triplet(*options.key);
            if (!triplet) {
                return std::unexpected(triplet.error());
            }
            config.ahx_key = ahx::AhxKey{
                .start = (*triplet)[0],
                .mult = (*triplet)[1],
                .add = (*triplet)[2],
            };
        } else if (encryption_type == 9) {
            auto numeric = parse_u64(*options.key, "--key");
            if (!numeric) {
                return std::unexpected(
                    std::string(format == Format::ahx ? "AHX" : "ADX") + " type-9 encode requires numeric `--key`"
                );
            }
            config.key64 = *numeric;
            config.subkey = options.subkey.value_or(0);
        } else {
            if (format == Format::adx && options.key->find(',') != std::string::npos) {
                return std::unexpected("ADX encode does not accept a triplet key; use a type-8 key string");
            }
            config.key_string = *options.key;
        }
    } else if (options.cipher_type.has_value() || options.subkey.value_or(0) != 0) {
        return std::unexpected(
            std::string(format == Format::ahx ? "AHX" : "ADX") + " encode encryption options require `--key`"
        );
    }

    const auto loops = (format == Format::adx) ? wav_loops_as_adx_loops(source) : std::vector<adx::AdxLoop>{};
    auto encoded = adx::AdxEncoder::encode(source, config, loops);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    return *encoded;
}

[[nodiscard]] std::expected<void, std::string> perform_encode_action(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const Options& options
) {
    if (!options.force_type.has_value()) {
        return std::unexpected("`--encode` requires `-f` with hca, adx, or ahx");
    }
    const Format format = *options.force_type;
    if (format != Format::hca && format != Format::adx && format != Format::ahx) {
        return std::unexpected("`--encode` currently supports hca, adx, and ahx");
    }

    wav::WavContainer source;
    if (auto loaded = source.load(input_path); !loaded) {
        return std::unexpected("WAV load failed: " + loaded.error());
    }

    std::expected<std::vector<uint8_t>, std::string> encoded =
        (format == Format::hca)
            ? encode_hca_from_wav(source, options)
            : encode_adx_like_from_wav(source, format, options);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    return write_bytes_file(output_path, *encoded);
}

[[nodiscard]] std::expected<std::string, std::string> required_second(
    const MutationSpec& mutation,
    std::string_view option
) {
    if (!mutation.second.has_value() || mutation.second->empty()) {
        return std::unexpected(std::string(option) + " expects LEFT=RIGHT");
    }
    return *mutation.second;
}

[[nodiscard]] std::expected<uint32_t, std::string> target_u32_index(
    std::string_view text,
    std::string_view option
) {
    auto index = parse_index_target(text);
    if (!index.has_value()) {
        return std::unexpected(std::string(option) + " target must be an entry index for this format");
    }
    if (*index > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(std::string(option) + " target index is out of range");
    }
    return static_cast<uint32_t>(*index);
}

[[nodiscard]] std::expected<void, std::string> apply_afs_mutation(
    afs::AfsContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            const std::filesystem::path source = mutation.first;
            auto bytes = read_bytes_file(source);
            if (!bytes) return std::unexpected(bytes.error());
            archive.add_file(*bytes, mutation.second);
            return {};
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = target_u32_index(mutation.first, "--replace");
            if (!index) return std::unexpected(index.error());
            auto bytes = read_bytes_file(*source);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.replace_file(*index, *bytes);
        }
        case MutationKind::remove: {
            auto index = target_u32_index(mutation.first, "--remove");
            if (!index) return std::unexpected(index.error());
            return archive.remove_file(*index);
        }
        case MutationKind::rename: {
            auto name = required_second(mutation, "--rename");
            if (!name) return std::unexpected(name.error());
            auto index = target_u32_index(mutation.first, "--rename");
            if (!index) return std::unexpected(index.error());
            return archive.set_name(*index, *name);
        }
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported AFS mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_awb_mutation(
    awb::AwbContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto bytes = read_bytes_file(mutation.first);
            if (!bytes) return std::unexpected(bytes.error());
            if (mutation.second.has_value()) {
                auto wave_id = parse_u64(*mutation.second, "--add wave ID");
                if (!wave_id) return std::unexpected(wave_id.error());
                archive.add_file(*bytes, *wave_id);
            } else {
                archive.add_file(*bytes);
            }
            return {};
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = target_u32_index(mutation.first, "--replace");
            if (!index) return std::unexpected(index.error());
            auto bytes = read_bytes_file(*source);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.replace_file(*index, *bytes);
        }
        case MutationKind::remove: {
            auto index = target_u32_index(mutation.first, "--remove");
            if (!index) return std::unexpected(index.error());
            return archive.remove_file(*index);
        }
        case MutationKind::rename:
            return std::unexpected("AWB does not support `--rename`; use `--add SRC=WAVE_ID` for explicit new wave IDs");
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported AWB mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_cpk_mutation(
    cpk::Cpk& archive,
    const MutationSpec& mutation,
    const Options& options
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto dest = required_second(mutation, "--add");
            if (!dest) return std::unexpected(dest.error());
            archive.add_file(mutation.first, *dest, options.compress);
            return {};
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = parse_index_target(mutation.first);
            if (!index.has_value()) {
                return std::unexpected("CPK `--replace` target must be an entry index");
            }
            return archive.replace_file(*index, *source, options.compress);
        }
        case MutationKind::remove: {
            auto index = parse_index_target(mutation.first);
            if (!index.has_value()) {
                return std::unexpected("CPK `--remove` target must be an entry index");
            }
            return archive.remove(*index);
        }
        case MutationKind::rename: {
            auto dest = required_second(mutation, "--rename");
            if (!dest) return std::unexpected(dest.error());
            auto index = parse_index_target(mutation.first);
            if (!index.has_value()) {
                return std::unexpected("CPK `--rename` target must be an entry index");
            }
            return archive.rename(*index, *dest);
        }
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = parse_index_target(mutation.first);
            auto to_index = parse_index_target(*to);
            if (!from_index.has_value() || !to_index.has_value()) {
                return std::unexpected("CPK `--move` expects FROM=TO entry indexes");
            }
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported CPK mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_cvm_mutation(
    cvm::CvmContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto dest = required_second(mutation, "--add");
            if (!dest) return std::unexpected(dest.error());
            auto added = archive.add_file(mutation.first, *dest);
            if (!added) return std::unexpected(added.error());
            return {};
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            if (auto index = parse_index_target(mutation.first)) {
                return archive.replace_file(static_cast<uint32_t>(*index), *source);
            }
            return archive.replace_file(mutation.first, *source);
        }
        case MutationKind::remove: {
            if (auto index = parse_index_target(mutation.first)) {
                return archive.remove(static_cast<uint32_t>(*index));
            }
            return archive.remove(mutation.first);
        }
        case MutationKind::rename: {
            auto dest = required_second(mutation, "--rename");
            if (!dest) return std::unexpected(dest.error());
            if (auto index = parse_index_target(mutation.first)) {
                return archive.rename(static_cast<uint32_t>(*index), *dest);
            }
            return archive.rename(mutation.first, *dest);
        }
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported CVM mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_acx_mutation(
    acx::AcxContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto bytes = read_bytes_file(mutation.first);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.add_file(*bytes);
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = target_u32_index(mutation.first, "--replace");
            if (!index) return std::unexpected(index.error());
            auto bytes = read_bytes_file(*source);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.set_file_data(*index, *bytes);
        }
        case MutationKind::remove: {
            auto index = target_u32_index(mutation.first, "--remove");
            if (!index) return std::unexpected(index.error());
            return archive.remove_file(*index);
        }
        case MutationKind::rename:
            return std::unexpected("ACX does not support `--rename`");
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported ACX mutation");
}

[[nodiscard]] std::expected<void, std::string> save_mutated_document(
    LoadedResult& loaded,
    const std::filesystem::path& output_path,
    const Options& options
) {
    return std::visit([&](auto& current) -> std::expected<void, std::string> {
        using T = std::decay_t<decltype(current)>;
        if constexpr (std::is_same_v<T, afs::AfsContainer>) {
            for (const auto& mutation : options.mutations) {
                if (auto result = apply_afs_mutation(current, mutation); !result) return result;
            }
            return current.build_to_file(output_path);
        } else if constexpr (std::is_same_v<T, awb::AwbContainer>) {
            for (const auto& mutation : options.mutations) {
                if (auto result = apply_awb_mutation(current, mutation); !result) return result;
            }
            return current.save_to_file(output_path);
        } else if constexpr (std::is_same_v<T, cpk::Cpk>) {
            for (const auto& mutation : options.mutations) {
                if (auto result = apply_cpk_mutation(current, mutation, options); !result) return result;
            }
            return current.save_to_file(output_path);
        } else if constexpr (std::is_same_v<T, cvm::CvmContainer>) {
            for (const auto& mutation : options.mutations) {
                if (auto result = apply_cvm_mutation(current, mutation); !result) return result;
            }
            return current.save_to_file(output_path, options.key.value_or(""));
        } else if constexpr (std::is_same_v<T, acx::AcxContainer>) {
            for (const auto& mutation : options.mutations) {
                if (auto result = apply_acx_mutation(current, mutation); !result) return result;
            }
            return current.save_to_file(output_path);
        } else {
            return std::unexpected(std::string(format_label(loaded.format)) + " does not support CLI mutation yet");
        }
    }, loaded.document);
}

[[nodiscard]] std::expected<void, std::string> perform_build_action(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const Options& options
) {
    if (!options.force_type.has_value()) {
        return std::unexpected("`--build` requires `-f`");
    }

    const auto parse_sfd_profile = [](std::string_view text) -> std::expected<sfd::SfdBuildProfile, std::string> {
        const std::string key = lower_ascii(text);
        if (key == "sofdec" || key == "sofdec-stream" || key == "sofdec_stream") {
            return sfd::SfdBuildProfile::sofdec_stream_standard_fixed_2048;
        }
        if (key == "sofdec2-v23249" || key == "sofdec-stream2-v23249" || key == "stream2-v23249") {
            return sfd::SfdBuildProfile::sofdec_stream2_fixed_2048_v23249;
        }
        if (key == "sofdec2-v23310" || key == "sofdec-stream2-v23310" || key == "stream2-v23310") {
            return sfd::SfdBuildProfile::sofdec_stream2_fixed_2048_v23310;
        }
        return std::unexpected("unsupported SFD `--profile`; use sofdec, sofdec2-v23249, or sofdec2-v23310");
    };

    switch (*options.force_type) {
        case Format::afs: {
            if (lower_ascii(input_path.extension().string()) == ".als") {
                auto archive = afs::AfsContainer::create_from_als(input_path, afs::AfsContainer::DEFAULT_ALIGNMENT, true);
                if (!archive) return std::unexpected(archive.error());
                return archive->build_to_file(output_path);
            }
            auto files = collect_directory_files(input_path);
            if (!files) return std::unexpected(files.error());
            auto archive = afs::AfsContainer::create(afs::AfsContainer::DEFAULT_ALIGNMENT, true);
            for (const auto& [source, relative] : *files) {
                auto bytes = read_bytes_file(source);
                if (!bytes) return std::unexpected(bytes.error());
                archive.add_file(*bytes, relative.generic_string());
            }
            return archive.build_to_file(output_path);
        }
        case Format::awb: {
            auto files = collect_directory_files(input_path);
            if (!files) return std::unexpected(files.error());
            auto archive = awb::AwbContainer::create();
            for (const auto& [source, _] : *files) {
                auto bytes = read_bytes_file(source);
                if (!bytes) return std::unexpected(bytes.error());
                archive.add_file(*bytes);
            }
            return archive.save_to_file(output_path);
        }
        case Format::cpk: {
            auto files = collect_directory_files(input_path);
            if (!files) return std::unexpected(files.error());
            cpk::CpkBuilder builder;
            for (const auto& [source, relative] : *files) {
                builder.add_file(source, relative.generic_string(), options.compress);
            }
            cpk::CpkBuilderOptions build_options;
            build_options.encoding = encoding_options(options);
            return builder.build_to_file(output_path, build_options);
        }
        case Format::acx: {
            acx::AcxBuilder builder;
            if (lower_ascii(input_path.extension().string()) == ".fls") {
                return builder.build_file_list_to_file(input_path, output_path);
            }
            auto files = collect_directory_files(input_path);
            if (!files) return std::unexpected(files.error());
            acx::AcxBuildInput input;
            input.entries.reserve(files->size());
            for (const auto& [source, _] : *files) {
                input.entries.push_back({.source_path = source, .data = std::nullopt});
            }
            return builder.build_to_file(output_path, input);
        }
        case Format::csb:
            return csb::CsbContainer::build_to_file(input_path, output_path, encoding_options(options));
        case Format::cvm: {
            cvm::CvmBuilder builder;
            if (lower_ascii(input_path.extension().string()) == ".cvs") {
                auto script = cvm::CvmBuildScript::load(input_path);
                if (!script) return std::unexpected(script.error());
                return builder.build_to_file(output_path, *script, options.key.value_or(""));
            }
            auto input = cvm::CvmBuildInput::from_directory(input_path);
            if (!input) return std::unexpected(input.error());
            return builder.build_to_file(output_path, *input, options.key.value_or(""));
        }
        case Format::usm: {
            usm::UsmBuildInput input;
            input.video_path = input_path;
            input.encoding = encoding_options(options);
            if (options.key.has_value()) {
                auto key = parse_u64(*options.key, "--key");
                if (!key) return std::unexpected(key.error());
                input.key = *key;
            }
            for (const auto& audio_path : options.audio_paths) {
                input.audio_tracks.push_back({.path = audio_path, .encrypt = false});
            }
            usm::UsmBuilder builder;
            return builder.build_to_file(output_path, input);
        }
        case Format::sfd: {
            if (options.audio_paths.size() > 1) {
                return std::unexpected("SFD build currently supports at most one `--audio` input");
            }
            sfd::SfdBuildInput input;
            input.video_path = input_path;
            if (!options.audio_paths.empty()) {
                input.audio_path = options.audio_paths.front();
            }
            input.output_name = output_path.filename().string();
            if (options.profile.has_value()) {
                auto profile = parse_sfd_profile(*options.profile);
                if (!profile) return std::unexpected(profile.error());
                input.build_profile = *profile;
            }
            if (options.version.has_value()) {
                input.header_builder_version = *options.version;
            }
            sfd::SfdBuilder builder;
            return builder.build_to_file(output_path, input);
        }
        default:
            return std::unexpected("`--build` currently supports afs, awb, cpk, acx, csb, cvm, usm, and sfd");
    }
}


} // namespace cricodecs::cli::detail
