#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
test_binary="/tmp/robot_attendance_host_tests"

gcc \
  -std=c11 \
  -D_POSIX_C_SOURCE=200809L \
  -DATTENDANCE_FILE='"/tmp/robot_attendance_host_test.csv"' \
  -DATTENDANCE_PREVIOUS_FILE='"/tmp/robot_attendance_host_test.previous.csv"' \
  -DATTENDANCE_MAX_FILE_BYTES=160 \
  -Wall -Wextra -Werror \
  -I"${repo_dir}/tests/host/stubs" \
  -I"${repo_dir}/firmware/main" \
  "${repo_dir}/firmware/main/attendance.c" \
  "${repo_dir}/tests/host/test_attendance.c" \
  -o "${test_binary}"

"${test_binary}"

