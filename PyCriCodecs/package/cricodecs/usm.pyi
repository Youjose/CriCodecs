from typing import Any, ClassVar

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

class UsmStreamInfo:
    stream_id: int
    channel_no: int
    filename: str
    filename_raw: bytes
    filesize: int
    avbps: int
    minbuf: int
    minchk: int
    fmtver: int

class UsmMuxAudioTrack:
    path: str
    encrypt: bool

class UsmMuxConfig:
    video_path: str
    audio_tracks: list[UsmMuxAudioTrack]
    key: int
    encrypt_audio: bool

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
    def load(source: Any, encoding: str | None = None) -> "Usm": ...
    @staticmethod
    def load_bytes(data: bytes, encoding: str | None = None) -> "Usm": ...
    def info(self, encoding: str | None = None) -> UsmInfo: ...
    def stream(self, index: int, encoding: str | None = None) -> UsmStreamInfo: ...
    def stream_bytes(self, index: int) -> bytes: ...
    def demux(self) -> dict[str, bytes]: ...
    def extract_file(self, index: int, output_path: Any) -> None: ...
    def extract(self, output_dir: Any) -> None: ...

def load(source: Any, *, encoding: str | None = None) -> Usm: ...
def demux(source: Any) -> dict[str, bytes]: ...
def extract(source: Any, output_dir: Any) -> None: ...
def mux(output_path: Any, config: UsmMuxConfig) -> None: ...

__all__: list[str]
