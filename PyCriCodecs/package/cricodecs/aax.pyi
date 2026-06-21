from typing import Any

class AaxSegmentInfo:
    row_index: int
    sample_count: int
    data_size: int
    loop_segment: bool

class AaxInfo:
    source_path: str | None
    name: str
    segment_count: int
    sample_rate: int
    channels: int
    sample_count: int
    has_loop_segments: bool
    segments: list[AaxSegmentInfo]

class AaxBuildEntry:
    source_path: str
    archive_path: str

class Aax:
    source_path: str | None
    name: str
    segment_count: int
    sample_rate: int
    channels: int
    sample_count: int
    has_loop_segments: bool
    segments: list[AaxSegmentInfo]
    adx_data: bytes

    @staticmethod
    def load(source: Any) -> "Aax": ...
    @staticmethod
    def load_bytes(data: bytes) -> "Aax": ...
    def info(self) -> AaxInfo: ...
    def segment(self, index: int) -> AaxSegmentInfo: ...
    def segment_data(self, index: int) -> bytes: ...
    def segment_bytes(self, index: int) -> bytes: ...
    def adx_bytes(self) -> bytes: ...
    def extract_file(self, index: int, output_path: Any) -> None: ...
    def extract(self, output_dir: Any) -> None: ...
    def save(self, output_path: Any | None = None) -> bytes | None: ...
    def save_bytes(self) -> bytes: ...

def load(source: Any) -> Aax: ...
def extract(source: Any, output_dir: Any) -> None: ...
def build(output_path: Any, entries: Any, *, alignment: int = ...) -> None: ...

__all__: list[str]
