#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
test_binary="/tmp/robot_attendance_host_tests"
payload_test_binary="/tmp/robot_attendance_payload_tests"
config_test_binary="/tmp/robot_attendance_config_tests"
outbox_test_binary="/tmp/robot_attendance_outbox_tests"
policy_test_binary="/tmp/robot_attendance_policy_tests"
ota_manifest_test_binary="/tmp/robot_attendance_ota_manifest_tests"
dfplayer_test_binary="/tmp/robot_attendance_dfplayer_tests"

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

gcc \
  -std=c11 \
  -Wall -Wextra -Werror \
  -I"${repo_dir}/tests/host/stubs" \
  -I"${repo_dir}/firmware/main" \
  "${repo_dir}/firmware/main/http_payload.c" \
  "${repo_dir}/tests/host/test_http_payload.c" \
  -o "${payload_test_binary}"

"${payload_test_binary}"

gcc \
  -std=c11 \
  -Wall -Wextra -Werror \
  -I"${repo_dir}/tests/host/stubs" \
  -I"${repo_dir}/firmware/main" \
  "${repo_dir}/firmware/main/device_config_codec.c" \
  "${repo_dir}/tests/host/test_device_config.c" \
  -o "${config_test_binary}"

"${config_test_binary}"

gcc \
  -std=c11 \
  -Wall -Wextra -Werror \
  -I"${repo_dir}/tests/host/stubs" \
  -I"${repo_dir}/firmware/main" \
  "${repo_dir}/firmware/main/outbox_codec.c" \
  "${repo_dir}/tests/host/test_outbox_codec.c" \
  -o "${outbox_test_binary}"

"${outbox_test_binary}"

gcc \
  -std=c11 \
  -Wall -Wextra -Werror \
  -I"${repo_dir}/firmware/main" \
  "${repo_dir}/firmware/main/upload_policy.c" \
  "${repo_dir}/tests/host/test_upload_policy.c" \
  -o "${policy_test_binary}"

"${policy_test_binary}"

gcc \
  -std=c11 \
  -Wall -Wextra -Werror \
  -I"${repo_dir}/firmware/main" \
  "${repo_dir}/firmware/main/ota_manifest.c" \
  "${repo_dir}/tests/host/test_ota_manifest.c" \
  -o "${ota_manifest_test_binary}"

"${ota_manifest_test_binary}"

gcc \
  -std=c11 \
  -Wall -Wextra -Werror \
  -I"${repo_dir}/firmware/main" \
  "${repo_dir}/firmware/main/dfplayer_protocol.c" \
  "${repo_dir}/tests/host/test_dfplayer_protocol.c" \
  -o "${dfplayer_test_binary}"

"${dfplayer_test_binary}"

attendance_node_bin="${NODE_BIN:-node}"
if ! command -v "${attendance_node_bin}" >/dev/null 2>&1; then
  echo "Node.js is required for firmware source tests; set NODE_BIN to its executable" >&2
  exit 1
fi
"${attendance_node_bin}" "${repo_dir}/tests/host/test_portal_html.js"
"${attendance_node_bin}" "${repo_dir}/tests/host/test_camera_resilience.js"
"${attendance_node_bin}" "${repo_dir}/tests/host/test_p24_source.js"
"${attendance_node_bin}" "${repo_dir}/tests/host/test_p25_source.js"
"${attendance_node_bin}" "${repo_dir}/tests/host/test_p3_source.js"
python3 "${repo_dir}/tests/host/test_prepare_audio_sd.py"
