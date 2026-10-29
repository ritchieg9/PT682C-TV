#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
TARGET="tv.sh"
FW="$PWD/firmware/dvb-demod-mxl692.fw"
[[ -f "$TARGET" ]] || { echo "Coloca este script junto a tv.sh, dentro de pt682c-tv-v04."; exit 1; }
[[ -f "$FW" ]] || { echo "Falta la carpeta firmware con dvb-demod-mxl692.fw."; exit 1; }
EXPECTED="8f77d5cb0de9111ed63c113cb2fd54a63e07a30040ae0b562cb025ab9c4e600d"
[[ "$(shasum -a 256 "$FW" | awk '{print $1}')" == "$EXPECTED" ]] || { echo "Firmware incorrecto; no se modifica tv.sh."; exit 1; }
if grep -Fq 'FW="${PT682C_FIRMWARE:-$PWD/firmware/dvb-demod-mxl692.fw}"' "$TARGET"; then
 echo "tv.sh ya usa el firmware local. SHA-256 correcto."
 exit 0
fi
TEMP="$(mktemp "./tv.sh.local-fw.XXXXXX")"
cleanup(){ if [[ -f "$TEMP" ]]; then rm -f "$TEMP"; fi; }
trap cleanup EXIT
perl -0777 -pe '
 $n=s{\$HOME/pt682c-diagnostic/firmware/dvb-demod-mxl692\.fw}{\$PWD/firmware/dvb-demod-mxl692.fw}g;
 die "No encontre una unica ruta esperada. tv.sh no sera reemplazado.\n" if $n != 1;
' "$TARGET" > "$TEMP"
bash -n "$TEMP"
grep -Fq 'FW="${PT682C_FIRMWARE:-$PWD/firmware/dvb-demod-mxl692.fw}"' "$TEMP" || {
 echo "La nueva ruta no coincide con lo esperado. Sin cambios."; exit 1;
}
BACKUP="tv.sh.before-local-firmware-$(date +%Y%m%d-%H%M%S)-$$"
cp -p "$TARGET" "$BACKUP"
mv "$TEMP" "$TARGET"
echo "ACTUALIZACION COMPLETA: tv.sh usa firmware/dvb-demod-mxl692.fw de esta carpeta."
echo "Respaldo del script anterior: $BACKUP"
echo "No se modificaron canales, escaneos, fuentes C ni archivos de la carpeta antigua."
echo "Inicio habitual: bash tv.sh | USB recien conectado: bash tv.sh --cold"
