#!/bin/bash
# No requiere conectar el sintonizador ni abrir la aplicacion.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
APP="$ROOT/PT682C TV.app"
if [[ $# -gt 0 ]]; then
    [[ -d "$1" ]] || { echo "Falta runtime en: $1"; exit 1; }
else
    [[ -d "$APP/Contents/Resources/VLCRuntime" ]] || { echo "Falta la app compilada."; exit 1; }
fi
mkdir -p "$ROOT/build-gui"
clang -Wall -Wextra -O2 \
    -I"$ROOT/Sources/VLCShim/include" \
    "$ROOT/support/check_vlc.c" "$ROOT/Sources/VLCShim/vlcshim.c" \
    -o "$ROOT/build-gui/check-vlc"
unset PT682C_LIBVLC VLC_PLUGIN_PATH VLC_DATA_PATH DYLD_LIBRARY_PATH DYLD_FALLBACK_LIBRARY_PATH
if [[ $# -gt 0 ]]; then
    python3 "$ROOT/support/vlc_runtime.py" audit "$1" --arch "$(uname -m)"
    "$ROOT/build-gui/check-vlc" "$1"
else
    python3 "$ROOT/support/vlc_runtime.py" audit-bundle "$APP" --arch "$(uname -m)"
    "$ROOT/build-gui/check-vlc" "$APP/Contents/Frameworks/VLCLibraries" \
        "$APP/Contents/PlugIns/VLCPlugins" "$APP/Contents/Resources/VLCRuntime/share"
fi
