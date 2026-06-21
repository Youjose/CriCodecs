from typing import Any

class AixLayer:
    sample_rate: int
    channel_count: int

class AixLoopInfo:
    start_segment: int
    start_sample: int
    end_segment: int
    end_sample: int

class AixSegment:
    offset: int
    size: int
    sample_count: int
    sample_rate: int

class AixInfo:
    source_path: str | None
    segment_count: int
    layer_count: int
    total_sample_count: int
    inferred_loop: AixLoopInfo | None
    segments: list[AixSegment]
    layers: list[AixLayer]

class AixBuildSegment:
    layer_adx_data: list[bytes]

class Aix:
    source_path: str | None
    segments: list[AixSegment]
    layers: list[AixLayer]
    total_sample_count: int
    inferred_loop: AixLoopInfo | None

    @staticmethod
    def load(source: Any) -> "Aix": ...
    @staticmethod
    def load_bytes(data: bytes) -> "Aix": ...
    def info(self) -> AixInfo: ...
    def segment_bytes(self, segment: int, layer: int) -> bytes: ...
    def extract_file(self, segment: int, layer: int, output_path: Any) -> None: ...
    def extract(self, output_dir: Any) -> None: ...

def load(source: Any) -> Aix: ...
def extract(source: Any, output_dir: Any) -> None: ...
def build(segments: Any, output_path: Any | None = None) -> bytes | None: ...

__all__: list[str]
