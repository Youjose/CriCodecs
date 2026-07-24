from typing import Any, ClassVar, Sequence, overload

class KeyCandidate:
    key: int
    score: float
    source_count: int
    evidence_count: int
    sample_blocks: int
    video_chunks: int
    audio_chunks: int
    audio_score: float
    hca_streams: int
    hca_score: float
    hca_video_supported: bool

class KeyRecoveryResult:
    candidates: list[KeyCandidate]
    source_count: int
    evidence_count: int

class UsmChunkType:
    CRID: ClassVar["UsmChunkType"]
    SFSH: ClassVar["UsmChunkType"]
    AHX: ClassVar["UsmChunkType"]
    ELM: ClassVar["UsmChunkType"]
    ATP: ClassVar["UsmChunkType"]
    PST: ClassVar["UsmChunkType"]
    SFV: ClassVar["UsmChunkType"]
    SFA: ClassVar["UsmChunkType"]
    ALP: ClassVar["UsmChunkType"]
    CUE: ClassVar["UsmChunkType"]
    SBT: ClassVar["UsmChunkType"]
    STA: ClassVar["UsmChunkType"]
    USR: ClassVar["UsmChunkType"]

class UsmSubtitleFormat:
    AUTO: ClassVar["UsmSubtitleFormat"]
    SOURCE_TEXT: ClassVar["UsmSubtitleFormat"]
    SRT: ClassVar["UsmSubtitleFormat"]
    ASS: ClassVar["UsmSubtitleFormat"]
    SBT: ClassVar["UsmSubtitleFormat"]

class UsmAudioCodec:
    ADX: ClassVar["UsmAudioCodec"]
    HCA: ClassVar["UsmAudioCodec"]
    UNKNOWN: ClassVar["UsmAudioCodec"]

class UsmStreamInfo:
    stream_id: int
    channel_no: int
    audio_codec: UsmAudioCodec | None
    filename: str
    filename_raw: bytes
    filesize: int
    avbps: int
    minbuf: int
    minchk: int
    fmtver: int

class UsmMuxAudioTrack:
    """An ADX or HCA input track for USM muxing.

    ``encrypt`` selects the codec-aware policy for this track: ADX uses the
    USM audio mask, plain HCA becomes cipher type 56, and encrypted HCA is
    preserved without a second encryption pass.
    """
    path: str
    encrypt: bool | None
    channel_no: int | None
    def __init__(self, path: Any, encrypt: bool | None = None, channel_no: int | None = None) -> None: ...

class UsmMuxSubtitleTrack:
    path: str
    language_id: int
    format: UsmSubtitleFormat
    channel_no: int | None
    def __init__(
        self,
        path: Any,
        language_id: int = 0,
        format: UsmSubtitleFormat = UsmSubtitleFormat.AUTO,
        channel_no: int | None = None,
    ) -> None: ...

class UsmMuxConfig:
    """USM mux configuration with optional alpha video and repeatable audio/subtitle tracks."""
    video_path: str
    alpha_path: str | None
    audio_tracks: list[UsmMuxAudioTrack]
    subtitle_tracks: list[UsmMuxSubtitleTrack]
    key: int | None
    encrypt_audio: bool | None
    def __init__(
        self,
        video_path: Any,
        audio_tracks: list[UsmMuxAudioTrack] = ...,
        subtitle_tracks: list[UsmMuxSubtitleTrack] = ...,
        encrypt_audio: bool | None = None,
        key: int | None = None,
        encoding: str | None = None,
        alpha_path: Any | None = None,
    ) -> None: ...

class UsmInfo:
    source_path: str | None
    container_filename: str
    stream_count: int
    streams: list[UsmStreamInfo]

class Usm:
    source_path: str | None
    container_filename: str
    stream_count: int
    streams: list[UsmStreamInfo]

    @staticmethod
    def load(source: Any, encoding: str | None = None, key: int | None = None) -> "Usm": ...
    @staticmethod
    def load_bytes(data: bytes, encoding: str | None = None, key: int | None = None) -> "Usm": ...
    def info(self, encoding: str | None = None) -> UsmInfo: ...
    def stream(self, index: int, encoding: str | None = None) -> UsmStreamInfo: ...
    def stream_bytes(self, index: int) -> bytes: ...
    def stream_sample(self, index: int, max_bytes: int = 4096) -> bytes: ...
    def set_key(self, key: int | None = None) -> None: ...
    def encrypt(self) -> bytes: ...
    def decrypt(self) -> bytes: ...
    def recover_key(self) -> KeyRecoveryResult: ...
    def demux(self) -> dict[str, bytes]: ...
    def extract_file(self, index: int, output_path: Any) -> None: ...
    def extract(self, output_dir: Any) -> None: ...

def load(source: Any, *, encoding: str | None = None, key: int | None = None) -> Usm: ...
def recover_key(source: Any | Sequence[Any], same_base_key: bool = True, encoding: str | None = None) -> KeyRecoveryResult: ...
def demux(source: Any, *, encoding: str | None = None, key: int | None = None) -> dict[str, bytes]: ...
def extract(source: Any, output_dir: Any, *, encoding: str | None = None, key: int | None = None) -> None: ...
@overload
def mux(config: UsmMuxConfig) -> bytes: ...
@overload
def mux(config: UsmMuxConfig, output_path: Any) -> None: ...
@overload
def mux(
    video_path: Any,
    audio_paths: list[Any] = ...,
    audio_encrypt: list[bool | None] = ...,
    encrypt_audio: bool | None = None,
    key: int | None = None,
    encoding: str | None = None,
    alpha_path: Any | None = None,
) -> bytes: ...
def mux_to_file(
    output_path: Any,
    video_path: Any,
    audio_paths: list[Any] = ...,
    audio_encrypt: list[bool | None] = ...,
    encrypt_audio: bool | None = None,
    key: int | None = None,
    encoding: str | None = None,
    alpha_path: Any | None = None,
) -> None: ...
def sbt_to_text(data: bytes) -> str: ...
def text_to_sbt(text: str, language_id: int = 0) -> bytes: ...
def sbt_to_srt(data: bytes) -> str: ...
def sbt_to_srt_tracks(data: bytes) -> dict[int, str]: ...
def srt_to_sbt(text: str, language_id: int = 0, time_unit: int = 1000) -> bytes: ...
def sbt_to_ass(data: bytes, title: str = "CriCodecs subtitles") -> str: ...
def ass_to_sbt(text: str, language_id: int = 0, time_unit: int = 1000) -> bytes: ...

__all__: list[str]
