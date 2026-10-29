#!/bin/bash
# Run ONCE on macOS. The source app is only read, never changed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
command -v python3 >/dev/null || { echo "Falta Python 3: brew install python"; exit 1; }
SOURCE="${1:-/Applications/VLC.app}"
exec python3 "$ROOT/support/vlc_runtime.py" import \
    "$SOURCE" "$ROOT/vendor/VLCRuntime" --arch "$(uname -m)"
