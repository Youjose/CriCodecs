from typing import Any, ClassVar

class MpegVideoType:
    UNKNOWN: ClassVar["MpegVideoType"]
    MPEG1: ClassVar["MpegVideoType"]
    MPEG2: ClassVar["MpegVideoType"]

class IvfHeader:
    magic: int
    version: int
    header_size: int
    fourcc: str
    width: int
    height: int
    rate: int
    scale: int
    num_frames: int
    unused: int

class H264SequenceParameterSet:
    profile_idc: int
    level_idc: int
    width: int
    height: int
    fixed_frame_rate: bool
    num_units_in_tick: int
    time_scale: int

class MpegVideoSequenceHeader:
    width: int
    height: int
    aspect_ratio_code: int
    frame_rate_code: int
    bit_rate_value: int

class IvfFrame:
    timestamp: int
    data: bytes

class VideoFrame:
    timestamp: int
    data: bytes

class IvfReader:
    header: IvfHeader
    raw_header: bytes
    has_frames: bool
    @staticmethod
    def load(path: str) -> "IvfReader": ...
    def read_next_frame(self) -> IvfFrame | None: ...

class MpegVideoReader:
    video_type: MpegVideoType
    sequence_header: MpegVideoSequenceHeader | None
    frame_rate: float
    frame_count: int
    has_frames: bool
    @staticmethod
    def load(path: str) -> "MpegVideoReader": ...
    def read_next_frame(self) -> VideoFrame | None: ...

class H264VideoReader:
    sequence_parameter_set: H264SequenceParameterSet | None
    frame_rate: float
    frame_count: int
    has_frames: bool
    @staticmethod
    def load(path: str) -> "H264VideoReader": ...
    def read_next_frame(self) -> VideoFrame | None: ...

Ivf = IvfReader
Mpeg = MpegVideoReader
H264 = H264VideoReader

def load(source: Any) -> IvfReader | MpegVideoReader | H264VideoReader: ...

__all__: list[str]
