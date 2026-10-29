#!/bin/bash
# Creates a drag-to-Applications disk image, not a root/admin package installer.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
APP="${1:-$ROOT/PT682C TV.app}"
[[ -d "$APP" ]] || { echo "Primero ejecuta: bash build-app.sh"; exit 1; }
python3 "$ROOT/support/distribution.py" audit "$APP"
codesign --verify --deep --strict "$APP"
ARCHS="$(lipo "$APP/Contents/MacOS/PT682CTV" -archs)"
case "$ARCHS" in
  *arm64*x86_64*|*x86_64*arm64*) LABEL="universal2" ;;
  *arm64*) LABEL="arm64" ;;
  *x86_64*) LABEL="x86_64" ;;
  *) echo "Arquitectura no soportada: $ARCHS"; exit 1 ;;
esac
mkdir -p "$ROOT/dist"
DMG="$ROOT/dist/PT682C-TV-0.6-$LABEL.dmg"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
ditto "$APP" "$WORK/PT682C TV.app"
ln -s /Applications "$WORK/Applications"
cp "$ROOT/INSTALL.md" "$WORK/LEEME.md"
hdiutil create -ov -volname "PT682C TV" -srcfolder "$WORK" -format UDZO "$DMG"
if [[ -n "${CODE_SIGN_IDENTITY:-}" ]]; then
    codesign --force --sign "$CODE_SIGN_IDENTITY" --timestamp "$DMG"
fi
echo
echo "DMG CREADO: $DMG"
echo "Instalacion: abrir DMG y arrastrar PT682C TV a Applications."
echo "El otro Mac no necesita Homebrew, VLC, libusb ni los fuentes."
echo "Este comando no envia archivos a Apple ni realiza notarizacion."
