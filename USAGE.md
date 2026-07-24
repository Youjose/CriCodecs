# CriCodecs usage

This document covers the CriCodecs command-line interface, Python package, and
C++ SDK. CriStudio is the recommended interface for interactive desktop use;
its menus, browser, editor, and build dialogs expose the same native format
implementations.

## Command-line interface

The CLI is input-driven. With no explicit operation, audio files decode to WAV
and archives or stream containers extract to a sibling directory.

```sh
cricodecs music.hca
cricodecs archive.cpk
cricodecs movie.usm -o movie_streams
```

Use `-f` only when automatic format detection is ambiguous or when selecting a
key-recovery domain.

### Inspect and extract

```sh
# Human-readable metadata
cricodecs -m archive.cpk

# Machine-readable metadata
cricodecs -m --json movie.usm

# List entries without extracting
cricodecs --list archive.cpk

# Extract raw encoded payloads rather than decoding audio
cricodecs --raw archive.cpk -o raw_entries

# Extract selected entry indices
cricodecs archive.cpk --index 2 --index 7 -o selected
```

Output templates may use `?i` for an entry index, `?e` for an entry filename,
and `?s` for the input filename.

### Encode audio

WAV input can be encoded as HCA, ADX, or AHX:

```sh
cricodecs --encode -f hca input.wav -o output.hca
cricodecs --encode -f adx input.wav -o output.adx
cricodecs --encode -f ahx input.wav -o output.ahx

# Select HCA encoder behavior explicitly.
cricodecs --encode -f hca input.wav -o output.hca \
  --header-version 3.00 --quality low --bitrate 192000 --ms-stereo

# ADX supports modes 2/3/4 and header versions 3/4/5.
cricodecs --encode -f adx input.wav -o output.adx \
  --mode 4 --header-version 3 --highpass 500 --trim-after-loop

# AHX supports modes 0x10/0x11 and the built-in allocation profiles.
cricodecs --encode -f ahx input.wav -o output.ahx \
  --mode 0x11 --profile 22050
```

Format-specific profiles, header versions, keys, subkeys, and cipher types are
selected with `--profile`, `--header-version`, `--key`, `--subkey`, and
`--cipher-type` where supported. Run `cricodecs --help` for the authoritative
option list in the installed build. HCA encoding also preserves the first valid
sampler loop declared by the input WAV.

### Build archives and stream containers

Directory-driven builders support AFS, AWB, CPK, ACX, CSB, and CVM. USM and
SFD accept prepared video plus supported audio inputs.

```sh
cricodecs --build -f cpk input_directory -o archive.cpk
cricodecs --build -f awb waveforms -o sound.awb
cricodecs --build -f awb waveforms -o sound.awb \
  --header-version 2 --alignment 32 --subkey 0
cricodecs --build -f cpk input_directory -o archive.cpk \
  --profile filename-id --alignment 2048

cricodecs --build -f usm movie.ivf \
  --audio dialogue.adx \
  --audio music.hca \
  -o movie.usm

cricodecs --build -f sfd movie.mpg \
  --audio soundtrack.adx \
  -o movie.sfd
```

Supplying `--key` while building USM masks video and ADX audio. Plain HCA is
converted to cipher type 56, while already-encrypted HCA is preserved.

### Mutate archives

Mutation options operate on formats that expose the corresponding native
operation:

```sh
cricodecs archive.cpk --add local.bin=data/local.bin -o updated.cpk
cricodecs archive.cpk --replace data/old.bin=replacement.bin -o updated.cpk
cricodecs archive.cpk --rename data/old.bin=data/new.bin -o updated.cpk
cricodecs archive.cpk --remove data/unused.bin -o updated.cpk
cricodecs archive.cpk --move 4=1 -o updated.cpk
```

Use `--compress` to compress added or replaced CPK payloads. Mutation targets
may be indices or archive paths depending on the format.

### Recover keys

Recovery always reports ranked candidates and their scores. It does not apply a
candidate globally or rewrite the input.

```sh
# HCA type-56 effective keys
cricodecs --recover-key -f hca music.hca
cricodecs --recover-key -f hca bank.acb --json
cricodecs --recover-key -f hca same_key_folder

# USM video/audio mask keys
cricodecs --recover-key -f usm movie.usm

# ADX/AHX encryption triplets
cricodecs --recover-key -f adx music.adx
cricodecs --recover-key -f ahx voice_a.ahx voice_b.ahx

# CRI AAC keys in AWB/ACB banks
cricodecs --recover-key -f awb BGM.awb
cricodecs --recover-key -f acb BGM.acb
```

Multiple inputs are assumed to share one base key by default. Add
`--independent` when each input may use a different key. HCA recovery discovers
eligible streams inside HCA, AWB, ACB, USM, and recursively scanned folders.
USM recovery can aggregate supported video evidence, masked ADX evidence, and
encrypted HCA evidence.

Scores are comparable within a recovery domain, but they do not all express
the same codec-specific structural test. Prefer the highest-ranked candidate
and validate it against representative files before bulk application.

HCA type-56 recovery returns a canonical value for bits 0 through 55. The
cipher table does not represent bits 56 through 63, so no HCA data can identify
the original caller key's upper byte. `unknown_high_bits` is separate: it
reports unresolved bits within the observable low 56 after AWB-subkey
normalization.

