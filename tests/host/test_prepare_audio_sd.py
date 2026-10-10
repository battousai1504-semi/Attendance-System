import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "prepare_audio_sd", ROOT / "tools" / "prepare_audio_sd.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PrepareAudioSdTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.sd = self.root / "sd"
        self.source.mkdir()
        self.sd.mkdir()
        for name in MODULE.BASE_TRACKS:
            (self.source / name).write_bytes(b"test-mp3")
        self.roster = self.root / "roster.json"
        self.roster.write_text(json.dumps({
            "ok": True, "consistent": True,
            "students": [
                {"id": 1, "name": "Học sinh A", "class": "12A1"},
                {"id": 2, "name": "Học sinh B", "class": "12A1"},
            ],
        }), encoding="utf-8")

    def test_two_greetings_map_to_stable_student_ids(self):
        (self.source / "student_0001.mp3").write_bytes(b"hello A")
        (self.source / "student_0002.mp3").write_bytes(b"hello B")
        MODULE.prepare(self.source, self.sd, self.roster)
        self.assertEqual((self.sd / "mp3" / "1001.mp3").read_bytes(), b"hello A")
        self.assertEqual((self.sd / "mp3" / "1002.mp3").read_bytes(), b"hello B")
        self.assertEqual((self.sd / "mp3" / "0002.mp3").read_bytes(), b"test-mp3")

    def test_missing_greeting_does_not_partially_copy(self):
        (self.source / "student_0001.mp3").write_bytes(b"hello A")
        with self.assertRaisesRegex(ValueError, "student_0002.mp3"):
            MODULE.prepare(self.source, self.sd, self.roster)
        self.assertFalse((self.sd / "mp3").exists())

    def test_duplicate_id_and_stale_clip_are_rejected(self):
        (self.source / "student_0001.mp3").write_bytes(b"hello A")
        (self.source / "student_0002.mp3").write_bytes(b"hello B")
        value = json.loads(self.roster.read_text(encoding="utf-8"))
        value["students"].append({"id": 1, "name": "Another A"})
        self.roster.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "bị lặp"):
            MODULE.prepare(self.source, self.sd, self.roster)
        value["students"].pop()
        self.roster.write_text(json.dumps(value), encoding="utf-8")
        (self.sd / "mp3").mkdir()
        (self.sd / "mp3" / "1003.mp3").write_bytes(b"old")
        with self.assertRaisesRegex(ValueError, "lời chào ID cũ"):
            MODULE.prepare(self.source, self.sd, self.roster)


if __name__ == "__main__":
    unittest.main()
