# Python Bindings

This subtree hosts the experimental `cricodecs` Python package.

## Current binding rules

- `load(source)` is the standard entry point for inspectable objects.
- `source` accepts bytes-like values, path-like values, and binary file-like objects.
- `cricodecs.load(source)` performs ordered loader probing and returns the
  first supported loaded object.
- Public module-level functions are reserved for whole-format one-shot operations.
- Detailed inspection lives on loaded objects.
- Single-entry extraction lives on loaded objects as `extract_file(...)` when the format has addressable entries.
- `extract(source, ...)` means whole-format extraction for the module in question.
- HCA header inspection is object-based through `cricodecs.hca.load(source).header()`.

## Public package shape

- `cricodecs.adx`
  - `load`, `decode`, `encode`
  - `Adx`, `AdxInfo`, `AdxHeader`, `AdxLoop`, `AdxEncodeConfig`
- `cricodecs.aax`
  - `load`, `extract`, `build`
  - `Aax`, `AaxInfo`, `AaxSegmentInfo`, `AaxBuildEntry`
- `cricodecs.aix`
  - `load`, `extract`
  - `Aix`, `AixInfo`, `AixSegment`, `AixLayer`, `AixLoopInfo`
- `cricodecs.acx`
  - `load`, `extract`, `build`
  - `Acx`, `AcxInfo`, `AcxEntry`, `AcxEntryType`
- `cricodecs.hca`
  - `load`, `decode`, `encode`, `encrypt`, `decrypt`
  - `Hca`, `HcaHeader`, `HcaEncodeConfig`, `HcaQuality`
- `cricodecs.utf`
  - `load`
  - `Utf`, `UtfInfo`, `Column`, `ColumnFlag`, `ColumnType`, `DataRef`, `Guid`
- `cricodecs.acb`
  - `load`
  - `Acb`, `AcbInfo`, `AcbWaveformInfo`
- `cricodecs.afs`
  - `load`, `extract`
  - `Afs`, `AfsInfo`, `AfsEntry`, `AfsEntryType`, `AfsDirectoryTimestamp`
- `cricodecs.awb`
  - `load`, `extract`
  - `Awb`, `AwbInfo`, `AwbEntry`, `AwbEntryInfo`, `AacEncryptionState`
- `cricodecs.cpk`
  - `load`, `extract`
  - `Cpk`, `CpkInfo`, `CpkFileInfo`, `CpkEntry`, `CpkMode`, `CpkPreset`
- `cricodecs.csb`
  - `load`, `build`, `extract`
  - `Csb`, `CsbInfo`, `CsbSection`, `CsbStreamInfo`, `CsbBuildEntry`
- `cricodecs.cvm`
  - `load`, `extract`, `build`, `export_script`, `load_script`, `parse_script`
  - `Cvm`, `CvmInfo`, `CvmHeader`, `CvmZoneLayout`, `CvmPrimaryVolume`, `CvmEntry`, `CvmDirectoryEntry`, `CvmDirectoryRecord`, `CvmBuildConfig`, `CvmBuildFile`, `CvmBuildScript`, `CvmBuildScriptInfo`
- `cricodecs.sfd`
  - `load`, `mux`, `demux`, `extract`
  - `Sfd`, `SfdInfo`, `SfdMuxConfig`, `SfdStream`, `SfdHeaderSummary`, `SfdElementRecord`, `SfdChunkSpan`, `SfdVideoSequenceHeader`, `SfdStreamType`, `SfdAudioType`, `SfdVideoType`, `SfdHeaderVariant`, `SfdBuildProfile`
- `cricodecs.usm`
  - `load`, `demux`, `extract`
  - `Usm`, `UsmInfo`, `UsmStreamInfo`, `UsmChunkType`
- `cricodecs.wav`
  - `load`, `build`
  - `Wav`, `WavInfo`, `WavFormat`, `SamplerChunk`, `SampleLoop`, `CuePoint`, `GUID`

## Deferred surface

- `cricodecs.ahx` is intentionally not exposed yet.
- The current native AHX API is config-driven encode/decode helpers rather than a self-describing loadable object, so binding it under the same package rules would require inventing a different user model.
- `cricodecs.sfd` mux support is intentionally limited to the current fixed-pack builder surface; broader authoring behavior is still deferred.
- `cricodecs.cvm` now exposes the mutable loaded `Cvm` object plus the current bounded Xbox-subset `.cvs` helpers and scrambled-image key flow, while `CvmVolumeSet` remains an advanced runtime helper and broader non-Xbox script/runtime behavior is still deferred.

## CLI

The shared CLI implementation lives in the native library and is exposed in two ways:

- native release builds produce a standalone `cricodecs` executable
- `pip install cricodecs` installs a `cricodecs` console script backed by the same native CLI implementation

Default CLI behavior is input-driven:

- `cricodecs <input>` extracts archive/container formats to a sibling directory
- `cricodecs <input>` decodes `ADX`, `AHX`, `HCA`, and `AAX` to a sibling `.wav`
- `UTF` defaults to metadata output only

Focused flags:

- `-e`, `--extract` explicitly requests the default write action
- `-m` prints metadata
- `-m --json` emits metadata as JSON
- `-f`, `--force-type` forces the parser type when detection needs help
- `-o`, `--output` overrides the default output path

## Build and verification

For normal local installation from the repo root:

```bash
uv pip install .
```

Focused build-tree checks remain:

```bash
cmake -S Tests -B Tests/.build -G Ninja
cmake --build Tests/.build --target AdxTests AaxTests AixTests AcxTests CsbTests CvmRofsTests SfdTests PcmTests
cmake -S . -B .build-python-check -G Ninja -DCRICODECS_BUILD_TESTS=ON -DCRICODECS_BUILD_PYTHON=ON
cmake --build .build-python-check --target cricodecs_python_contract_check cricodecs_python_native_smoke
```
