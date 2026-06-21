from typing import Any, ClassVar

class CpkMode:
    MODE0: ClassVar["CpkMode"]
    MODE1: ClassVar["CpkMode"]
    MODE2: ClassVar["CpkMode"]
    MODE3: ClassVar["CpkMode"]

class CpkPreset:
    CUSTOM: ClassVar["CpkPreset"]
    ID: ClassVar["CpkPreset"]
    ID_GROUP: ClassVar["CpkPreset"]
    FILENAME: ClassVar["CpkPreset"]
    FILENAME_ID: ClassVar["CpkPreset"]
    FILENAME_GROUP: ClassVar["CpkPreset"]
    FILENAME_ID_GROUP: ClassVar["CpkPreset"]

class CpkEntry:
    id: int
    toc_index: int
    dirname: str
    dirname_raw: bytes
    filename: str
    filename_raw: bytes
    full_path: str
    file_offset: int
    file_size: int
    extract_size: int
    is_compressed: bool
    request_compress: bool
    group: str
    attribute: str
    user_string: str
    update_date_time: int

class CpkFileInfo:
    index: int
    path: str
    size: int
    extract_size: int
    compressed: bool

class CpkInfo:
    source_path: str | None
    file_count: int
    alignment: int
    content_offset: int
    toc_offset: int
    etoc_offset: int
    itoc_offset: int
    gtoc_offset: int
    has_toc: bool
    has_etoc: bool
    has_itoc: bool
    has_gtoc: bool
    tver: str
    comment: str
    layout_mode: CpkMode
    preset: CpkPreset
    files: list[CpkEntry]

class Cpk:
    source_path: str | None
    file_count: int
    alignment: int
    content_offset: int
    has_toc: bool
    has_etoc: bool
    has_itoc: bool
    has_gtoc: bool
    tver: str
    comment: str
    layout_mode: CpkMode
    preset: CpkPreset
    declared_preset: CpkPreset
    files: list[CpkEntry]

    @staticmethod
    def create(preset: CpkPreset | None = None) -> "Cpk": ...
    @staticmethod
    def load(source: Any, encoding: str | None = None) -> "Cpk": ...
    @staticmethod
    def load_bytes(data: bytes, encoding: str | None = None) -> "Cpk": ...
    def info(self, encoding: str | None = None) -> CpkInfo: ...
    def file_info(self, index: int, encoding: str | None = None) -> CpkFileInfo: ...
    def file_infos(self, encoding: str | None = None) -> list[CpkFileInfo]: ...
    def file_bytes(self, index: int) -> bytes: ...
    def add_file(self, source_path: Any, archive_path: str | None = None) -> int: ...
    def add_bytes(self, data: bytes, archive_path: str) -> int: ...
    def replace_file(self, index: int, source_path: Any) -> None: ...
    def replace_bytes(self, index: int, data: bytes) -> None: ...
    def remove(self, index: int) -> None: ...
    def rename(self, index: int, archive_path: str) -> None: ...
    def set_file_path(self, index: int, archive_path: str) -> None: ...
    def enable_toc(self, enabled: bool) -> None: ...
    def enable_etoc(self, enabled: bool) -> None: ...
    def enable_itoc(self, enabled: bool) -> None: ...
    def enable_gtoc(self, enabled: bool) -> None: ...
    def declared_preset_for_save(self) -> CpkPreset: ...
    def has_declared_preset(self) -> bool: ...
    def etoc_local_dir(self) -> str: ...
    def extract(self, output_dir: Any) -> None: ...
    def save(self, output_path: Any | None = None, encoding: str | None = None) -> bytes | None: ...
    def save_bytes(self, encoding: str | None = None) -> bytes: ...

def create(preset: CpkPreset | None = None) -> Cpk: ...
def load(source: Any, *, encoding: str | None = None) -> Cpk: ...
def extract(source: Any, output_dir: Any, *, encoding: str | None = None) -> None: ...

__all__: list[str]
