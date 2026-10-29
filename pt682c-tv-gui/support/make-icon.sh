#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[[ $# -eq 1 ]] || { echo "Uso: bash support/make-icon.sh salida.icns"; exit 1; }
SOURCE="$ROOT/Assets/PT682CTV.png"
if [[ -f "$ROOT/Assets/PT682CTV.icns" && "${REBUILD_ICON:-0}" != "1" ]]; then
    cp "$ROOT/Assets/PT682CTV.icns" "$1"
    exit 0
fi
[[ -f "$SOURCE" ]] || { echo "Falta el icono fuente: $SOURCE"; exit 1; }
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
ICONSET="$WORK/PT682CTV.iconset"
mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$SOURCE" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" "$SOURCE" \
        --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$1"
