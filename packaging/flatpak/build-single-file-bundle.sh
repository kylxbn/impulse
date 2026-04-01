#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST="${SCRIPT_DIR}/com.kylxbn.impulse.yaml"
BUILD_DIR="${ROOT_DIR}/.flatpak-builder/build/com.kylxbn.impulse"
REPO_DIR="${ROOT_DIR}/.flatpak-builder/repo"
BUNDLE_DIR="${ROOT_DIR}/dist"
BUNDLE_PATH="${BUNDLE_DIR}/com.kylxbn.impulse.flatpak"
APP_ID="com.kylxbn.impulse"
BRANCH="stable"

mkdir -p "${BUILD_DIR}" "${REPO_DIR}" "${BUNDLE_DIR}"

if ! flatpak remotes --user --columns=name | grep -Fxq flathub; then
    flatpak remote-add --user --if-not-exists \
        flathub \
        https://flathub.org/repo/flathub.flatpakrepo
fi

flatpak-builder \
    --force-clean \
    --user \
    --install \
    --install-deps-from=flathub \
    --repo="${REPO_DIR}" \
    "${BUILD_DIR}" \
    "${MANIFEST}"

flatpak build-bundle "${REPO_DIR}" "${BUNDLE_PATH}" "${APP_ID}" "${BRANCH}"

printf 'Installed Flatpak: %s\n' "${APP_ID}"
printf 'Bundle created: %s\n' "${BUNDLE_PATH}"
