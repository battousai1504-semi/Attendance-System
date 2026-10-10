#!/usr/bin/env python3
"""Copy DFPlayer prompts, validating every enrolled Student ID first.

The board owns face/student IDs; the SD card owns pre-recorded speech. One
complete sentence per ID avoids timing gaps and clipped multi-track speech.
"""

import json
import os
from pathlib import Path
import shutil
import sys


BASE_TRACKS = (
    "ready.mp3",
    "attendance_success.mp3",
    "duplicate.mp3",
    "time_unavailable.mp3",
    "enroll_success.mp3",
    "system_error.mp3",
)
MAX_STUDENT_ID = 50
STUDENT_TRACK_BASE = 1000


def read_roster(path):
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or not isinstance(value.get("students"), list):
        raise ValueError("Roster phải là JSON từ GET /students của robot")
    students = {}
    for student in value["students"]:
        if not isinstance(student, dict):
            raise ValueError("Dòng học sinh trong roster không hợp lệ")
        student_id = student.get("id")
        name = student.get("name")
        if (isinstance(student_id, bool) or not isinstance(student_id, int) or
                not 1 <= student_id <= MAX_STUDENT_ID or
                not isinstance(name, str) or not name.strip()):
            raise ValueError("Student ID hoặc tên trong roster không hợp lệ")
        if student_id in students:
            raise ValueError(f"Student ID {student_id} bị lặp trong roster")
        students[student_id] = name.strip()
    if not students:
        raise ValueError("Roster không có học sinh nào")
    if value.get("consistent") is False:
        raise ValueError("Roster và số khuôn mặt trên robot chưa đồng bộ")
    return students


def build_plan(source_dir, roster_path=None):
    plan = []
    for track, filename in enumerate(BASE_TRACKS, start=1):
        plan.append((source_dir / filename, f"{track:04d}.mp3", None))
    if roster_path is not None:
        for student_id, name in sorted(read_roster(roster_path).items()):
            plan.append((source_dir / f"student_{student_id:04d}.mp3",
                         f"{STUDENT_TRACK_BASE + student_id:04d}.mp3", name))
    missing = [str(source) for source, _, _ in plan
               if not source.is_file() or source.stat().st_size == 0]
    if missing:
        raise ValueError("Thiếu hoặc rỗng file âm thanh:\n  " + "\n  ".join(missing))
    return plan


def prepare(source_dir, sd_root, roster_path=None):
    if not source_dir.is_dir():
        raise ValueError(f"Không có thư mục audio nguồn: {source_dir}")
    if not sd_root.is_dir():
        raise ValueError(f"Thẻ SD chưa được mount: {sd_root}")
    plan = build_plan(source_dir, roster_path)
    target_dir = sd_root / "mp3"
    if roster_path is not None and target_dir.is_dir():
        expected = {filename for _, filename, _ in plan}
        stale = sorted(path.name for path in target_dir.iterdir()
                       if path.is_file() and path.name.endswith(".mp3") and
                       len(path.name) == 8 and path.name[:4].isdigit() and
                       1001 <= int(path.name[:4]) <= 1050 and
                       path.name not in expected)
        if stale:
            raise ValueError("Thẻ còn lời chào ID cũ; sao lưu/xóa thủ công trước: " +
                             ", ".join(stale))
    target_dir.mkdir(exist_ok=True)
    for source, filename, name in plan:
        shutil.copyfile(source, target_dir / filename)
        print(f"{source.name} -> mp3/{filename}" +
              (f" (ID {int(filename[:4]) - STUDENT_TRACK_BASE}: {name})"
               if name else ""))
    os.sync()
    if roster_path is None:
        print("CẢNH BÁO: không có roster; chỉ cài 6 câu chung, chưa có lời chào theo tên.",
              file=sys.stderr)
    else:
        print(f"Đã chuẩn bị {len(plan) - len(BASE_TRACKS)} lời chào theo tên.")
    print(f"Thẻ DFPlayer sẵn sàng tại: {target_dir}")


def main(argv):
    if len(argv) not in (3, 4):
        print("Cách dùng: prepare_audio_sd.sh AUDIO_SOURCE_DIR MOUNTED_SD_ROOT "
              "[ROSTER_JSON]", file=sys.stderr)
        return 2
    try:
        prepare(Path(argv[1]), Path(argv[2]),
                Path(argv[3]) if len(argv) == 4 else None)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Không chuẩn bị thẻ SD: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
