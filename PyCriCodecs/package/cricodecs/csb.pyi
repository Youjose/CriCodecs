from typing import Any

class CsbBuildEntry:
    source_path: str
    archive_path: str

class CsbSection:
    row_index: int
    name: str
    table_type: str
    data_size: int

class CsbStreamInfo:
    row_index: int
    name: str
    name_raw: bytes
    format: str
    channels: int
    sample_rate: int
    sample_count: int
    streamed: bool
    wrapper_size: int
    wrapper_table_name: str
    suggested_path: str

class CsbInfo:
    source_path: str | None
    name: str
    section_count: int
    element_count: int
    stream_count: int
    sections: list[CsbSection]
    streams: list[CsbStreamInfo]

class Csb:
    source_path: str | None
    name: str
    section_count: int
    element_count: int
    stream_count: int
    sections: list[CsbSection]
    elements: list[Any]
    streams: list[CsbStreamInfo]

    @staticmethod
    def load(source: Any, encoding: str | None = None) -> "Csb": ...
    @staticmethod
    def load_bytes(data: bytes, encoding: str | None = None) -> "Csb": ...
    def info(self, encoding: str | None = None) -> CsbInfo: ...
    def section(self, index: int, encoding: str | None = None) -> CsbSection: ...
    def element(self, index: int, encoding: str | None = None) -> Any: ...
    def stream(self, index: int, encoding: str | None = None) -> CsbStreamInfo: ...
    def stream_bytes(self, index: int) -> bytes: ...
    def wrapper_bytes(self, index: int) -> bytes: ...
    def extract_file(self, index: int, output_path: Any, encoding: str | None = None) -> None: ...
    def extract(self, output_dir: Any, encoding: str | None = None) -> None: ...
    def save(self, output_path: Any | None = None) -> bytes | None: ...
    def save_to_file(self, output_path: Any) -> None: ...

def load(source: Any, *, encoding: str | None = None) -> Csb: ...
def extract(source: Any, output_dir: Any, *, encoding: str | None = None) -> None: ...
def build(entries: Any, output_path: Any | None = None, *, encoding: str | None = None) -> bytes | None: ...

__all__: list[str]
