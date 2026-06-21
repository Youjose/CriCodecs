# CriCodecs

CriCodecs is a modern C++23 implementation of CRI codec and container formats
with Python bindings built on nanobind.

The public branch ships the core C++ source tree and the Python binding package.

## Installation

### Requirements

- CMake 4.2 or newer
- A recent C++23 compiler
  - GCC 16.1 or newer on Linux
  - Visual Studio 2026 / MSVC v145 or newer on Windows
  - A current Apple Clang/Xcode toolchain on macOS
- Python 3.9 or newer for Python bindings

Use a fresh build directory for each generator and compiler. If you change
compilers, generators, Python versions, or CMake versions, delete the old build
directory first.

### Python From Source

From the repository root:

```sh
python -m pip install .
```

For verbose build logs:

```sh
python -m pip install -v .
```

For local development with an already prepared build environment:

```sh
python -m pip install --no-build-isolation -ve .
```

### Python From A Wheel

Download a wheel matching your Python version, operating system, and CPU
architecture, then install it directly:

```sh
python -m pip install cricodecs-0.0.1b0-*.whl
```

### C++ Core Library

Build only the C++ library:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCRICODECS_BUILD_PYTHON=OFF
cmake --build build --parallel
```

Build the C++ library and Python bindings with CMake:

```sh
cmake -S . -B build-python -G Ninja -DCMAKE_BUILD_TYPE=Release -DCRICODECS_BUILD_PYTHON=ON
cmake --build build-python --parallel
```

On Windows with Visual Studio 2026:

```sh
cmake -S . -B build-vs -G "Visual Studio 18 2026" -A x64 -DCRICODECS_BUILD_PYTHON=OFF
cmake --build build-vs --config Release --parallel
```

If the Visual Studio generator is missing, update CMake first and run
`cmake -G` to list the generators available on your machine.

## Python Usage

Import the package as `cricodecs`. The top-level `cricodecs.load()` helper
detects supported formats from a path or bytes and returns the matching object.
Individual modules also expose format-specific helpers such as `load()`,
`decode()`, `encode()`, `extract()`, `demux()`, or `save_bytes()`.

### Top-Level Load

```python
import cricodecs

obj = cricodecs.load("input.cpk")
print(type(obj))
print(repr(obj))
```

### ADX Decode And Encode

```python
from pathlib import Path

from cricodecs import adx

wav_bytes = adx.decode("input.adx")
Path("output.wav").write_bytes(wav_bytes)

encoded = adx.encode(Path("source.wav").read_bytes())
Path("output.adx").write_bytes(encoded)
```

### HCA Decode, Encode, Encrypt

```python
from pathlib import Path

from cricodecs import hca

wav_bytes = hca.decode("input.hca", keycode=0xCF222F1FE0748978)
Path("decoded.wav").write_bytes(wav_bytes)

config = hca.HcaEncodeConfig()
config.sample_rate = 48000
config.channel_count = 2
config.quality = hca.HcaQuality.HIGH

hca_bytes = hca.encode(Path("source.wav").read_bytes(), config)
Path("encoded.hca").write_bytes(hca_bytes)

encrypted = hca.encrypt(hca_bytes, cipher_type=56, keycode=0xCF222F1FE0748978)
Path("encrypted.hca").write_bytes(encrypted)
```

### CPK Inspect, Extract, Build

```python
from pathlib import Path

from cricodecs import cpk

archive = cpk.load("input.cpk")
print(archive.info())
archive.extract("input_extracted")

new_archive = cpk.create(cpk.CpkPreset.FILENAME)
new_archive.add_file("data/voice.adx", "voice/voice.adx")
new_archive.add_bytes(b"hello", "text/readme.txt")
Path("output.cpk").write_bytes(new_archive.save_bytes())
```

### ACB/AWB And USM

```python
from cricodecs import acb, usm

cue_sheet = acb.load("sound.acb")
print(cue_sheet.info())
cue_sheet.extract("sound")

movie = usm.load("movie.usm")
print(movie.info())
movie.extract("movie_streams")
```

Objects and small data structs implement useful `repr()` output, so interactive
inspection is intended to be practical:

```python
from cricodecs import adx

print(repr(adx.AdxEncodeConfig()))
```

## C++ Usage

The current public CMake target is:

```cmake
CriCodecs
```

The source tree is organized by CRI format under `CriCodecs/src`. Public APIs
use value types and `std::expected<T, std::string>` for recoverable failures.

### ADX

```cpp
#include <filesystem>
#include <fstream>
#include <vector>

#include "adx_codec.hpp"

int main() {
    auto adx = cricodecs::adx::Adx::load("input.adx");
    if (!adx) {
        return 1;
    }

    auto decoded = adx->decode();
    if (!decoded) {
        return 1;
    }

    const auto& pcm = decoded->pcm_data;
    (void)pcm;
}
```

### HCA

```cpp
#include <cstdint>
#include <vector>

#include "hca_codec.hpp"

std::vector<int16_t> decode_hca(std::span<const uint8_t> bytes) {
    auto decoded = cricodecs::hca::decode(bytes, 0xCF222F1FE0748978ULL);
    if (!decoded) {
        return {};
    }
    return std::move(*decoded);
}
```

### CPK

```cpp
#include "cpk_container.hpp"

int main() {
    auto archive = cricodecs::cpk::Cpk::load("input.cpk");
    if (!archive) {
        return 1;
    }

    return archive->files().empty() ? 1 : 0;
}
```

## Credits

CriCodecs builds on public knowledge and prior open-source work around CRI
formats. Many thanks and credits to:

- [vgmstream](https://github.com/vgmstream/vgmstream) for HCA and CRI audio
  research.
- [VGAudio](https://github.com/Thealexbarney/VGAudio) for ADX and HCA codec
  behavior references.
- [bnnm](https://github.com/bnnm) for CRI audio-format research and tooling.
- [Nyagamon](https://github.com/Nyagamon) for CRI format research.
