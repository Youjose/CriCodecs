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

### Inspect and render ACB cues

ACB files have two useful extraction views:

- the default flat view extracts each ACB `WaveformTable` row with its
  preferred cue-derived name (several rows may reuse one physical AWB asset);
- `--cue` resolves authored sequences, selector alternatives, action-start
  chains, blocks, scheduled clips, and static loop policy into unique playable
  cue plans.

```sh
# Flat waveform view.
cricodecs --list Music.acb
cricodecs Music.acb -o Music_waveforms

# Resolved cue-plan view.
cricodecs --cue --list Music.acb

# Show blocks, clips, timings, AWB IDs, and physical AWB stream indexes for
# zero-based resolved-plan index 3.
cricodecs --cue --list --index 3 Music.acb

# Render all unique static plans, or only plan index 3.
cricodecs --cue Music.acb -o Music_cues
cricodecs --cue --index 3 Music.acb -o Music_cues
```

The number before each `--cue --list` entry is its zero-based resolved-plan
index. The numeric filename prefix is one-based and exists only to keep bulk
output ordered. Equivalent plans reached through several cue names/actions
share one output; distinct plans with the same terminal cue name use selector
labels such as `__Music_GameState-Scene`, with `__variant-N` as the fallback
when authored choices have no usable label.

Infinite ACB blocks have no natural duration when exported outside the game.
The default renders the initial play with zero repeats and advances. Empty
infinite holds are included once but are not affected by the global loop count:

```sh
# Add two repeats to every audio-bearing infinite block.
cricodecs --cue --cue-loop-count 2 Music.acb -o Music_cues

# Override one zero-based block position on selected plan 3.
cricodecs --cue --index 3 --cue-block-loop-count 3=4 \
  Music.acb -o Music_cues

# Stop after the first rendered infinite block.
cricodecs --cue --cue-stop-at-loop Music.acb -o Music_cues

# Omit waveform-less infinite holding blocks.
cricodecs --cue --cue-skip-empty-holds Music.acb -o Music_cues
```

This is a static projection, not a full CRI Atom runtime. Live game-variable
changes, event-driven block transitions, multi-ACB outside links, gains,
envelopes, transition curves, and resampling are not currently emulated.

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

### Inspect and render ACB cue graphs

An `acb.Acb` keeps both the flat waveform view and the authored cue graph.
Graph indexes are zero-based table-row indexes; an authored cue ID is a
different value and should be converted explicitly:

```python
from cricodecs import acb

bank = acb.load("Music.acb")
graph = bank.graph

print(bank.waveform_count)
print(graph.cue_count, graph.waveform_count)
print(graph.sequences[0])
print(graph.track_events[0])

cue_index = next(
    row.cue_index
    for row in graph.cue_names
    if row.name == "Music_Chapter01_Village_Scene"
)
cue_id = graph.cue(cue_index).cue_id
assert graph.cue_index_by_id(cue_id) == cue_index
```

The graph exposes cues, names, sequences, tracks, action tracks, synths,
block sequences, blocks, waveforms, strings, outside links, command streams,
diagnostics, and per-cue node/edge assemblies:

```python
assembly = graph.assemble_cue(cue_index)
print(assembly.nodes)
print(assembly.edges)
print(assembly.unresolved)
```

Named selector alternatives can be inspected before choosing a plan:

```python
print(bank.selector_options(cue_index))
# {'Music_GameState': ['Battle', 'Scene']}

resolution = bank.resolve_cue(cue_index)
print(resolution.plan_count)       # 2
print(resolution.filenames())

for resolved in resolution.plans:
    print(resolved.plan)
    for source in resolved.sources:
        print(source.source_cue_name)
        print(source.selector_values)
        print(source.action_cue_chain)
        print(source.paths)
```

`cue_plan()` returns a plan directly only when the selected cue resolves to one
unique audio result. Supply named selectors when available:

```python
plan = bank.cue_plan(
    cue_index,
    selectors={"Music_GameState": "Scene"},
)

# The cue-ID form is explicit rather than treating IDs and indexes as the same.
same_plan = bank.cue_plan_by_id(
    cue_id,
    selectors={"Music_GameState": "Scene"},
)
```

For random, sequential, or otherwise unlabeled alternatives, inspect
`resolve_cue()` and select a zero-based variant:

```python
resolution = bank.resolve_cue(cue_index)
plan = bank.cue_plan(cue_index, variant=0)
```

Calling `cue_plan(cue_index)` without selectors or a variant raises
`ValueError` when several unique plans remain. Control-only cues can resolve to
no playable plan; inspect `non_playable_cues` and `diagnostics` on the
resolution instead of assuming every cue contains audio.

Plans expose their authored blocks and scheduled waveform clips:

```python
for block in plan.blocks:
    print(
        block.block_position,
        block.name,
        block.duration_us,
        block.authored_loop_count,
        block.render_loop_count,
    )
    for clip in block.clips:
        print(
            clip.waveform_index,
            clip.start_time_us,
            clip.awb_wave_id,
            clip.awb_stream_index,
            clip.awb_bank,
        )
```

`awb_wave_id` and `awb_bank` come from the ACB and remain available when the
ACB was loaded from bytes without an AWB. `awb_stream_index` is the zero-based
physical file index and is available only when an embedded or companion AWB
can be resolved.

Render or export a chosen static plan against its source ACB:

```python
from pathlib import Path

Path("village_scene.wav").write_bytes(plan.wav_bytes(bank))
plan.export(bank, "village_scene.wav")
```

The plan takes the `Acb` explicitly and does not retain a hidden reference to
it. HCA key parameters are accepted by both plan audio methods when needed.

Resolve or export the whole sheet with the same semantic deduplication and
selector-aware filenames as the CLI:

```python
sheet = bank.resolve_cues()
print(sheet.plan_count)
print(sheet.non_playable_cues)
print(sheet.filenames())

written_paths = bank.extract_cues("Music_cues")
```

Looping is expressed as repeats after the initial block play. The defaults are
zero repeats, advance after an infinite audio block, and include empty
infinite holds once:

```python
plan = bank.cue_plan(
    cue_index,
    selectors={"Music_GameState": "Scene"},
    loop_count=2,
    advance_after_infinite=False,
    include_empty_holds=False,
    block_loop_counts={3: 4},
)
```

As with CLI cue export, these are static paths. They do not emulate live
game-variable changes, event-driven transitions, or external ACB loading.

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
