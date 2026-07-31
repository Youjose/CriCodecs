#pragma once
/**
 * @file acb_cue_renderer.hpp
 * @brief Deterministic, static playback planning and PCM rendering for ACB cues.
 *
 * The CRI runtime can keep an infinite block active until a game action changes
 * it. A file exporter cannot observe that action, so the policy below makes
 * that otherwise implicit decision explicit and inspectable.
 */

#include "acb_container.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cricodecs::acb {

struct AcbCueBlockLoopOverride {
    /// Zero-based position in the authored BlockSequence.BlockIndex order.
    uint32_t block_position = 0;
    /// Number of repeats after the block's initial play.
    uint32_t loop_count = 0;
};

struct AcbCueRenderOptions {
    /// Repeats after the initial play for an audio-bearing infinite block.
    uint32_t infinite_block_loop_count = 0;
    /// Continue to the next authored block after the finite export substitute.
    bool advance_after_infinite_block = true;
    /// Render infinite holding blocks that schedule no waveform material.
    bool include_empty_infinite_blocks = true;
    /// Per-position repeat overrides, applied after authored loop policy.
    std::vector<AcbCueBlockLoopOverride> block_loop_overrides;
    uint64_t hca_keycode = 0;
    std::optional<uint16_t> hca_subkey;
};

enum class AcbCueAwbBank : uint8_t {
    memory,
    stream,
};

struct AcbCueClipPlan {
    uint32_t waveform_index = 0;
    int64_t start_time_us = 0;
    /// Selected authored AWB/AFS2 wave ID from the ACB waveform row.
    std::optional<uint16_t> awb_wave_id;
    /// Zero-based physical stream/file index in the resolved AWB.
    std::optional<uint32_t> awb_stream_index;
    /// Authored memory/stream bank selection, refined when an AWB is resolved.
    std::optional<AcbCueAwbBank> awb_bank;
};

struct AcbCueBlockPlan {
    std::optional<uint32_t> block_position;
    std::optional<uint32_t> block_index;
    std::string name;
    uint64_t duration_us = 0;
    int32_t authored_loop_count = 0;
    uint32_t render_loop_count = 0;
    bool forced_advance = false;
    bool skipped_empty_hold = false;
    std::vector<AcbCueClipPlan> clips;
};

struct AcbCuePlaybackPlan {
    uint32_t cue_index = 0;
    uint32_t cue_id = 0;
    std::string cue_name;
    std::vector<AcbCueBlockPlan> blocks;
    std::vector<std::string> diagnostics;
};

struct AcbRenderedCue {
    AcbCuePlaybackPlan plan;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    std::vector<int16_t> pcm;
};

[[nodiscard]] std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::expected<AcbRenderedCue, std::string> render_cue(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::expected<void, std::string> extract_cue(
    const AcbContainer& acb,
    uint32_t cue_index,
    const std::filesystem::path& output_path,
    const AcbCueRenderOptions& options = {});

[[nodiscard]] std::string cue_filename(
    const AcbContainer& acb,
    uint32_t cue_index,
    bool include_index_prefix = false);

} // namespace cricodecs::acb