As an optional application-level heuristic, search unencrypted metadata or
executables for the recovered seven bytes. In a little-endian stored 64-bit
key, the following byte is the unknown upper byte; in big-endian storage, it is
the preceding byte. A textual 16-digit hexadecimal key may contain the
recovered 14 digits as its suffix. A match is not proof: the original key may
be derived, obfuscated, split, or absent. This search applies to HCA/USM
keycodes and not to ADX/AHX triplets or other recovery domains.

## Python package

Install the published package with:

```sh
python -m pip install cricodecs
```

### Detect and inspect

`cricodecs.load()` accepts a filesystem path or bytes and returns the matching
format object.

```python
import cricodecs

document = cricodecs.load("archive.cpk")
print(type(document))
print(document)
```

Format modules expose focused `load`, `decode`, `encode`, `extract`, `build`,
`mux`, or `recover_key` functions according to their native capabilities.

### Decode and encode audio

```python
from pathlib import Path
from cricodecs import adx, hca

Path("music.wav").write_bytes(adx.decode("music.adx"))

adx_bytes = adx.encode(Path("source.wav").read_bytes())
Path("encoded.adx").write_bytes(adx_bytes)

hca_config = hca.HcaEncodeConfig()
hca_config.sample_rate = 48_000
hca_config.channel_count = 2
hca_config.quality = hca.HcaQuality.HIGH

hca_bytes = hca.encode(Path("source.wav").read_bytes(), hca_config)
Path("encoded.hca").write_bytes(hca_bytes)
```

Keys may be supplied directly to decode/encrypt helpers:

```python
from cricodecs import hca

wav = hca.decode("encrypted.hca", keycode=0xCF222F1FE0748978)
encrypted = hca.encrypt(
    "plain.hca",
    cipher_type=56,
    keycode=0xCF222F1FE0748978,
)
```

### Work with archives

```python
from pathlib import Path
from cricodecs import cpk

archive = cpk.load("input.cpk")
print(archive.info())
archive.extract("input_extracted")

created = cpk.create(cpk.CpkPreset.FILENAME)
created.add_file("voice.adx", "audio/voice.adx")
created.add_bytes(b"hello\n", "docs/readme.txt")
Path("output.cpk").write_bytes(created.save_bytes())
```

Loaded mutable container objects preserve inspectable state. Prefer their
`save_bytes()` or file-output methods after mutation rather than reconstructing
private table structures yourself.

### Mux USM

```python
from cricodecs import usm

config = usm.UsmMuxConfig(
    video_path="movie.264",
    audio_tracks=[usm.UsmMuxAudioTrack("movie.adx")],
    subtitle_tracks=[
        usm.UsmMuxSubtitleTrack(
            "subtitles_en.srt",
            language_id=0,
            format=usm.UsmSubtitleFormat.SRT,
        ),
        usm.UsmMuxSubtitleTrack(
            "subtitles_alt.ass",
            language_id=1,
            format=usm.UsmSubtitleFormat.ASS,
        ),
    ],
)
usm.mux(config, "movie_with_subtitles.usm")
```

SBT language IDs are numeric subtitle slots; the available SDK documentation
does not define a universal locale mapping. ASS imports can preserve per-cue
IDs from Dialogue names such as `lang5`.

### Recover keys

```python
from cricodecs import adx, hca, usm

hca_result = hca.recover_key(
    ["voice_01.hca", "voice_02.hca"],
    same_base_key=True,
)
print(hca_result.candidates[0])

adx_result = adx.recover_key("music.adx")
movie_result = usm.recover_key("movie.usm")
```

Recovery result objects expose a bounded `candidates` sequence plus source and
evidence counts. Set `same_base_key=False` when the supplied inputs should be
recovered independently.

## C++ SDK

The SDK requires C++23. Install either the static or shared release package,
then consume its CMake target:

```cmake
find_package(CriCodecs CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE CriCodecs::CriCodecs)
```

The umbrella header exposes the complete public surface:

```cpp
#include <cricodecs/cricodecs.hpp>
```

Individual headers under `<cricodecs/<format>/...>` avoid pulling unrelated
formats into a translation unit.

### Error handling

Recoverable native operations return `std::expected<T, std::string>`. Check the
result before using it and preserve the returned error text when reporting a
failure:

```cpp
#include <iostream>
#include <cricodecs/adx/adx_codec.hpp>

int main() {
    auto input = cricodecs::adx::Adx::load("input.adx");
    if (!input) {
        std::cerr << input.error() << '\n';
        return 1;
    }

    auto decoded = input->decode();
    if (!decoded) {
        std::cerr << decoded.error() << '\n';
        return 1;
    }

    std::cout << decoded->sample_rate << " Hz\n";
}
```

### Load an archive

```cpp
#include <cricodecs/cpk/cpk_container.hpp>

auto archive = cricodecs::cpk::Cpk::load("input.cpk");
if (!archive) {
    return 1;
}

for (const auto& file : archive->files()) {
    // Inspect entry metadata without materializing every payload.
}
```

Filesystem and byte-span overloads are available where the format supports
both workflows. Keep large archive browsing metadata-first and request entry
bytes only for preview, extraction, or mutation.

## Build from source

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCRICODECS_BUILD_TESTS=OFF \
  -DCRICODECS_INSTALL_CPP=ON
cmake --build build --parallel 4
cmake --install build --prefix ./install
```

Useful build switches are:

- `CRICODECS_BUILD_CLI`
- `CRICODECS_BUILD_PYTHON`
- `CRICODECS_BUILD_CRISTUDIO`
- `CRICODECS_INSTALL_CPP`
- `BUILD_SHARED_LIBS`

Use a fresh build directory after changing the compiler, generator, Python
version, architecture, or shared/static linkage.
