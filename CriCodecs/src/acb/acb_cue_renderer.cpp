/**
 * @file acb_cue_renderer.cpp
 * @brief Static ACB cue planning, timed layering, and supported audio decoding.
 */

#include "acb_cue_renderer.hpp"

#include "../adx/adx_codec.hpp"
#include "../hca/hca_codec.hpp"
#include "../wav/wav_container.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace cricodecs::acb {

namespace {

struct ReferenceKey {
    uint16_t type = 0;
    uint16_t index = invalid_acb_index;
    friend bool operator<(const ReferenceKey& lhs, const ReferenceKey& rhs) noexcept {
        return std::tie(lhs.type, lhs.index) < std::tie(rhs.type, rhs.index);
    }
};

struct AuthoredAwbReference {
    uint16_t wave_id = invalid_acb_index;
    AcbCueAwbBank bank = AcbCueAwbBank::memory;
};

std::optional<AuthoredAwbReference> authored_awb_reference(
    const AcbCueWaveform& waveform) {
    const bool stream_bank = waveform.streaming != 0;
    const auto first = stream_bank
        ? waveform.stream_awb_id
        : waveform.memory_awb_id;
    const auto fallback = stream_bank
        ? waveform.memory_awb_id
        : waveform.stream_awb_id;
    const auto wave_id = first != invalid_acb_index
        ? first
        : waveform.id != invalid_acb_index
            ? waveform.id
            : fallback;
    if (wave_id == invalid_acb_index) {
        return std::nullopt;
    }
    return AuthoredAwbReference{
        .wave_id = wave_id,
        .bank = stream_bank
            ? AcbCueAwbBank::stream
            : AcbCueAwbBank::memory,
    };
}

class PlaybackPlanner {
public:
    PlaybackPlanner(
        const AcbCueGraph& graph,
        uint32_t cue_index,
        const AcbCueRenderOptions& options)
        : m_graph(graph), m_cue_index(cue_index), m_options(options) {}

