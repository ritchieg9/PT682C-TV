#!/bin/bash
# Ejecuta la aplicacion sin generar el paquete .app (modo desarrollo).
# Requiere app-config.json en esta carpeta (lo genera build-app.sh).
set -euo pipefail
cd "$(dirname "$0")"
[[ -f app-config.json ]] || { echo "Falta app-config.json. Ejecuta antes: bash build-app.sh"; exit 1; }
swift build
.build/debug/PT682CTV
