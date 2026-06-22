#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

[[nodiscard]] std::expected<Options, std::string> parse_options(std::span<const std::string> args) {
    Options options;

    for (size_t index = 0; index < args.size(); ++index) {
        const auto& arg = args[index];
        const auto require_value = [&](std::string_view option) -> std::expected<std::string, std::string> {
            if (index + 1 >= args.size()) {
                return std::unexpected(std::string("missing value for option `") + std::string(option) + "`");
            }
            ++index;
            return args[index];
        };

        if (arg == "-h" || arg == "--help") {
            options.help = true;
            continue;
        }
        if (arg == "-e" || arg == "--export") {
            continue;
        }
        if (arg == "--encode") {
            options.encode = true;
            continue;
        }
        if (arg == "--build") {
            options.build = true;
            continue;
        }
        if (arg == "--compress") {
            options.compress = true;
            continue;
        }
        if (arg == "--encrypt") {
            options.encrypt = true;
            continue;
        }
        if (arg == "--decrypt") {
            options.decrypt = true;
            continue;
        }
        if (arg == "--raw") {
            options.raw = true;
            continue;
        }
        if (arg == "--list") {
            options.list_only = true;
            continue;
        }
        if (arg == "-m") {
            options.metadata_only = true;
            continue;
        }
        if (arg == "--json") {
            options.json = true;
            continue;
        }
        if (arg == "-q" || arg == "--quiet") {
            options.quiet = true;
            continue;
        }
        if (arg == "-f" || arg == "--force-type") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_format_key(*value);
            if (!parsed.has_value()) {
                return std::unexpected("unsupported forced type `" + *value + "`");
            }
            options.force_type = *parsed;
            continue;
        }
        if (arg == "-o" || arg == "--output") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.output_path = std::filesystem::path(*value);
            continue;
        }
        if (arg == "--index") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_u64(*value, "--index");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            options.indexes.push_back(static_cast<size_t>(*parsed));
            continue;
        }
        if (arg == "--key") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.key = *value;
            continue;
        }
        if (arg == "--subkey") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_u16(*value, "--subkey");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            options.subkey = *parsed;
            continue;
        }
        if (arg == "--cipher-type") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_u16(*value, "--cipher-type");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            options.cipher_type = *parsed;
            continue;
        }
        if (arg == "--aac-keycode") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_u64(*value, "--aac-keycode");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            options.aac_keycode = *parsed;
            continue;
        }
        if (arg == "--encoding") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.encoding = *value;
            continue;
        }
        if (arg == "--audio") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.audio_paths.emplace_back(*value);
            continue;
        }
        if (arg == "--profile") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.profile = *value;
            continue;
        }
        if (arg == "--version") {
            options.show_version = true;
            continue;
        }
        if (arg == "--header-version") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.version = *value;
            continue;
        }
        if (arg == "--add") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            const size_t split = value->find('=');
            if (split == std::string::npos) {
                options.mutations.push_back(MutationSpec{MutationKind::add, trim_ascii(*value), std::nullopt});
            } else {
                auto pair = parse_pair_value(*value, "--add");
                if (!pair) {
                    return std::unexpected(pair.error());
                }
                options.mutations.push_back(MutationSpec{MutationKind::add, pair->first, pair->second});
            }
            continue;
        }
        if (arg == "--replace") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto pair = parse_pair_value(*value, "--replace");
            if (!pair) {
                return std::unexpected(pair.error());
            }
            options.mutations.push_back(MutationSpec{MutationKind::replace, pair->first, pair->second});
            continue;
        }
        if (arg == "--remove") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.mutations.push_back(MutationSpec{MutationKind::remove, trim_ascii(*value), std::nullopt});
            continue;
        }
        if (arg == "--rename") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto pair = parse_pair_value(*value, "--rename");
            if (!pair) {
                return std::unexpected(pair.error());
            }
            options.mutations.push_back(MutationSpec{MutationKind::rename, pair->first, pair->second});
            continue;
        }
        if (arg == "--move") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto pair = parse_pair_value(*value, "--move");
            if (!pair) {
                return std::unexpected(pair.error());
            }
            options.mutations.push_back(MutationSpec{MutationKind::move, pair->first, pair->second});
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            return std::unexpected("unknown option `" + arg + "`");
        }
        if (options.input_path.has_value()) {
            return std::unexpected("only one input path is supported");
        }
        options.input_path = std::filesystem::path(arg);
    }

    if (options.help || options.show_version) {
        return options;
    }
    if (!options.input_path.has_value()) {
        options.help = true;
        return options;
    }
    if (options.encode && !options.output_path.has_value()) {
        return std::unexpected("`--encode` requires `-o`/`--output`");
    }
    if (options.encode && !options.force_type.has_value()) {
        return std::unexpected("`--encode` requires `-f` with hca, adx, or ahx");
    }
    if (options.build && !options.output_path.has_value()) {
        return std::unexpected("`--build` requires `-o`/`--output`");
    }
    if (options.build && !options.force_type.has_value()) {
        return std::unexpected("`--build` requires `-f`");
    }
    if (options.encode && (options.metadata_only || options.json || options.raw || options.list_only || options.encrypt || options.decrypt || !options.indexes.empty())) {
        return std::unexpected("`--encode` cannot be combined with export/list/metadata/crypto selection flags");
    }
    if (options.build && (options.encode || options.metadata_only || options.json || options.raw || options.list_only || options.encrypt || options.decrypt || !options.indexes.empty())) {
        return std::unexpected("`--build` cannot be combined with encode/export/list/metadata/crypto selection flags");
    }
    if (!options.build && (!options.audio_paths.empty() || options.profile.has_value() || options.version.has_value())) {
        return std::unexpected("`--audio`, `--profile`, and `--version` are only valid with `--build`");
    }
    if (!options.mutations.empty() && !options.output_path.has_value()) {
        return std::unexpected("mutation commands require `-o`/`--output`");
    }
    if (!options.mutations.empty() && (options.build || options.encode || options.metadata_only || options.json || options.raw || options.list_only || options.encrypt || options.decrypt || !options.indexes.empty())) {
        return std::unexpected("mutation commands cannot be combined with encode/export/list/metadata/crypto selection flags");
    }
    if (options.compress && options.mutations.empty() && !options.build) {
        return std::unexpected("`--compress` is only valid with mutation/build commands");
    }
    if (options.encrypt && options.decrypt) {
        return std::unexpected("`--encrypt` and `--decrypt` cannot be combined");
    }
    if ((options.encrypt || options.decrypt) && options.raw) {
        return std::unexpected("`--raw` cannot be combined with `--encrypt` or `--decrypt`");
    }
    if (options.json && !options.metadata_only) {
        return std::unexpected("`--json` requires `-m`");
    }
    if (options.metadata_only && (options.raw || options.list_only || !options.indexes.empty() || options.encrypt || options.decrypt)) {
        return std::unexpected("`-m` cannot be combined with export/list selection flags");
    }
    if (options.list_only && options.output_path.has_value()) {
        return std::unexpected("`--list` cannot be combined with `--output`");
    }
    if (options.list_only && options.metadata_only) {
        return std::unexpected("`--list` cannot be combined with `-m`");
    }
    if (options.list_only && (options.encrypt || options.decrypt)) {
        return std::unexpected("`--list` cannot be combined with `--encrypt` or `--decrypt`");
    }
    if ((options.encrypt || options.decrypt) && !options.indexes.empty()) {
        return std::unexpected("`--index` is not valid with `--encrypt` or `--decrypt`");
    }
    return options;
}