    std::expected<AcbCuePlaybackPlan, std::string> build() {
        std::set<uint32_t> override_positions;
        for (const auto& override : m_options.block_loop_overrides) {
            if (!override_positions.insert(override.block_position).second) {
                return std::unexpected(
                    "ACB cue plan failed: duplicate loop override for block position " +
                    std::to_string(override.block_position));
            }
        }
        if (m_cue_index >= m_graph.cues().size()) {
            return std::unexpected("ACB cue plan failed: cue index is out of range");
        }

        const auto& cue = m_graph.cues()[m_cue_index];
        m_plan.cue_index = m_cue_index;
        m_plan.cue_id = cue.cue_id;
        m_plan.cue_name = std::string(m_graph.cue_name(m_cue_index));
        if (m_plan.cue_name.empty()) {
            m_plan.cue_name = "cue_" + std::to_string(m_cue_index);
        }

        if (cue.reference.type == 8 || cue.reference.type == 9) {
            auto result = append_block_sequence(cue.reference.index);
            if (!result) return std::unexpected(result.error());
        } else {
            if (!m_options.block_loop_overrides.empty()) {
                return std::unexpected(
                    "ACB cue plan failed: block loop overrides require a BlockSequence cue");
            }
            AcbCueBlockPlan block{
                .block_position = std::nullopt,
                .block_index = std::nullopt,
                .name = m_plan.cue_name,
                .clips = {},
            };
            auto result = append_reference(
                cue.reference.type, cue.reference.index, 0, block.clips);
            if (!result) return std::unexpected(result.error());
            block.duration_us = inferred_duration_us(block.clips);
            if (!block.clips.empty()) {
                m_plan.blocks.push_back(std::move(block));
            }
        }

        if (std::ranges::none_of(m_plan.blocks, [](const AcbCueBlockPlan& block) {
                return !block.skipped_empty_hold && !block.clips.empty();
            })) {
            return std::unexpected(
                "ACB cue plan failed: cue `" + m_plan.cue_name +
                "` has no statically playable audio");
        }
        return std::move(m_plan);
    }

private:
    std::expected<void, std::string> append_block_sequence(uint16_t index) {
        if (index >= m_graph.block_sequences().size()) {
            return std::unexpected(
                "ACB cue plan failed: block-sequence index is out of range");
        }
        const auto& sequence = m_graph.block_sequences()[index];
        // Individual Block.TrackIndex lists schedule audio. Top-level
        // BlockSequence tracks carry sequence parameters in Music.acb and do
        // not have TrackEvent rows; those parameters remain inspectable.
        if (!sequence.track_indices.empty()) {
            m_plan.diagnostics.push_back(
                "top-level BlockSequence track parameters are preserved by the "
                "graph but are not applied by the static renderer");
        }
        if (sequence.num_watch_actions != 0 || sequence.num_stop_actions != 0) {
            m_plan.diagnostics.push_back(
                "watch/stop action tracks are preserved by the graph but are not executed "
                "by the static renderer");
        }

        for (const auto& override : m_options.block_loop_overrides) {
            if (override.block_position >= sequence.block_indices.size()) {
                return std::unexpected(
                    "ACB cue plan failed: block loop override position " +
                    std::to_string(override.block_position) + " is out of range");
            }
        }

        for (size_t position = 0;
             position < sequence.block_indices.size();
             ++position) {
            const auto block_position = static_cast<uint32_t>(position);
            const auto block_index = sequence.block_indices[position];
            if (block_index >= m_graph.blocks().size()) {
                return std::unexpected(
                    "ACB cue plan failed: block index is out of range");
            }
            const auto& authored = m_graph.blocks()[block_index];
            const int32_t loop_count = static_cast<int16_t>(authored.num_loops);
            AcbCueBlockPlan block{
                .block_position = block_position,
                .block_index = block_index,
                .name = std::string(m_graph.string_value(authored.name_index)),
                .duration_us = authored.duration_us(),
                .authored_loop_count = loop_count,
                .render_loop_count = loop_count < 0
                    ? m_options.infinite_block_loop_count
                    : static_cast<uint32_t>(loop_count),
                .clips = {},
            };
            if (block.name.empty()) {
                block.name = "block_" + std::to_string(block_index);
            }

            for (const auto track_index : authored.track_indices) {
                auto result = append_track(track_index, 0, block.clips);
                if (!result) return result;
            }
            if (authored.num_action_tracks != 0) {
                m_plan.diagnostics.push_back(
                    "block `" + block.name +
                    "` has action tracks that are not executed by the static renderer");
            }

            const auto override = std::ranges::find_if(
                m_options.block_loop_overrides,
                [block_position](const AcbCueBlockLoopOverride& candidate) {
                    return candidate.block_position == block_position;
                });
            const bool has_override =
                override != m_options.block_loop_overrides.end();
            if (has_override) {
                block.render_loop_count = override->loop_count;
                m_plan.diagnostics.push_back(
                    "block `" + block.name + "` at position " +
                    std::to_string(block_position) + " repeats " +
                    std::to_string(block.render_loop_count) +
                    " time(s) by explicit override");
            }

            if (loop_count < 0) {
                const bool empty_hold = block.clips.empty();
                block.forced_advance =
                    !empty_hold && m_options.advance_after_infinite_block;
                if (empty_hold &&
                    !m_options.include_empty_infinite_blocks &&
                    !has_override) {
                    block.skipped_empty_hold = true;
                    m_plan.diagnostics.push_back(
                        "block `" + block.name +
                        "` is an authored infinite hold with no scheduled waveform; "
                        "the static render skips its synthesized duration");
                } else {
                    if (empty_hold && !has_override) {
                        block.render_loop_count = 0;
                    }
                    m_plan.diagnostics.push_back(
                        "block `" + block.name +
                        "` is authored with LoopNum=-1 (infinite); "
                        "the static render repeats it " +
                        std::to_string(block.render_loop_count) + " time(s)" +
                        (block.forced_advance
                             ? " and then forces the next authored block"
                             : empty_hold
                                 ? " without applying the global loop/stop policy"
                                 : " and stops there"));
                }
            }
            m_plan.blocks.push_back(std::move(block));
            if (loop_count < 0 &&
                !m_options.advance_after_infinite_block &&
                !m_plan.blocks.back().clips.empty()) {
                break;
            }
        }
        return {};
    }

