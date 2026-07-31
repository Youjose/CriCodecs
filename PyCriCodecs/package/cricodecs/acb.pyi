from typing import Any

from .awb import KeyRecoveryResult
from .awb import AacEncryptionState, Awb, AwbInfo

class AcbWaveformInfo:
    index: int
    name: str
    name_raw: bytes
    filename: str
    id: int
    memory_awb_id: int
    stream_awb_id: int
    port_no: int
    streaming: int
    encode_type: int
    loop_flag: bool
    extension_data: int

class AcbCueClipPlan:
    waveform_index: int
    start_time_us: int
    awb_wave_id: int | None
    awb_stream_index: int | None
    awb_bank: str | None

class AcbCueBlockPlan:
    block_position: int | None
    block_index: int | None
    name: str
    duration_us: int
    authored_loop_count: int
    render_loop_count: int
    forced_advance: bool
    skipped_empty_hold: bool
    clips: list[AcbCueClipPlan]

class AcbCuePlaybackPlan:
    cue_index: int
    cue_id: int
    cue_name: str
    blocks: list[AcbCueBlockPlan]
    diagnostics: list[str]

class WaveformAwbEntry:
    waveform_index: int
    wave_id: int
    awb_index: int
    stream_bank: bool

class AcbInfo:
    source_path: str | None
    name: str
    waveform_count: int
    cue_count: int
    waveforms: list[AcbWaveformInfo]
    table_name: str
    row_count: int
    column_count: int
    has_embedded_awb: bool
    companion_awb_path: str | None
    has_aac_waveforms: bool
    awb_info: AwbInfo | None

class Acb:
    source_path: str | None
    name: str
    waveform_count: int
    cue_count: int
    has_embedded_awb: bool
    companion_awb_path: str | None
    has_aac_waveforms: bool

    @staticmethod
    def load(source: Any, encoding: str | None = None) -> "Acb": ...
    @staticmethod
    def load_bytes(data: bytes, encoding: str | None = None) -> "Acb": ...
    def info(self) -> AcbInfo: ...
    def waveform(self, index: int) -> AcbWaveformInfo: ...
    def waveform_awb_entry(self, index: int, awb: Awb | None = None, prefer_stream_bank: bool = False) -> WaveformAwbEntry: ...
    def replace_waveform_bytes(self, index: int, awb: Awb, data: bytes, prefer_stream_bank: bool = False) -> WaveformAwbEntry: ...
    def replace_waveform_file(self, index: int, awb: Awb, input_path: Any, prefer_stream_bank: bool = False) -> WaveformAwbEntry: ...
    def waveform_name(self, index: int) -> str: ...
    def waveform_name_raw(self, index: int) -> bytes: ...
    def waveform_filename(self, index: int, include_index_prefix: bool = True) -> str: ...
    def cue_filename(self, index: int, include_index_prefix: bool = True) -> str: ...
    def cue_plan(self, index: int, loop_count: int = 0, advance_after_infinite: bool = True, include_empty_holds: bool = True, block_loop_counts: dict[int, int] | None = None) -> AcbCuePlaybackPlan: ...
    def cue_wav_bytes(self, index: int, loop_count: int = 0, advance_after_infinite: bool = True, hca_keycode: int = 0, hca_subkey: int | None = None, include_empty_holds: bool = True, block_loop_counts: dict[int, int] | None = None) -> bytes: ...
    def extract_cue(self, index: int, output_path: Any, loop_count: int = 0, advance_after_infinite: bool = True, hca_keycode: int = 0, hca_subkey: int | None = None, include_empty_holds: bool = True, block_loop_counts: dict[int, int] | None = None) -> None: ...
    def extract_cues(self, output_dir: Any, loop_count: int = 0, advance_after_infinite: bool = True, hca_keycode: int = 0, hca_subkey: int | None = None, include_empty_holds: bool = True, block_loop_counts: dict[int, int] | None = None) -> list[str]: ...
    def waveform_bytes(self, index: int, aac_keycode: int = 0) -> bytes: ...
    def extract_waveform_data(self, index: int, aac_keycode: int = 0) -> bytes: ...
    def extract_waveform_stream_data(self, index: int, aac_keycode: int = 0) -> bytes: ...
    def probe_waveform_aac_encryption(self, index: int, keycode: int) -> AacEncryptionState: ...
    def recover_aac_key(self) -> KeyRecoveryResult: ...
    def embedded_awb_bytes(self) -> bytes | None: ...
    def load_awb(self) -> Awb | None: ...
    def extract_file(self, index: int, output_path: Any, aac_keycode: int = 0) -> None: ...
    def extract(self, output_dir: Any, aac_keycode: int = 0) -> None: ...

def load(source: Any, encoding: str | None = None) -> Acb: ...
def extract(source: Any, output_dir: Any, aac_keycode: int = 0, encoding: str | None = None) -> None: ...

__all__: list[str]
