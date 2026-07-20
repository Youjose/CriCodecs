#include "modules/wav/wav_browse.hpp"

#include "shared/document_helpers.hpp"

namespace cristudio::modules::wav {

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::wav::WavContainer& wav) {
    auto doc = base_document(path, "WAV audio");
    const auto& format = wav.format();
    doc.info.push_back({"Channels", number(format.channels)});
    doc.info.push_back({"Sample rate", number(format.sample_rate)});
    doc.info.push_back({"Bit depth", number(format.bit_depth)});
    doc.info.push_back({"Samples", number(wav.sample_count())});
    doc.info.push_back({"Loops", bool_text(wav.has_loops())});
    return doc;
}

} // namespace cristudio::modules::wav
