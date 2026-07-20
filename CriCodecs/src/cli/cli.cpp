#include "cli_internal.hpp"

namespace cricodecs::cli {
using namespace detail;

namespace {

void print_build_line(std::ostream& out) {
    out << "build: " << build_identity() << '\n';
}

} // namespace

std::string_view format_key(Format format) noexcept {
    switch (format) {
        case Format::aax: return "aax";
        case Format::acb: return "acb";
        case Format::acx: return "acx";
        case Format::adx: return "adx";
        case Format::afs: return "afs";
        case Format::ahx: return "ahx";
        case Format::aix: return "aix";
        case Format::awb: return "awb";
        case Format::cpk: return "cpk";
        case Format::csb: return "csb";
        case Format::cvm: return "cvm";
        case Format::hca: return "hca";
        case Format::sfd: return "sfd";
        case Format::usm: return "usm";
        case Format::utf: return "utf";
        case Format::wav: return "wav";
        case Format::video: return "video";
    }
    return "unknown";
}

std::string_view format_label(Format format) noexcept {
    switch (format) {
        case Format::aax: return "AAX";
        case Format::acb: return "ACB";
        case Format::acx: return "ACX";
        case Format::adx: return "ADX";
        case Format::afs: return "AFS";
        case Format::ahx: return "AHX";
        case Format::aix: return "AIX";
        case Format::awb: return "AWB";
        case Format::cpk: return "CPK";
        case Format::csb: return "CSB";
        case Format::cvm: return "CVM";
        case Format::hca: return "HCA";
        case Format::sfd: return "SFD";
        case Format::usm: return "USM";
        case Format::utf: return "UTF";
        case Format::wav: return "WAV";
        case Format::video: return "Video";
    }
    return "Unknown";
}

std::optional<Format> parse_format_key(std::string_view text) noexcept {
    const std::string lowered = lower_ascii(text);
    if (lowered == "aax") return Format::aax;
    if (lowered == "acb") return Format::acb;
    if (lowered == "acx") return Format::acx;
    if (lowered == "adx") return Format::adx;
    if (lowered == "afs") return Format::afs;
    if (lowered == "ahx") return Format::ahx;
    if (lowered == "aix") return Format::aix;
    if (lowered == "awb") return Format::awb;
    if (lowered == "cpk") return Format::cpk;
    if (lowered == "csb") return Format::csb;
    if (lowered == "cvm") return Format::cvm;
    if (lowered == "hca") return Format::hca;
    if (lowered == "sfd") return Format::sfd;
    if (lowered == "usm") return Format::usm;
    if (lowered == "utf") return Format::utf;
    if (lowered == "wav") return Format::wav;
    if (lowered == "video") return Format::video;
    return std::nullopt;
}

bool format_supported_in_cli(Format format) noexcept {
    return format != Format::wav && format != Format::video;
}

bool format_supports_default_write(Format format) noexcept {
    return format != Format::utf;
}

int run(std::span<const std::string> args, std::ostream& out, std::ostream& err) {
    auto options = parse_options(args);
    if (!options) {
        err << options.error() << '\n';
        err << "Use `cricodecs --help` for usage.\n";
        return 1;
    }

    if (options->help) {
        print_usage(out, !options->quiet);
        return 0;
    }
    if (options->show_version) {
        out << build_identity() << '\n';
        return 0;
    }

    if (options->recover_key) {
        for (const auto& path : options->input_paths) {
            if (!std::filesystem::exists(path)) {
                err << "input path does not exist: " << path.string() << '\n';
                return 1;
            }
            if (!std::filesystem::is_regular_file(path) && !std::filesystem::is_directory(path)) {
                err << "input path is not a regular file or directory: " << path.string() << '\n';
                return 1;
            }
        }
        if (*options->force_type == Format::acb || *options->force_type == Format::awb) {
            auto recovered = perform_aac_key_recovery(options->input_paths, *options->force_type, *options);
            if (!recovered) {
                err << recovered.error() << '\n';
                return 1;
            }
            if (options->json) {
                print_aac_key_recovery_json(out, *recovered);
                out << '\n';
            } else {
                print_aac_key_recovery_text(out, *recovered);
            }
        } else if (*options->force_type == Format::usm) {
            auto recovered = perform_usm_key_recovery(options->input_paths, *options);
            if (!recovered) {
                err << recovered.error() << '\n';
                return 1;
            }
            if (options->json) {
                print_usm_key_recovery_json(out, *recovered);
                out << '\n';
            } else {
                print_usm_key_recovery_text(out, *recovered);
            }
        } else if (*options->force_type == Format::adx) {
            auto recovered = perform_adx_key_recovery(options->input_paths, *options);
            if (!recovered) {
                err << recovered.error() << '\n';
                return 1;
            }
            if (options->json) {
                print_adx_key_recovery_json(out, *recovered);
                out << '\n';
            } else {
                print_adx_key_recovery_text(out, *recovered);
            }
        } else if (*options->force_type == Format::ahx) {
            auto recovered = perform_ahx_key_recovery(options->input_paths, *options);
            if (!recovered) {
                err << recovered.error() << '\n';
                return 1;
            }
            if (options->json) {
                print_ahx_key_recovery_json(out, *recovered);
                out << '\n';
            } else {
                print_ahx_key_recovery_text(out, *recovered);
            }
        } else {
            auto recovered = perform_hca_key_recovery(options->input_paths, *options);
            if (!recovered) {
                err << recovered.error() << '\n';
                return 1;
            }
            if (options->json) {
                print_hca_key_recovery_json(out, *recovered);
                out << '\n';
            } else {
                print_hca_key_recovery_text(out, *recovered);
            }
        }
        return 0;
    }

    const auto& input_path = options->input_paths.front();
    if (!std::filesystem::exists(input_path)) {
        err << "input path does not exist: " << input_path.string() << '\n';
        return 1;
    }
    if (!options->build && !std::filesystem::is_regular_file(input_path)) {
        err << "input path is not a regular file: " << input_path.string() << '\n';
        return 1;
    }

    if (options->build) {
        if (!options->quiet) {
            print_build_line(out);
            out << "format: " << format_key(*options->force_type) << '\n';
            out << "output: " << options->output_path->string() << '\n';
        }
        auto built = perform_build_action(input_path, *options->output_path, *options);
        if (!built) {
            err << built.error() << '\n';
            return 1;
        }
        if (!options->quiet) {
            out << "done\n";
        }
        return 0;
    }

    if (options->encode) {
        if (!options->quiet) {
            print_build_line(out);
            out << "format: " << format_key(*options->force_type) << '\n';
            out << "output: " << options->output_path->string() << '\n';
        }
        auto encoded = perform_encode_action(input_path, *options->output_path, *options);
        if (!encoded) {
            err << encoded.error() << '\n';
            return 1;
        }
        if (!options->quiet) {
            out << "done\n";
        }
        return 0;
    }

    auto loaded = load_best_effort(input_path, *options);
    if (!loaded) {
        err << loaded.error() << '\n';
        return 1;
    }

    if (!options->mutations.empty()) {
        const auto& output_path = *options->output_path;
        if (!options->quiet) {
            out << "format: " << format_key(loaded->format) << '\n';
            out << "output: " << output_path.string() << '\n';
        }
        auto mutated = save_mutated_document(*loaded, output_path, *options);
        if (!mutated) {
            err << mutated.error() << '\n';
            return 1;
        }
        if (!options->quiet) {
            out << "done\n";
        }
        return 0;
    }

    const bool metadata_only = options->metadata_only || loaded->format == Format::utf;

    if (metadata_only && options->output_path.has_value()) {
        err << "`--output` is not valid for metadata-only mode\n";
        return 1;
    }

    if (metadata_only) {
        if (options->json) {
            print_metadata_json(out, loaded->format, loaded->document);
            out << '\n';
        } else {
            print_metadata_text(out, loaded->format, loaded->document);
        }
        return 0;
    }

    if (options->list_only) {
        auto items = collect_export_items(*loaded, *options);
        if (!items) {
            err << items.error() << '\n';
            return 1;
        }
        print_item_list(out, *items);
        return 0;
    }

    const bool top_level_audio_export =
        loaded->format == Format::adx || loaded->format == Format::ahx ||
        loaded->format == Format::hca || loaded->format == Format::aax;

    if (loaded->format == Format::utf) {
        err << "UTF has no export action yet\n";
        return 1;
    }

    if (!options->quiet) {
        print_build_line(out);
        out << "format: " << format_key(loaded->format) << '\n';
    }

    std::expected<void, std::string> action;
    if (options->encrypt || options->decrypt) {
        const auto output_path = options->output_path.value_or(
            crypto_output_path(input_path, options->encrypt ? "_encrypted" : "_decrypted")
        );
        if (output_path.empty()) {
            err << "could not derive an output path\n";
            return 1;
        }
        if (!options->quiet) {
            out << "output: " << output_path.string() << '\n';
        }
        action = perform_crypto_action(*loaded, output_path, *options);
    } else if (top_level_audio_export && (loaded->format != Format::aax || !options->raw || options->indexes.empty())) {
        if (!options->indexes.empty() && !(options->raw && loaded->format != Format::aax)) {
            err << "`--index` is only valid for multi-item exports or raw AAX segment export\n";
            return 1;
        }
        const auto output_path =
            options->output_path.value_or(default_output_path(input_path, loaded->format, options->raw));
        if (output_path.empty()) {
            err << "could not derive an output path\n";
            return 1;
        }
        if (!options->quiet) {
            out << "output: " << output_path.string() << '\n';
        }
        action = perform_audio_export_action(*loaded, output_path, *options);
    } else {
        const auto output_root =
            options->output_path.value_or(default_output_path(input_path, loaded->format, options->raw));
        if (output_root.empty()) {
            err << "could not derive an output path\n";
            return 1;
        }
        if (!options->quiet) {
            out << "output: " << output_root.string() << '\n';
        }
        action = perform_multi_item_export(*loaded, input_path, *options);
    }
    if (!action) {
        err << action.error() << '\n';
        return 1;
    }

    if (!options->quiet) {
        out << "done\n";
    }
    return 0;
}


} // namespace cricodecs::cli
