from typing import Any

class GUID:
    data1: int
    data2: int
    data3: int
    data4: bytes

class WavFormat:
    compression_mode: int
    channels: int
    sample_rate: int
    avg_bytes_per_sec: int
    block_align: int
    bit_depth: int
    extension_size: int
    valid_bits_per_sample: int
    channel_mask: int
    sub_format: GUID

class CuePoint:
    name: int
    position: int
    chunk_id: int
    chunk_start: int
    block_start: int
    sample_offset: int

class SampleLoop:
    cue_point_id: int
    type: int
    start: int
    end: int
    fraction: int
    play_count: int

class SamplerChunk:
    manufacturer: int
    product: int
    sample_period: int
    midi_unity_note: int
    midi_pitch_fraction: int
    smpte_format: int
    smpte_offset: int
    loops: list[SampleLoop]
    sampler_data: bytes

class WavInfo:
    source_path: str | None
    sample_rate: int
    channels: int
    bit_depth: int
    sample_count: int
    has_loops: bool

class Wav:
    source_path: str | None
    format: WavFormat
    sample_rate: int
    channels: int
    sample_count: int
    has_loops: bool
    cues: list[CuePoint]
    sampler: SamplerChunk | None

    @staticmethod
    def load(source: Any) -> "Wav": ...
    @staticmethod
    def load_bytes(data: bytes) -> "Wav": ...
    def info(self) -> WavInfo: ...
    def pcm16le(self) -> bytes: ...
    def sample(self, index: int) -> int: ...

def load(source: Any) -> Wav: ...
def build(output_path: Any, pcm16le: bytes, sample_rate: int, channels: int, loops: Any = ...) -> None: ...
def build_bytes(pcm16le: bytes, sample_rate: int, channels: int, loops: Any = ...) -> bytes: ...

__all__: list[str]