    std::expected<void, std::string> append_track(
        uint16_t index,
        int64_t base_time_us,
        std::vector<AcbCueClipPlan>& clips) {
        if (index >= m_graph.tracks().size()) {
            return std::unexpected("ACB cue plan failed: track index is out of range");
        }
        const auto& track = m_graph.tracks()[index];
        const auto kind = m_graph.track_events().empty()
            ? AcbCommandTableKind::legacy_command
            : AcbCommandTableKind::track_event;
        const auto* stream = m_graph.command_stream(kind, track.event_index);
        if (stream == nullptr) {
            return {};
        }
        for (const auto& target : stream->scheduled_targets) {
            auto result = append_reference(
                static_cast<uint16_t>(target.target.type),
                target.target.index,
                base_time_us + target.time_us,
                clips);
            if (!result) return result;
        }
        return {};
    }

    std::expected<void, std::string> append_reference(
        uint16_t type,
        uint16_t index,
        int64_t start_time_us,
        std::vector<AcbCueClipPlan>& clips) {
        if (start_time_us < 0) {
            return std::unexpected(
                "ACB cue plan failed: negative scheduled waveform time is unsupported");
        }
        const ReferenceKey key{type, index};
        if (m_active.size() >= 64 || !m_active.insert(key).second) {
            return std::unexpected(
                "ACB cue plan failed: cyclic or excessively deep playback reference");
        }

        std::expected<void, std::string> result;
        switch (type) {
            case 0:
                break;
            case 1:
                if (index >= m_graph.waveforms().size()) {
                    result = std::unexpected(
                        "ACB cue plan failed: waveform index is out of range");
                } else {
                    AcbCueClipPlan clip{
                        .waveform_index = index,
                        .start_time_us = start_time_us,
                        .awb_wave_id = std::nullopt,
                        .awb_stream_index = std::nullopt,
                        .awb_bank = std::nullopt,
                    };
                    if (const auto awb = authored_awb_reference(
                            m_graph.waveforms()[index])) {
                        clip.awb_wave_id = awb->wave_id;
                        clip.awb_bank = awb->bank;
                    }
                    clips.push_back(std::move(clip));
                }
                break;
            case 2:
            case 6:
                result = append_synth(index, start_time_us, clips);
                break;
            case 3:
            case 7:
                result = append_sequence(index, start_time_us, clips);
                break;
            case 5:
                result = std::unexpected(
                    "ACB cue plan failed: outside-link cues require the linked ACB");
                break;
            case 8:
            case 9:
                result = std::unexpected(
                    "ACB cue plan failed: nested block sequences are not supported");
                break;
            default:
                result = std::unexpected(
                    "ACB cue plan failed: unsupported reference type " +
                    std::to_string(type));
                break;
        }
        m_active.erase(key);
        return result;
    }

    std::expected<void, std::string> append_synth(
        uint16_t index,
        int64_t start_time_us,
        std::vector<AcbCueClipPlan>& clips) {
        if (index >= m_graph.synths().size()) {
            return std::unexpected("ACB cue plan failed: synth index is out of range");
        }
        const auto& synth = m_graph.synths()[index];
        if (synth.reference_items.size() > 1 && synth.type != 0) {
            return std::unexpected(
                "ACB cue plan failed: synth type `" +
                std::string(sequence_type_name(synth.type)) +
                "` requires a runtime choice");
        }
        for (const auto& reference : synth.reference_items) {
            auto result = append_reference(
                reference.type, reference.index, start_time_us, clips);
            if (!result) return result;
        }
        return {};
    }

