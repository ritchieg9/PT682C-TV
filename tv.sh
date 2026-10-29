#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
BOOT="warm"
if [[ $# -gt 0 ]]; then
 [[ $# -eq 1 && "$1" == "--cold" ]] || { echo "Uso: bash tv.sh [--cold]"; exit 1; }
 BOOT="cold"
fi
mkdir -p build data logs
DB="data/last-scan.tsv"
echo
echo "PT682C TV v0.4 - Lista guardada y nuevo escaneo"
if [[ -s "$DB" ]]; then
 COUNT="$(awk '!/^#/ && NF {n++} END {print n+0}' "$DB")"
 echo "Lista disponible: $COUNT entradas"
 head -n 1 "$DB"
else
 echo "No hay lista guardada. Elige nuevo escaneo."
fi
echo
echo "1) Usar la lista guardada"
echo "2) Nuevo escaneo y guardar programas"
echo "q) Salir"
read -r -p "Opcion [1]: " OPTION
OPTION="${OPTION:-1}"
case "$OPTION" in
 1) [[ -s "$DB" ]] || { echo "Primero realiza un escaneo."; exit 1; }; ACTION="--play" ;;
 2) ACTION="--scan" ;;
 q|Q) exit 0 ;;
 *) echo "Opcion invalida"; exit 1 ;;
esac
FW="${PT682C_FIRMWARE:-$PWD/firmware/dvb-demod-mxl692.fw}"
[[ -f "$FW" ]] || { echo "Firmware no encontrado: $FW"; exit 1; }
EXPECTED="8f77d5cb0de9111ed63c113cb2fd54a63e07a30040ae0b562cb025ab9c4e600d"
[[ "$(shasum -a 256 "$FW" | awk '{print $1}')" == "$EXPECTED" ]] || { echo "SHA-256 incorrecto; dispositivo sin tocar."; exit 1; }
if [[ -z "${VLC_BIN:-}" ]]; then
 for APP in /Applications/VLC.app "$HOME/Applications/VLC.app"; do
  for NAME in VLC vlc; do
   if [[ -x "$APP/Contents/MacOS/$NAME" ]]; then VLC_BIN="$APP/Contents/MacOS/$NAME";break 2;fi
  done
 done
fi
[[ -n "${VLC_BIN:-}" && -x "$VLC_BIN" ]] || { echo "No encuentro VLC. Define VLC_BIN con su ejecutable."; exit 1; }
export VLC_BIN
USB_PREFIX="$(brew --prefix libusb)"
DEFINE=""
[[ "$BOOT" == "warm" ]] && DEFINE="-DTV_WARM"
clang -std=gnu11 -Wall -Wextra -Wno-unused-function -O2 $DEFINE pt682c_tv.c \
-I"$USB_PREFIX/include/libusb-1.0" -L"$USB_PREFIX/lib" -Wl,-rpath,"$USB_PREFIX/lib" \
-lusb-1.0 -o "build/pt682c_tv_$BOOT"
echo
echo "Cierra los receptores anteriores y VLC. Conserva la posicion de la antena."
if [[ "$BOOT" == "warm" ]]; then
 echo "REAPERTURA: USB conectado con firmware activo. Reinicia puente y reutiliza firmware."
 echo "Si acabas de conectarlo fisicamente, cancela y usa: bash tv.sh --cold"
else
 echo "INICIO EN FRIO: usa este modo solo despues de conectar/reconectar fisicamente el PT682C."
 echo "Cargara firmware. Para siguientes aperturas usa bash tv.sh sin --cold."
fi
if [[ "$ACTION" == "--scan" ]]; then
 echo "Escaneo: RF 2-51 excepto 37; puede tardar varios minutos. No reemplaza la lista sin GUARDAR."
fi
read -r -p "Escribe INICIAR para continuar, o Enter para cancelar: " CONFIRM
[[ "$CONFIRM" == "INICIAR" ]] || exit 0
LOG="logs/tv-$(date +%Y%m%d-%H%M%S)-$$.txt"
"./build/pt682c_tv_$BOOT" "$FW" "$ACTION" 2>&1 | tee "$LOG"
