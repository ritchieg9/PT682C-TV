#!/bin/bash
# PT682C TV GUI: compila la aplicacion nativa sobre el proyecto pt682c-tv-v04.
# Coloca esta carpeta (pt682c-tv-gui) DENTRO de pt682c-tv-v04 y ejecuta este script.
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_HOME="${PT682C_HOME:-$(dirname "$APP_DIR")}"

echo "== PT682C TV: compilacion =="
command -v swift >/dev/null || { echo "Falta Swift. Ejecuta: xcode-select --install"; exit 1; }
command -v brew >/dev/null || { echo "Falta Homebrew (brew)."; exit 1; }
command -v python3 >/dev/null || { echo "Falta Python 3: brew install python"; exit 1; }
RUNTIME="$APP_DIR/vendor/VLCRuntime"
[[ -f "$RUNTIME/runtime-manifest.json" ]] || {
  echo "Falta el runtime VLC local."
  echo "Ejecuta una vez: bash import-vlc.sh \"/ruta/a/VLC.app\""
  echo "O copia vendor/VLCRuntime del proyecto compartido (misma arquitectura)."
  exit 1
}
echo "Verificando VLC local (sin consultar /Applications)..."
python3 "$APP_DIR/support/vlc_runtime.py" audit "$RUNTIME" --arch "$(uname -m)" --hashes

# --- 1. Proyecto v0.4 ----------------------------------------------------------
for f in pt682c_tv.c core/runtime_cold.h core/runtime_warm.h core/discovery.h \
         core/loader_cold.c core/loader_warm.c data/last-scan.tsv; do
  [[ -f "$SRC_HOME/$f" ]] || {
    echo "Falta: $SRC_HOME/$f"
    echo "Coloca pt682c-tv-gui dentro de la carpeta pt682c-tv-v04."
    exit 1
  }
done

# --- 2. Firmware verificado ----------------------------------------------------
FW="$SRC_HOME/firmware/dvb-demod-mxl692.fw"
[[ -f "$FW" ]] || FW="$HOME/pt682c-diagnostic/firmware/dvb-demod-mxl692.fw"
EXPECTED="8f77d5cb0de9111ed63c113cb2fd54a63e07a30040ae0b562cb025ab9c4e600d"
[[ "$(shasum -a 256 "$FW" | awk '{print $1}')" == "$EXPECTED" ]] \
  || { echo "Firmware incorrecto o no encontrado."; exit 1; }
echo "Firmware: $FW"
echo "Lista de canales: $SRC_HOME/data/last-scan.tsv"

# --- 3. Hijos del receptor (frio y caliente) ----------------------------------
# Mismas banderas de compilacion que tv.sh de tu proyecto v0.4.
BUILD="$APP_DIR/build-gui"
mkdir -p "$BUILD"
USB_PREFIX="$(brew --prefix libusb)"
CLANG_COMMON=(-std=gnu11 -Wall -Wextra -Wno-unused-function -O2
              -Wl,-headerpad_max_install_names
              -I"$USB_PREFIX/include/libusb-1.0"
              -L"$USB_PREFIX/lib" -Wl,-rpath,"$USB_PREFIX/lib" -lusb-1.0)

echo "Compilando hijo caliente (TV_WARM)..."
(cd "$SRC_HOME" && clang "${CLANG_COMMON[@]}" -DTV_WARM pt682c_tv.c -o "$BUILD/pt682c_tv_warm")

echo "Compilando hijo frio..."
(cd "$SRC_HOME" && clang "${CLANG_COMMON[@]}" pt682c_tv.c -o "$BUILD/pt682c_tv_cold")

echo "Compilando VLC neutro..."
clang -O2 "$APP_DIR/support/vlc-neutral.c" -o "$BUILD/vlc-neutral"

# --- 4. Aplicacion Swift -------------------------------------------------------
echo "Compilando la aplicacion (Swift)..."
cd "$APP_DIR"
swift build -c release -Xlinker -headerpad_max_install_names
EXE="$APP_DIR/.build/release/PT682CTV"
[[ -x "$EXE" ]] || { echo "La compilacion Swift fallo."; exit 1; }