    std::expected<void, std::string> append_sequence(
        uint16_t index,
        int64_t start_time_us,
        std::vector<AcbCueClipPlan>& clips) {
        if (index >= m_graph.sequences().size()) {
            return std::unexpected("ACB cue plan failed: sequence index is out of range");
        }
        const auto& sequence = m_graph.sequences()[index];
        if (sequence.track_indices.size() > 1 && sequence.type != 0) {
            return std::unexpected(
                "ACB cue plan failed: sequence type `" +
                std::string(sequence_type_name(sequence.type)) +
                "` requires a runtime track choice");
        }
        if (sequence.num_action_tracks != 0 ||
            sequence.num_watch_actions != 0 ||
            sequence.num_stop_actions != 0) {
            m_plan.diagnostics.push_back(
                "cue sequence action tracks are preserved by the graph but are not "
                "executed by the static renderer");
        }
        for (const auto track_index : sequence.track_indices) {
            auto result = append_track(track_index, start_time_us, clips);
            if (!result) return result;
        }
        return {};
    }

    uint64_t inferred_duration_us(std::span<const AcbCueClipPlan> clips) const {
        uint64_t duration = 0;
        for (const auto& clip : clips) {
            if (clip.waveform_index >= m_graph.waveforms().size()) continue;
            const auto& waveform = m_graph.waveforms()[clip.waveform_index];
            if (waveform.sampling_rate == 0 || clip.start_time_us < 0) continue;
            const uint64_t audio_us =
                (static_cast<uint64_t>(waveform.num_samples) * 1'000'000 +
                 waveform.sampling_rate - 1) /
                waveform.sampling_rate;
            duration = std::max(
                duration, static_cast<uint64_t>(clip.start_time_us) + audio_us);
        }
        return duration;
    }

    const AcbCueGraph& m_graph;
    uint32_t m_cue_index;
    const AcbCueRenderOptions& m_options;
    AcbCuePlaybackPlan m_plan;
    std::set<ReferenceKey> m_active;
};

struct DecodedWaveform {
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    std::vector<int16_t> pcm;
};

uint64_t frames_for_us(uint64_t time_us, uint32_t sample_rate) {
    return (time_us * sample_rate + 500'000) / 1'000'000;
}

int16_t saturate_sample(int64_t sample) noexcept {
    return static_cast<int16_t>(std::clamp<int64_t>(
        sample,
        std::numeric_limits<int16_t>::min(),
        std::numeric_limits<int16_t>::max()));
}

std::string safe_cue_name(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (const unsigned char ch : name) {
        if (ch < 0x20 || ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            result.push_back('_');
        } else {
            result.push_back(static_cast<char>(ch));
        }
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
        result.pop_back();
    }
    return result.empty() ? "cue" : result;
}

} // namespace

std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbCueGraph& graph,
    uint32_t cue_index,
    const AcbCueRenderOptions& options) {
    return PlaybackPlanner(graph, cue_index, options).build();
}

