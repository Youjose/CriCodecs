import struct
import tempfile
import unittest
from pathlib import Path

from cricodecs import usm


class UsmCuePythonTest(unittest.TestCase):
    def test_sample_and_regenerated_cues(self):
        sample = bytes.fromhex((Path(__file__).parent / "usm_cue_example.hex").read_text())
        times = [1, 41000, 76814, 99327, 108250, 109843, 110343, 113113]
        names = [
            "Rumble=2000.000_80.000_80.000_6",
            "Rumble=2000.000_80.000_80.000_6",
            "Rumble=2000.000_80.000_80.000_6",
            "Rumble=2000.000_60.000_60.000_6",
            "Rumble=2000.000_60.000_60.000_6",
            "Trigger=PreEnd",
            "Trigger=stopVideoRendering",
            "Trigger=End",
        ]
        video = (
            struct.pack("<4sHH4sHHIIII", b"DKIF", 0, 32, b"VP90", 16, 16, 30, 1, 1, 0)
            + struct.pack("<IQ", 1, 0)
            + b"\x80"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "video.ivf"
            path.write_bytes(video)
            config = usm.UsmMuxConfig(path)
            container = usm.mux(config)
            parsed = usm.Usm.load_bytes(container[:0x800] + sample + container[0x800:]).cue_metadata()
            self.assertEqual(len(parsed), 1)
            self.assertEqual([(p.time, p.name, p.cue_type) for p in parsed[0].points],
                             list(zip(times, names, [1] * 8)))

            points = parsed[0].points
            points[0].parameter = [1, 2, 3]
            config.cue_metadata = [usm.UsmCueMetadata(points)]
            rebuilt = usm.Usm.load_bytes(usm.mux(config)).cue_metadata()
            self.assertEqual([(p.time, p.name, p.cue_type) for p in rebuilt[0].points],
                             list(zip(times, names, [1] * 8)))
            self.assertEqual(rebuilt[0].points[0].parameter, [1, 2, 3])
            self.assertFalse(hasattr(rebuilt[0], "frame_rate"))


if __name__ == "__main__":
    unittest.main()
