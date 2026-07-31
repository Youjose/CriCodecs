#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

namespace {

[[nodiscard]] constexpr std::string_view listing_mode_key(
    OutputListingMode mode) noexcept {
    switch (mode) {
        case OutputListingMode::entries:
            return "entries";
        case OutputListingMode::cue_plans:
            return "cue_plans";
    }
    return "entries";
}

[[nodiscard]] constexpr std::string_view choice_domain_key(
    acb::AcbCueChoiceDomain domain) noexcept {
    switch (domain) {
        case acb::AcbCueChoiceDomain::sequence_track:
            return "sequence_track";
        case acb::AcbCueChoiceDomain::synth_reference:
            return "synth_reference";
    }
    return "sequence_track";
}

[[nodiscard]] constexpr std::string_view awb_bank_key(
    acb::AcbCueAwbBank bank) noexcept {
    switch (bank) {
        case acb::AcbCueAwbBank::memory:
            return "memory";
        case acb::AcbCueAwbBank::stream:
            return "stream";
    }
    return "memory";
}

template <typename T>
void print_optional_integer(std::ostream& out, const std::optional<T>& value) {
    if (value) {
        out << *value;
    } else {
        out << "null";
    }
}

void print_u32_array(
    std::ostream& out,
    std::span<const uint32_t> values) {
    out << '[';
    for (size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << values[index];
    }
    out << ']';
}

void print_choice(
    std::ostream& out,
    const acb::AcbCueChoiceSelection& choice) {
    out << "{\"domain\":" << quote_json(choice_domain_key(choice.domain))
        << ",\"node_index\":" << choice.node_index
        << ",\"occurrence\":" << choice.occurrence
        << ",\"option_index\":" << choice.option_index
        << ",\"mode\":" << static_cast<uint32_t>(choice.mode)
        << ",\"selector_name\":" << quote_json(choice.selector_name)
        << ",\"selector_value\":" << quote_json(choice.selector_value)
        << '}';
}

void print_choice_paths(
    std::ostream& out,
    std::span<const std::vector<acb::AcbCueChoiceSelection>> paths) {
    out << '[';
    for (size_t path_index = 0; path_index < paths.size(); ++path_index) {
        if (path_index != 0) {
            out << ',';
        }
        out << '[';
        for (size_t choice_index = 0;
             choice_index < paths[path_index].size();
             ++choice_index) {
            if (choice_index != 0) {
                out << ',';
            }
            print_choice(out, paths[path_index][choice_index]);
        }
        out << ']';
    }
    out << ']';
}

void print_selector_values(
    std::ostream& out,
    std::span<const acb::AcbCueSelectorValue> selectors) {
    out << '[';
    for (size_t index = 0; index < selectors.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << "{\"name\":" << quote_json(selectors[index].name)
            << ",\"value\":" << quote_json(selectors[index].value)
            << '}';
    }
    out << ']';
}

void print_cue_sources(
    std::ostream& out,
    std::span<const acb::AcbCuePlanSource> sources) {
    out << '[';
    for (size_t index = 0; index < sources.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto& source = sources[index];
        out << "{\"source_cue_index\":" << source.source_cue_index
            << ",\"source_cue_id\":" << source.source_cue_id
            << ",\"source_cue_name\":"
            << quote_json(source.source_cue_name)
            << ",\"terminal_cue_index\":" << source.terminal_cue_index
            << ",\"action_cue_chain\":";
        print_u32_array(out, source.action_cue_chain);
        out << ",\"selectors\":";
        print_selector_values(out, source.selector_values);
        out << ",\"paths\":";
        print_choice_paths(out, source.paths);
        out << '}';
    }
    out << ']';
}

void print_cue_clip(
    std::ostream& out,
    const acb::AcbCueClipPlan& clip) {
    out << "{\"waveform_index\":" << clip.waveform_index
        << ",\"awb_wave_id\":";
    print_optional_integer(out, clip.awb_wave_id);
    out << ",\"awb_stream_index\":";
    print_optional_integer(out, clip.awb_stream_index);
    out << ",\"awb_bank\":";
    if (clip.awb_bank) {
        out << quote_json(awb_bank_key(*clip.awb_bank));
    } else {
        out << "null";
    }
    out << ",\"start_time_us\":" << clip.start_time_us << '}';
}

void print_cue_block(
    std::ostream& out,
    const acb::AcbCueBlockPlan& block) {
    out << "{\"block_position\":";
    print_optional_integer(out, block.block_position);
    out << ",\"block_index\":";
    print_optional_integer(out, block.block_index);
    out << ",\"name\":" << quote_json(block.name)
        << ",\"authored_loop_count\":" << block.authored_loop_count
        << ",\"render_loop_count\":" << block.render_loop_count
        << ",\"forced_advance\":" << bool_text(block.forced_advance)
        << ",\"skipped_empty_hold\":" << bool_text(block.skipped_empty_hold)
        << ",\"clips\":[";
    for (size_t index = 0; index < block.clips.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        print_cue_clip(out, block.clips[index]);
    }
    out << "],\"duration_us\":" << block.duration_us << '}';
}

void print_cue_plan(
    std::ostream& out,
    const acb::AcbCuePlaybackPlan& plan,
    bool detailed) {
    out << "\"cue_index\":" << plan.cue_index
        << ",\"cue_id\":" << plan.cue_id
        << ",\"cue_name\":" << quote_json(plan.cue_name);
    if (!detailed) {
        return;
    }
    out << ",\"blocks\":[";
    for (size_t index = 0; index < plan.blocks.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        print_cue_block(out, plan.blocks[index]);
    }
    out << ']';
}

void print_listing_item(
    std::ostream& out,
    const OutputItem& item) {
    out << "{\"index\":" << item.index
        << ",\"name\":" << quote_json(item.entry_name.generic_string())
        << ",\"path\":" << quote_json(item.relative_path.generic_string());
    if (item.cue_plan) {
        out << ',';
        print_cue_plan(out, *item.cue_plan, item.detailed);
        out << ",\"sources\":";
        print_cue_sources(out, item.cue_sources);
    }
    out << '}';
}

} // namespace

void print_item_list(std::ostream& out, const OutputListing& listing) {
    for (const auto& item : listing.items) {
        out << item.index << ": " << item.relative_path.generic_string() << '\n';
        for (const auto& detail : item.details) {
            out << detail << '\n';
        }
    }
}

void print_item_list_json(
    std::ostream& out,
    const OutputListing& listing) {
    out << "{\"format\":" << quote_json(format_key(listing.format))
        << ",\"mode\":" << quote_json(listing_mode_key(listing.mode))
        << ",\"raw\":" << bool_text(listing.raw)
        << ",\"item_count\":" << listing.items.size();
    if (listing.acb_cues) {
        out << ",\"authored_cue_count\":"
            << listing.acb_cues->authored_cue_count
            << ",\"non_playable_cues\":";
        print_u32_array(out, listing.acb_cues->non_playable_cues);
    }
    out << ",\"items\":[";
    for (size_t index = 0; index < listing.items.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        print_listing_item(out, listing.items[index]);
    }
    out << "]}";
}

} // namespace cricodecs::cli::detail