std::expected<AcbCuePlaybackPlan, std::string> plan_cue_playback(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueRenderOptions& options) {
    auto plan = plan_cue_playback(acb.cue_graph(), cue_index, options);
    if (!plan) {
        return std::unexpected(plan.error());
    }

    if (!acb.has_embedded_awb() && !acb.companion_awb_path()) {
        return plan;
    }

    std::map<uint32_t, WaveformAwbEntry> resolved;
    std::set<uint32_t> attempted;
    for (auto& block : plan->blocks) {
        for (auto& clip : block.clips) {
            if (attempted.insert(clip.waveform_index).second) {
                auto entry = acb.waveform_awb_entry(clip.waveform_index);
                if (!entry) {
                    plan->diagnostics.push_back(
                        "waveform " + std::to_string(clip.waveform_index) +
                        " AWB provenance is unresolved: " + entry.error());
                } else {
                    resolved.emplace(clip.waveform_index, *entry);
                }
            }
            const auto entry = resolved.find(clip.waveform_index);
            if (entry == resolved.end()) {
                continue;
            }
            clip.awb_wave_id = entry->second.wave_id;
            clip.awb_stream_index = entry->second.awb_index;
            clip.awb_bank = entry->second.stream_bank
                ? AcbCueAwbBank::stream
                : AcbCueAwbBank::memory;
        }
    }
    return plan;
}