void print_usage(std::ostream& out, bool show_identity) {
    if (show_identity) {
        out << "CriCodecs " << build_identity() << '\n';
    }
    out <<
        "Usage: cricodecs <input> [-e] [--encode|--build] [--raw] [--list] [--encrypt|--decrypt] [-m] [--json] [-q] [-f TYPE] [-o PATH]\n"
        "                 [--index N] [--key VALUE] [--subkey VALUE] [--cipher-type VALUE] [--aac-keycode VALUE]\n"
        "                 [--encoding NAME] [--audio PATH] [--profile NAME] [--header-version VALUE]\n"
        "\n"
        "  -e, --export         explicit export; same as default behavior\n"
        "      --encode         encode WAV input as hca/adx/ahx; requires -f and -o\n"
        "      --build          build afs/awb/cpk/acx/csb/cvm from directory or supported list/script input\n"
        "      --audio PATH     add build audio input; repeatable for usm/sfd\n"
        "      --profile NAME   build profile where supported\n"
        "      --header-version VALUE  builder/header version where supported\n"
        "      --add SRC=DEST   add file to archive; DEST is archive path/name or AWB wave ID\n"
        "      --replace T=SRC  replace archive entry T with source file SRC\n"
        "      --remove T       remove archive entry T\n"
        "      --rename T=DEST  rename archive entry T where supported\n"
        "      --move FROM=TO   reorder archive entries by index\n"
        "      --compress       compress added/replaced CPK payloads\n"
        "      --raw            export raw contained/original payloads without audio decode\n"
        "      --list           list exportable items and exit\n"
        "  -m                   print metadata only\n"
        "      --json           emit metadata as JSON (requires -m)\n"
        "      --encrypt        write an encrypted/scrambled output file where supported\n"
        "      --decrypt        write a decrypted/descrambled output file where supported\n"
        "  -o, --output         override output path/root\n"
        "                        ?i = selected entry index, ?e = entry filename, ?s = input filename\n"
        "      --index          export only a specific item index; repeatable\n"
        "  -q, --quiet          suppress non-error console output\n"
        "  -f, --force-type     force input parsing as a specific type\n"
        "      --key            format key string or numeric keycode where applicable\n"
        "      --subkey         numeric subkey for HCA/ADX type 9 where applicable\n"
        "      --cipher-type    target cipher/encryption type where applicable\n"
        "      --aac-keycode    AAC keycode for ACB waveform extraction\n"
        "      --encoding       text encoding override where applicable\n"
        "      --version        show version/build information\n"
        "  -h, --help           show this help text\n"
        "\n"
        "Valid force types:\n"
        "  aax acb acx adx afs ahx aix awb cpk csb cvm hca sfd usm utf\n";
}


} // namespace cricodecs::cli::detail
