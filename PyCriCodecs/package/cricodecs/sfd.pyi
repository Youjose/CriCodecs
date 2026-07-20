from typing import Any, ClassVar

class SfdStreamType:
    VIDEO: ClassVar["SfdStreamType"]
    AUDIO: ClassVar["SfdStreamType"]
    PRIVATE_DATA: ClassVar["SfdStreamType"]

class SfdVideoType:
    UNKNOWN: ClassVar["SfdVideoType"]
    MPEG1: ClassVar["SfdVideoType"]
    MPEG2: ClassVar["SfdVideoType"]

class SfdAudioType:
    UNKNOWN: ClassVar["SfdAudioType"]
    ADX: ClassVar["SfdAudioType"]
    AIX: ClassVar["SfdAudioType"]
    AC3: ClassVar["SfdAudioType"]

class SfdHeaderVariant:
    UNKNOWN: ClassVar["SfdHeaderVariant"]
    SOFDEC_STREAM: ClassVar["SfdHeaderVariant"]
    SOFDEC_STREAM2: ClassVar["SfdHeaderVariant"]

class SfdBuildProfile:
    SOFDEC_STREAM_STANDARD_FIXED_2048: ClassVar["SfdBuildProfile"]
    SOFDEC_STREAM_FIXED_2048: ClassVar["SfdBuildProfile"]
    SOFDEC_STREAM_SFDMUXG_FIXED_2048: ClassVar["SfdBuildProfile"]
    SOFDEC_STREAM2_FIXED_2048_V23249: ClassVar["SfdBuildProfile"]
    SOFDEC_STREAM2_FIXED_2048_V23310: ClassVar["SfdBuildProfile"]
    SOFDEC_STREAM2_CRAFT: ClassVar["SfdBuildProfile"]
    SOFDEC_STREAM2_MEDIANOCHE: ClassVar["SfdBuildProfile"]

class SfdVideoSequenceHeader:
    width: int
    height: int
    aspect_ratio_code: int
    frame_rate_code: int
    bit_rate_value: int

class SfdChunkSpan:
    source_offset: int
    size: int

class SfdElementRecord:
    stream_id: int
    source_type: int
    short_name: str
    timestamp: str
    width: int | None
    height: int | None
    frame_rate_code: int | None
    picture_rate: int | None
    audio_channels: int | None
    audio_sample_rate: int | None
    detail_bytes: bytes
    footer_bytes: bytes

class SfdHeaderSummary:
    variant: SfdHeaderVariant
    header_label: str
    output_name: str
    short_output_name: str
    output_timestamp: str
    builder_version: str
    version_tag_bytes: bytes
    version_tag_size: int
    pack_size: int
    variable_pack: bool
    reserved_header_size: int
    min_header_packet_count: int
    bitrate_bytes_per_second: int
    element_count: int
    video_count: int
    audio_count: int
    private_count: int
    element_records: list[SfdElementRecord]

class SfdStream:
    index: int
    type: SfdStreamType
    type_index: int
    stream_id: int
    source_name: str
    extracted_size: int
    packet_count: int
    chunks: list[SfdChunkSpan]
    video_type: SfdVideoType
    audio_type: SfdAudioType
    video_header: SfdVideoSequenceHeader | None
    element_record: SfdElementRecord | None
    def suggested_path(self, include_index_prefix: bool = True) -> str: ...

class SfdInfo:
    source_path: str | None
    stream_count: int
    streams: list[SfdStream]
    header_summary: SfdHeaderSummary | None

class Sfd:
    source_path: str | None
    stream_count: int
    streams: list[SfdStream]
    header_summary: SfdHeaderSummary | None

    @staticmethod
    def load(source: Any) -> "Sfd": ...
    @staticmethod
    def load_bytes(data: bytes) -> "Sfd": ...
    def info(self) -> SfdInfo: ...
    def stream(self, index: int) -> SfdStream: ...
    def find_stream_by_id(self, stream_id: int) -> SfdStream | None: ...
    def stream_bytes(self, index: int) -> bytes: ...
    def demux(self, include_index_prefix: bool = True) -> dict[str, bytes]: ...
    def extract_file(self, index: int, output_path: Any) -> None: ...
    def extract(self, output_dir: Any) -> None: ...
    def save(self, output_path: Any | None = None) -> bytes | None: ...
    def save_to_file(self, output_path: Any) -> None: ...

def load(source: Any) -> Sfd: ...
def demux(source: Any, include_index_prefix: bool = True) -> dict[str, bytes]: ...
def extract(source: Any, output_dir: Any) -> None: ...
def mux(
    video_path: str,
    audio_path: str | None = None,
    video_source_name: str = "",
    audio_source_name: str = "",
    video_stream_name: str = "",
    audio_stream_name: str = "",
    output_name: str = "",
    build_profile: SfdBuildProfile | None = None,
    header_builder_version: str = "",
    mux_profile: SfdBuildProfile | None = None,
    header_builder_version_override: str = "",
) -> bytes: ...
def mux_to_file(
    output_path: str,
    video_path: str,
    audio_path: str | None = None,
    video_source_name: str = "",
    audio_source_name: str = "",
    video_stream_name: str = "",
    audio_stream_name: str = "",
    output_name: str = "",
    build_profile: SfdBuildProfile | None = None,
    header_builder_version: str = "",
    mux_profile: SfdBuildProfile | None = None,
    header_builder_version_override: str = "",
) -> None: ...

__all__: list[str]
