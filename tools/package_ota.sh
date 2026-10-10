#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 4 ]]; then
  echo "Usage: $0 VERSION HTTPS_URL [BIN] [MANIFEST_OUTPUT]" >&2
  exit 2
fi

version="$1"
url="$2"
binary="${3:-firmware/build/robot_attendance.bin}"
manifest="${4:-firmware/build/ota-manifest.json}"

if [[ ! "${version}" =~ ^[A-Za-z0-9._+-]{1,31}$ ]]; then
  echo "VERSION must match [A-Za-z0-9._+-]{1,31}" >&2
  exit 2
fi
if [[ "${url}" != https://* || "${url}" == *'"'* || "${url}" == *'\'* ||
      "${url}" =~ [[:space:]] ]]; then
  echo "HTTPS_URL must be a JSON-safe https:// URL without whitespace" >&2
  exit 2
fi
if [[ ! -f "${binary}" ]]; then
  echo "Firmware binary not found: ${binary}" >&2
  exit 2
fi

size="$(stat -c %s "${binary}")"
if (( size < 1 || size > 6291456 )); then
  echo "Firmware size ${size} does not fit the 0x600000 OTA slot" >&2
  exit 2
fi
sha256="$(sha256sum "${binary}" | awk '{print $1}')"
checksum_file="${binary%.*}.sha256"
printf '%s  %s\n' "${sha256}" "$(basename "${binary}")" > "${checksum_file}"
printf '{"version":"%s","url":"%s","size":%s,"sha256":"%s"}\n' \
  "${version}" "${url}" "${size}" "${sha256}" > "${manifest}"

echo "OTA package ready:"
echo "  binary:   ${binary}"
echo "  checksum: ${checksum_file}"
echo "  manifest: ${manifest}"
