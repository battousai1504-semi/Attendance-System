#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 SOURCE_AUDIO_DIR MOUNTED_SD_ROOT" >&2
  exit 2
fi

source_dir="$1"
sd_root="$2"

if [[ ! -d "$source_dir" ]]; then
  echo "Source audio directory does not exist: $source_dir" >&2
  exit 1
fi
if [[ ! -d "$sd_root" || ! -w "$sd_root" ]]; then
  echo "SD mount is missing or not writable: $sd_root" >&2
  exit 1
fi

declare -a sources=(
  "ready.mp3"
  "attendance_success.mp3"
  "duplicate.mp3"
  "time_unavailable.mp3"
  "enroll_success.mp3"
  "system_error.mp3"
)

for source_name in "${sources[@]}"; do
  if [[ ! -s "$source_dir/$source_name" ]]; then
    echo "Missing or empty source file: $source_dir/$source_name" >&2
    exit 1
  fi
done

destination="$sd_root/mp3"
mkdir -p "$destination"

for index in "${!sources[@]}"; do
  track="$(printf '%04d' "$((index + 1))")"
  install -m 0644 "$source_dir/${sources[$index]}" "$destination/$track.mp3"
  echo "${sources[$index]} -> mp3/$track.mp3"
done

sync
echo "DFPlayer SD layout ready at: $destination"
