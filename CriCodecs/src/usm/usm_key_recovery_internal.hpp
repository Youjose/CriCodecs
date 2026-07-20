#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace cricodecs::usm {

class UsmReader;

struct AudioKeyGuess {
    uint64_t key = 0;
    float score = 0.0f;
    size_t audio_chunks = 0;
    bool used_zero_window = false;
};

[[nodiscard]] std::optional<AudioKeyGuess> recover_adx_audio_key(const UsmReader& source);

} // namespace cricodecs::usm
