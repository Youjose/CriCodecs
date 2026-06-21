from typing import Any, ClassVar, Sequence

from .ahx import AhxKey
from .wav import Wav

class AdxLoop:
    index: int
    type: int
    start_sample: int
    start_byte: int
    end_sample: int
    end_byte: int

class AdxHeader:
    signature: int
    data_offset: int
    encoding_mode: int
    block_size: int
    bit_depth: int
    channels: int
    sample_rate: int
    sample_count: int
    highpass_freq: int
    version: int
    flags: int

class AdxEncodeConfig:
    sample_rate: int
    channels: int
    bit_depth: int
    block_size: int
    encoding_mode: int
    highpass_freq: int
    filter_id: int
    version: int
    encryption_type: int
    delete_samples_after_loop_end: bool
    key_string: str
    key64: int
    subkey: int

class AdxInfo:
    source_path: str | None
    header: AdxHeader
    has_loops: bool
    loops: list[AdxLoop]
    is_encrypted: bool
    is_ahx: bool

class Adx:
    source_path: str | None
    header: AdxHeader
    has_loops: bool
    loops: list[AdxLoop]
    is_encrypted: bool
    is_ahx: bool

    @staticmethod
    def load(source: Any) -> "Adx": ...
    @staticmethod
    def load_bytes(data: bytes) -> "Adx": ...
    def info(self) -> AdxInfo: ...
    def decode(self) -> bytes: ...
    def encode(self) -> bytes: ...
    def set_key_type8(self, key: str | bytes) -> None: ...
    def set_key_type9(self, key: int, subkey: int = 0) -> None: ...
    def set_ahx_key(self, start: int, mult: int, add: int) -> None: ...

def load(source: Any) -> Adx: ...
def decode(source: Any, key: AhxKey | bytes | Sequence[int] | str | int | None = None, subkey: int = 0) -> bytes: ...
def encode(wav: Wav | Any, config: AdxEncodeConfig | None = None, loops: Any = None) -> bytes: ...

__all__: list[str]
