#include "shared/i18n.hpp"
#include "modules/wav/wav_browse.hpp"

#include "shared/document_helpers.hpp"

namespace cristudio::modules::wav {

LoadedDocument summarize(const std::filesystem::path& path, const cricodecs::wav::WavContainer& wav) {
    auto doc = base_document(path, cristudio::i18n::translate_utf8("Wav.WavBrowse", "WAV audio"));
    const auto& format = wav.format();
    doc.info.push_back({cristudio::i18n::translate_utf8("Wav.WavBrowse", "Channels"), number(format.channels)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Wav.WavBrowse", "Sample rate"), number(format.sample_rate)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Wav.WavBrowse", "Bit depth"), number(format.bit_depth)});
    doc.info.push_back({cristudio::i18n::translate_utf8("Wav.WavBrowse", "Samples"), number(wav.sample_count())});
    doc.info.push_back({cristudio::i18n::translate_utf8("Wav.WavBrowse", "Loops"), bool_text(wav.has_loops())});
    return doc;
}

} // namespace cristudio::modules::wav
