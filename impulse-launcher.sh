#!/usr/bin/bash
set -euo pipefail

appdir="/usr/lib/impulse"
entrypoint="$appdir/dist/main/main/index.js"

if [[ ! -f "$entrypoint" ]]; then
  echo "Impulse entrypoint not found at: $entrypoint" >&2
  exit 1
fi

exec /usr/bin/electron --class=impulse --name=impulse "$entrypoint" "$@"
