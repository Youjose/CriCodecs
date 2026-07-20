from typing import Any, ClassVar, Iterable, Sequence

from .wav import Wav

class AhxBitAllocationPreset:
    DEFAULT_PATTERN: ClassVar["AhxBitAllocationPreset"]
    PRESET_22050: ClassVar["AhxBitAllocationPreset"]
    PRESET_24000: ClassVar["AhxBitAllocationPreset"]
    PRESET_44100: ClassVar["AhxBitAllocationPreset"]
    PRESET_48000: ClassVar["AhxBitAllocationPreset"]

class AhxKey:
    start: int
    mult: int
    add: int
    def empty(self) -> bool: ...

class KeyCandidate:
    key: AhxKey
    score: float
    source_count: int
    evidence_count: int
    evidence_frames: int
    candidate_counts: tuple[int, int, int]
    canonical_type9_code: int

class KeyRecoveryResult:
    candidates: list[KeyCandidate]
    source_count: int
    evidence_count: int

class AhxDecodeConfig:
    encoding_mode: int
    sample_rate: int
    sample_count: int
    channels: int
    encryption_type: int
    start_offset: int
    key: AhxKey

class AhxEncodeConfig:
    sample_rate: int
    channels: int
    encoding_mode: int
    encryption_type: int
    key: AhxKey
    bit_allocation_pattern: tuple[int, ...]

AhxKeyLike = AhxKey | bytes | Sequence[int] | str | int | None

def default_bit_allocation_pattern() -> tuple[int, ...]: ...
def preset_bit_allocation_pattern(preset: AhxBitAllocationPreset) -> tuple[int, ...]: ...
def clamp_bit_allocation_pattern(pattern: Iterable[int]) -> tuple[int, ...]: ...
def decode(source: Any, config: AhxDecodeConfig | None = None, key: AhxKeyLike = None, subkey: int = 0) -> bytes: ...
def recover_key(source: Any | Sequence[Any], same_base_key: bool = True) -> KeyRecoveryResult: ...
def encode(wav: Wav | Any, config: AhxEncodeConfig | None = None) -> bytes: ...

__all__: list[str]
