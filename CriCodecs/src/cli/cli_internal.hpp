#pragma once

#include "cli.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "../aax/aax_container.hpp"
#include "../acb/acb_container.hpp"
#include "../acx/acx_builder.hpp"
#include "../acx/acx_container.hpp"
#include "../adx/adx_codec.hpp"
#include "../adx/adx_key_recovery.hpp"
#include "../afs/afs_container.hpp"
#include "../ahx/ahx_codec.hpp"
#include "../ahx/ahx_key_recovery.hpp"
#include "../aix/aix_container.hpp"
#include "../awb/awb_container.hpp"
#include "../cpk/cpk_container.hpp"
#include "../csb/csb_container.hpp"
#include "../cvm/cvm_builder.hpp"
#include "../cvm/cvm_container.hpp"
#include "../hca/hca_codec.hpp"
#include "../sfd/sfd_container.hpp"
#include "../usm/usm_container.hpp"
#include "../utf/utf_table.hpp"
#include "../utilities/io.hpp"
#include "../utilities/text_encoding.hpp"
#include "../wav/wav_container.hpp"

namespace cricodecs::cli::detail {

constexpr uintmax_t kUnknownPathProbeLimit = 16u * 1024u * 1024u;

using LoadedDocument = std::variant<
    aax::AaxContainer,
    acb::AcbContainer,
    acx::AcxContainer,
    adx::Adx,
    afs::AfsContainer,
    aix::Aix,
    awb::AwbContainer,
    cpk::Cpk,
    csb::CsbContainer,
    cvm::CvmContainer,
    hca::Hca,
    sfd::SfdContainer,
    usm::UsmReader,
    utf::UtfTable
>;

enum class MutationKind {
    add,
    replace,
    remove,
    rename,
    move,
};

struct MutationSpec {
    MutationKind kind;
    std::string first;
    std::optional<std::string> second;
};

struct Options {
    bool metadata_only = false;
    bool json = false;
    bool quiet = false;
    bool help = false;
    bool list_only = false;
    bool raw = false;
    bool encode = false;
    bool build = false;
    bool compress = false;
    bool encrypt = false;
    bool decrypt = false;
    bool recover_key = false;
    bool independent_key_recovery = false;
    bool show_version = false;
    std::optional<Format> force_type;
    std::optional<std::filesystem::path> output_path;
    std::optional<std::string> encoding;
    std::optional<std::string> profile;
    std::optional<std::string> version;
    std::optional<std::string> key;
    std::optional<uint16_t> subkey;
    std::optional<uint16_t> cipher_type;
    std::optional<uint64_t> aac_keycode;
    std::vector<size_t> indexes;
    std::vector<std::filesystem::path> audio_paths;
    std::vector<uint8_t> audio_channels;
    std::vector<MutationSpec> mutations;
    std::vector<std::filesystem::path> input_paths;
};

struct LoadedResult {
    Format format;
    LoadedDocument document;
};

struct OutputItem {
    size_t index = 0;
    std::filesystem::path entry_name;
    std::filesystem::path relative_path;
};

struct Failure {
    Format format;
    std::string message;
    int score = 0;
};

struct HcaRecoveryOutput {
    hca::KeyRecoveryResult recovery;
    size_t hca_count = 0;
};

struct UsmRecoveryOutput {
    std::filesystem::path input_path;
    usm::KeyRecoveryResult recovery;
};

struct AdxRecoveryOutput {
    adx::AdxRecoveryResult guess;
    size_t source_count = 0;
};

struct AhxRecoveryOutput {
    ahx::AhxRecoveryResult guess;
    size_t source_count = 0;
};

struct AacRecoveryOutput {
    awb::KeyRecoveryResult recovery;
    size_t container_count = 0;
};

[[nodiscard]] std::string lower_ascii(std::string_view text);
[[nodiscard]] std::string trim_ascii(std::string_view text);
[[nodiscard]] std::expected<uint64_t, std::string> parse_u64(std::string_view text, std::string_view context);
[[nodiscard]] std::expected<uint16_t, std::string> parse_u16(std::string_view text, std::string_view context);
[[nodiscard]] std::optional<size_t> parse_index_target(std::string_view text);
[[nodiscard]] std::expected<std::pair<std::string, std::string>, std::string> parse_pair_value(
    std::string_view text,
    std::string_view option
);
[[nodiscard]] bool contains_placeholder(std::string_view text);
void replace_all(std::string& text, std::string_view needle, std::string_view replacement);
[[nodiscard]] std::string quote_json(std::string_view text);
[[nodiscard]] std::string bool_text(bool value);
[[nodiscard]] std::string hex_text(uint64_t value);
[[nodiscard]] std::string build_identity();
[[nodiscard]] bool has_magic_at(std::span<const uint8_t> bytes, size_t offset, const io::FourCC& magic);
[[nodiscard]] bool has_cvm_header(std::span<const uint8_t> bytes);
[[nodiscard]] bool looks_like_acx(std::span<const uint8_t> bytes) noexcept;
void push_unique(std::vector<Format>& formats, Format format);
[[nodiscard]] text::EncodingOptions encoding_options(const Options& options);
[[nodiscard]] std::filesystem::path default_output_path(
    const std::filesystem::path& input,
    Format format,
    bool raw
);
[[nodiscard]] std::filesystem::path crypto_output_path(
    const std::filesystem::path& input,
    std::string_view suffix
);
[[nodiscard]] std::expected<void, std::string> write_bytes_file(
    const std::filesystem::path& output_path,
    std::span<const uint8_t> bytes
);
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> read_bytes_file(
    const std::filesystem::path& input_path
);
[[nodiscard]] std::expected<std::vector<std::pair<std::filesystem::path, std::filesystem::path>>, std::string>
collect_directory_files(const std::filesystem::path& input_dir);
[[nodiscard]] std::expected<std::array<uint16_t, 3>, std::string> parse_key_triplet(std::string_view text);
[[nodiscard]] std::expected<void, std::string> apply_adx_key(adx::Adx& audio, const Options& options);
[[nodiscard]] std::expected<uint64_t, std::string> hca_keycode(const Options& options);

[[nodiscard]] std::expected<LoadedResult, std::string> load_best_effort(
    const std::filesystem::path& path,
    const Options& options
);
void print_metadata_text(std::ostream& out, Format format, const LoadedDocument& document);
void print_metadata_json(std::ostream& out, Format format, const LoadedDocument& document);
[[nodiscard]] std::expected<void, std::string> perform_encode_action(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const Options& options
);
[[nodiscard]] std::expected<void, std::string> save_mutated_document(
    LoadedResult& loaded,
    const std::filesystem::path& output_path,
    const Options& options
);
[[nodiscard]] std::expected<void, std::string> perform_build_action(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const Options& options
);
[[nodiscard]] std::expected<std::vector<OutputItem>, std::string> collect_export_items(
    LoadedResult& loaded,
    const Options& options
);
void print_item_list(std::ostream& out, const std::vector<OutputItem>& items);
[[nodiscard]] std::expected<void, std::string> perform_audio_export_action(
    LoadedResult& loaded,
    const std::filesystem::path& output_path,
    const Options& options
);
[[nodiscard]] std::expected<void, std::string> perform_crypto_action(
    LoadedResult& loaded,
    const std::filesystem::path& output_path,
    const Options& options
);
[[nodiscard]] std::expected<void, std::string> perform_multi_item_export(
    LoadedResult& loaded,
    const std::filesystem::path& input_path,
    const Options& options
);
[[nodiscard]] std::expected<HcaRecoveryOutput, std::string> perform_hca_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    const Options& options
);
void print_hca_key_recovery_text(std::ostream& out, const HcaRecoveryOutput& result);
void print_hca_key_recovery_json(std::ostream& out, const HcaRecoveryOutput& result);
[[nodiscard]] std::expected<std::vector<UsmRecoveryOutput>, std::string> perform_usm_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    const Options& options
);
void print_usm_key_recovery_text(std::ostream& out, std::span<const UsmRecoveryOutput> results);
void print_usm_key_recovery_json(std::ostream& out, std::span<const UsmRecoveryOutput> results);
[[nodiscard]] std::expected<AdxRecoveryOutput, std::string> perform_adx_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    const Options& options
);
void print_adx_key_recovery_text(std::ostream& out, const AdxRecoveryOutput& result);
void print_adx_key_recovery_json(std::ostream& out, const AdxRecoveryOutput& result);
[[nodiscard]] std::expected<AhxRecoveryOutput, std::string> perform_ahx_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    const Options& options
);
void print_ahx_key_recovery_text(std::ostream& out, const AhxRecoveryOutput& result);
void print_ahx_key_recovery_json(std::ostream& out, const AhxRecoveryOutput& result);
[[nodiscard]] std::expected<AacRecoveryOutput, std::string> perform_aac_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    Format container_format,
    const Options& options
);
void print_aac_key_recovery_text(std::ostream& out, const AacRecoveryOutput& result);
void print_aac_key_recovery_json(std::ostream& out, const AacRecoveryOutput& result);
[[nodiscard]] std::expected<Options, std::string> parse_options(std::span<const std::string> args);
void print_usage(std::ostream& out, bool show_identity = true);

} // namespace cricodecs::cli::detail
