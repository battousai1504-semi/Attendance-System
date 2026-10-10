#!/usr/bin/env python3
"""Print a recording checklist from an Excel name list and the robot's IDs.

The worksheet's STT is never treated as a Student ID. Only GET /students owns
the audio mapping. The generated CSV is private and should stay in audio-source/.
"""

import csv
import json
import re
import sys
import unicodedata
import xml.etree.ElementTree as ET
from pathlib import Path
from zipfile import ZipFile


NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}


def normalized(name):
    return " ".join(unicodedata.normalize("NFC", name).split()).casefold()


def excel_names(path):
    with ZipFile(path) as archive:
        strings = []
        if "xl/sharedStrings.xml" in archive.namelist():
            root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
            strings = ["".join(item.itertext()) for item in root]
        root = ET.fromstring(archive.read("xl/worksheets/sheet1.xml"))
        names = {}
        for row in root.findall(".//m:sheetData/m:row", NS):
            cells = {}
            for cell in row.findall("m:c", NS):
                column = re.match(r"[A-Z]+", cell.attrib.get("r", ""))
                if not column or column.group() not in ("A", "B"):
                    continue
                value = cell.find("m:v", NS)
                if cell.attrib.get("t") == "s" and value is not None:
                    text = strings[int(value.text)]
                elif cell.attrib.get("t") == "inlineStr":
                    text = "".join(cell.itertext())
                else:
                    text = value.text if value is not None else ""
                cells[column.group()] = text.strip() if text else ""
            if cells.get("A", "").isdigit() and cells.get("B"):
                key = normalized(cells["B"])
                if key in names:
                    raise ValueError("Tên bị trùng trong Excel: " + cells["B"])
                names[key] = cells["B"]
    if not names:
        raise ValueError("Không thấy cột STT / HỌ VÀ TÊN ở sheet đầu")
    return names


def recording_rows(workbook, roster):
    names = excel_names(workbook)
    data = json.loads(Path(roster).read_text(encoding="utf-8"))
    if not isinstance(data, dict) or not isinstance(data.get("students"), list):
        raise ValueError("Roster phải là JSON từ GET /students của robot")
    if data.get("consistent") is False:
        raise ValueError("Danh sách và khuôn mặt trên robot chưa đồng bộ")
    rows = []
    seen_ids = set()
    for student in data["students"]:
        student_id, name = student.get("id"), student.get("name", "")
        if (isinstance(student_id, bool) or not isinstance(student_id, int)
                or not 1 <= student_id <= 50 or student_id in seen_ids):
            raise ValueError("Student ID thiếu, trùng hoặc ngoài khoảng 1–50")
        seen_ids.add(student_id)
        key = normalized(str(name))
        if key not in names:
            raise ValueError(f"ID {student_id}: '{name}' không khớp Excel. "
                             "Kiểm tra dấu/tên và xác nhận lại trước khi ghi âm.")
        display_name = names[key]
        rows.append((student_id, display_name,
                     f"Xin chào {display_name}, bạn đã điểm danh thành công.",
                     f"student_{student_id:04d}.mp3", f"{1000 + student_id:04d}.mp3"))
    if not rows:
        raise ValueError("Robot chưa có học sinh")
    return sorted(rows)


def main():
    if len(sys.argv) != 3:
        print("Dùng: plan_audio_from_xlsx.py DANH_SACH.xlsx students.json", file=sys.stderr)
        return 2
    try:
        rows = recording_rows(sys.argv[1], sys.argv[2])
    except (OSError, ValueError, KeyError, ET.ParseError, json.JSONDecodeError) as error:
        print("Không thể lập kế hoạch ghi âm: " + str(error), file=sys.stderr)
        return 1
    writer = csv.writer(sys.stdout)
    writer.writerow(("Student ID", "Tên", "Câu cần đọc", "File ghi âm", "File trên SD"))
    writer.writerows(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