std::expected<AcbRenderedCue, std::string> render_cue(
    const AcbContainer& acb,
    uint32_t cue_index,
    const AcbCueRenderOptions& options) {
    auto plan = plan_cue_playback(acb, cue_index, options);
    if (!plan) return std::unexpected(plan.error());

    uint16_t hca_subkey = options.hca_subkey.value_or(0);
    if (!options.hca_subkey) {
        auto subkey = acb.awb_subkey();
        if (!subkey) return std::unexpected(subkey.error());
        hca_subkey = *subkey;
    }

    std::map<uint32_t, DecodedWaveform> decoded;
    const auto decode_waveform = [&](uint32_t waveform_index)
        -> std::expected<std::reference_wrapper<const DecodedWaveform>, std::string> {
        if (const auto it = decoded.find(waveform_index); it != decoded.end()) {
            return std::cref(it->second);
        }
        auto codec = acb.waveform_codec(waveform_index);
        if (!codec) return std::unexpected(codec.error());
        if (*codec != awb::EntryCodec::Hca && *codec != awb::EntryCodec::Adx) {
            return std::unexpected(
                "ACB cue render failed: waveform " +
                std::to_string(waveform_index) + " uses unsupported codec `" +
                std::string(awb::entry_codec_name(*codec)) + "`");
        }
        auto data = acb.extract_waveform_data(waveform_index);
        if (!data) {
            return std::unexpected(
                "ACB cue render failed for waveform " +
                std::to_string(waveform_index) + ": " + data.error());
        }

        DecodedWaveform waveform;
        switch (*codec) {
            case awb::EntryCodec::Hca: {
                auto audio = hca::Hca::load(*data);
                if (!audio) return std::unexpected(audio.error());
                auto pcm = audio->decode(options.hca_keycode, hca_subkey);
                if (!pcm) return std::unexpected(pcm.error());
                waveform.sample_rate = audio->header().fmt.sample_rate;
                waveform.channels = audio->header().fmt.channel_count;
                waveform.pcm = std::move(*pcm);
                break;
            }
            case awb::EntryCodec::Adx: {
                auto audio = adx::Adx::load(*data);
                if (!audio) return std::unexpected(audio.error());
                auto pcm = audio->decode();
                if (!pcm) return std::unexpected(pcm.error());
                waveform.sample_rate = pcm->sample_rate;
                waveform.channels = pcm->channels;
                waveform.pcm = std::move(pcm->pcm_data);
                break;
            }
            default:
                std::unreachable();
        }
        const auto [it, inserted] = decoded.emplace(
            waveform_index, std::move(waveform));
        (void)inserted;
        return std::cref(it->second);
    };

    uint32_t output_rate = 0;
    uint8_t output_channels = 0;
    std::vector<int16_t> output;
    for (const auto& block : plan->blocks) {
        for (const auto& clip : block.clips) {
            auto waveform = decode_waveform(clip.waveform_index);
            if (!waveform) return std::unexpected(waveform.error());
            const auto& audio = waveform->get();
            if (output_rate == 0) {
                output_rate = audio.sample_rate;
                output_channels = audio.channels;
            } else if (
                audio.sample_rate != output_rate ||
                audio.channels != output_channels) {
                return std::unexpected(
                    "ACB cue render failed: mixed sample rates or channel counts "
                    "require resampling, which is not implemented");
            }
        }
    }
    if (output_rate == 0 || output_channels == 0) {
        return std::unexpected("ACB cue render failed: cue produced no decodable audio");
    }

    for (const auto& block : plan->blocks) {
        if (block.skipped_empty_hold) {
            continue;
        }
        uint64_t block_frames = 0;
        for (const auto& clip : block.clips) {
            auto waveform = decode_waveform(clip.waveform_index);
            if (!waveform) return std::unexpected(waveform.error());
            const auto& audio = waveform->get();
            const uint64_t start_frame =
                frames_for_us(static_cast<uint64_t>(clip.start_time_us), output_rate);
            block_frames = std::max(
                block_frames,
                start_frame + audio.pcm.size() / output_channels);
        }
        if (block.duration_us != 0) {
            block_frames = frames_for_us(block.duration_us, output_rate);
        }
        if (block_frames >
            std::numeric_limits<size_t>::max() / std::max<uint8_t>(output_channels, 1)) {
            return std::unexpected("ACB cue render failed: block PCM size overflows");
        }

        std::vector<int64_t> mixed(
            static_cast<size_t>(block_frames) * output_channels, 0);
        for (const auto& clip : block.clips) {
            auto waveform = decode_waveform(clip.waveform_index);
            if (!waveform) return std::unexpected(waveform.error());
            const auto& audio = waveform->get();
            const size_t start_sample = static_cast<size_t>(
                frames_for_us(static_cast<uint64_t>(clip.start_time_us), output_rate) *
                output_channels);
            const size_t count = std::min(
                audio.pcm.size(), mixed.size() - std::min(start_sample, mixed.size()));
            for (size_t sample = 0; sample < count; ++sample) {
                mixed[start_sample + sample] += audio.pcm[sample];
            }
        }

        std::vector<int16_t> rendered_block;
        rendered_block.reserve(mixed.size());
        for (const auto sample : mixed) {
            rendered_block.push_back(saturate_sample(sample));
        }
        const uint64_t total_plays =
            static_cast<uint64_t>(block.render_loop_count) + 1;
        if (
            rendered_block.size() >
                (std::numeric_limits<size_t>::max() - output.size()) /
                    total_plays) {
            return std::unexpected("ACB cue render failed: output PCM size overflows");
        }
        output.reserve(output.size() + rendered_block.size() * total_plays);
        for (uint64_t play = 0; play < total_plays; ++play) {
            output.insert(output.end(), rendered_block.begin(), rendered_block.end());
        }
    }

    plan->diagnostics.push_back(
        "waveform gain, envelopes, transition curves, and runtime selector/action "
        "changes are not applied by the static renderer");
    return AcbRenderedCue{
        .plan = std::move(*plan),
        .sample_rate = output_rate,
        .channels = output_channels,
        .pcm = std::move(output),
    };
}

std::expected<void, std::string> extract_cue(
    const AcbContainer& acb,
    uint32_t cue_index,
    const std::filesystem::path& output_path,
    const AcbCueRenderOptions& options) {
    auto rendered = render_cue(acb, cue_index, options);
    if (!rendered) return std::unexpected(rendered.error());
    return wav::WavContainer::write(
        output_path.string(),
        rendered->pcm,
        rendered->sample_rate,
        rendered->channels);
}

std::string cue_filename(
    const AcbContainer& acb,
    uint32_t cue_index,
    bool include_index_prefix) {
    std::string result;
    if (include_index_prefix) {
        result = std::to_string(cue_index + 1);
        result += '_';
    }
    result += safe_cue_name(acb.cue_graph().cue_name(cue_index));
    result += ".wav";
    return result;
}

} // namespace cricodecs::acb
