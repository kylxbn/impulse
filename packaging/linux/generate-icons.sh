#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SOURCE_ICON="${ROOT_DIR}/impulse.png"

if [[ ! -f "${SOURCE_ICON}" ]]; then
    echo "missing source icon: ${SOURCE_ICON}" >&2
    exit 1
fi

for size in 16 24 32 48 64 128 256 512; do
    target_dir="${SCRIPT_DIR}/icons/hicolor/${size}x${size}/apps"
    mkdir -p "${target_dir}"
    magick "${SOURCE_ICON}" \
        -background none \
        -resize "${size}x${size}" \
        -gravity center \
        -extent "${size}x${size}" \
        -strip \
        "${target_dir}/impulse.png"
done
