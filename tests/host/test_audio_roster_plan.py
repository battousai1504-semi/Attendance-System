import json
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile
import sys


sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from plan_audio_from_xlsx import recording_rows  # noqa: E402


class AudioRosterPlanTest(unittest.TestCase):
    def test_robot_id_not_excel_stt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            workbook = root / "list.xlsx"
            worksheet = '''<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
              <sheetData><row r="4"><c r="A4"><v>1</v></c>
              <c r="B4" t="inlineStr"><is><t>Lê Gia Bảo</t></is></c></row></sheetData>
              </worksheet>'''
            with ZipFile(workbook, "w") as archive:
                archive.writestr("xl/worksheets/sheet1.xml", worksheet)
            roster = root / "students.json"
            roster.write_text(json.dumps({"students": [{"id": 7, "name": "Lê Gia Bảo"}]}),
                              encoding="utf-8")
            rows = recording_rows(workbook, roster)
            self.assertEqual(rows[0][0], 7)
            self.assertEqual(rows[0][3], "student_0007.mp3")
            self.assertEqual(rows[0][4], "1007.mp3")
            roster.write_text(json.dumps({"students": [{"id": 7, "name": "Sai tên"}]}),
                              encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "không khớp Excel"):
                recording_rows(workbook, roster)


if __name__ == "__main__":
    unittest.main()