# --- 5. Configuracion -----------------------------------------------------------
make_config() {  # $1 = salida, $2 = binarios hijos, $3 = runtime
  python3 - "$1" "$2" "$FW" "$SRC_HOME" "$3" <<'PY'
import json, pathlib, sys
output, children, firmware, root, runtime = sys.argv[1:]
pathlib.Path(output).write_text(json.dumps({
    "childCold": str(pathlib.Path(children) / "pt682c_tv_cold"),
    "childWarm": str(pathlib.Path(children) / "pt682c_tv_warm"),
    "firmware": firmware,
    "channelsFile": str(pathlib.Path(root) / "data/last-scan.tsv"),
    "workDir": root,
    "vlcNeutral": str(pathlib.Path(children) / "vlc-neutral"),
    "vlcRuntime": runtime
}, indent=2) + "\n")
PY
}
make_config "$APP_DIR/app-config.json" "$BUILD" "$RUNTIME"

# --- 6. Paquete .app ------------------------------------------------------------
APPBUNDLE="$APP_DIR/PT682C TV.app"
rm -rf "$APPBUNDLE"
mkdir -p "$APPBUNDLE/Contents/MacOS" "$APPBUNDLE/Contents/Resources" \
         "$APPBUNDLE/Contents/Frameworks"
cp "$EXE" "$APPBUNDLE/Contents/MacOS/PT682CTV"
cp "$BUILD/pt682c_tv_cold" "$BUILD/pt682c_tv_warm" "$BUILD/vlc-neutral" \
   "$APPBUNDLE/Contents/MacOS/"
python3 "$APP_DIR/support/vlc_runtime.py" bundle "$RUNTIME" "$APPBUNDLE" --arch "$(uname -m)"
cp "$APP_DIR/THIRD-PARTY.md" "$APPBUNDLE/Contents/Resources/"
mkdir -p "$APPBUNDLE/Contents/Resources/Firmware" \
         "$APPBUNDLE/Contents/Resources/Defaults" "$APPBUNDLE/Contents/Resources/Legal"
cp "$FW" "$APPBUNDLE/Contents/Resources/Firmware/dvb-demod-mxl692.fw"
cp "$SRC_HOME/data/last-scan.tsv" "$APPBUNDLE/Contents/Resources/Defaults/last-scan.tsv"
cp "$APP_DIR/Assets/Legal/"* "$APPBUNDLE/Contents/Resources/Legal/"
bash "$APP_DIR/support/make-icon.sh" "$APPBUNDLE/Contents/Resources/PT682CTV.icns"
python3 "$APP_DIR/support/distribution.py" bundle-deps "$APPBUNDLE" --arch "$(uname -m)"
python3 "$APP_DIR/support/distribution.py" manifest "$APPBUNDLE" --arch "$(uname -m)"
cat > "$APPBUNDLE/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>PT682CTV</string>
    <key>CFBundleIdentifier</key><string>local.pt682c.tv</string>
    <key>CFBundleName</key><string>PT682C TV</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.6</string>
    <key>CFBundleVersion</key><string>6</string>
    <key>CFBundleIconFile</key><string>PT682CTV</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF
echo "Firmando bibliotecas y componentes incluidos..."
python3 "$APP_DIR/support/distribution.py" audit "$APPBUNDLE"
python3 "$APP_DIR/support/distribution.py" sign "$APPBUNDLE" \
    --identity "${CODE_SIGN_IDENTITY:--}"
python3 "$APP_DIR/support/vlc_runtime.py" audit-bundle "$APPBUNDLE" --arch "$(uname -m)"

echo
echo "LISTO."
echo "Aplicacion: $APPBUNDLE"
echo "Abrela con: open \"$APPBUNDLE\""
echo "APP AUTOCONTENIDA: VLC, libusb, receptores y firmware incluidos."
echo "Datos: ~/Library/Application Support/PT682C TV"
echo "Arquitectura: $(uname -m). Universal Intel + Apple Silicon requiere ambas compilaciones."
echo "Para crear el DMG: bash build-dmg.sh"
if [[ -z "${CODE_SIGN_IDENTITY:-}" ]]; then
  echo "Firma ad hoc para pruebas. La distribucion publica requiere revisar licencias, Developer ID y notarizacion."
fi
