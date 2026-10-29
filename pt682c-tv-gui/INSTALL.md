# Instalar PT682C TV 0.6

Abre el DMG, arrastra **PT682C TV** a **Applications** y espera a que termine
la copia. Expulsa el DMG y abre la aplicacion desde Aplicaciones.
No la ejecutes directamente desde la imagen de disco.

Esta version incluye VLC, libusb, los ejecutables del receptor, el firmware
verificado y el icono de television. No necesitas Homebrew, herramientas de
compilacion, VLC instalado ni la carpeta del proyecto en el Mac de destino.
Necesitas macOS 13 o posterior, el PT682C compatible y una antena.
La recepcion depende de la señal disponible y la ubicacion de la antena.

## Arquitectura del paquete

- **arm64:** Mac con Apple Silicon (familias M).
- **x86_64:** Mac Intel.
- **universal2:** contiene codigo nativo para ambas arquitecturas.

Un DMG no convierte por si solo una app arm64 en una app universal.
Apple exige incluir ambas arquitecturas en todo el codigo relevante
([binarios universales](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary)).

## Primer uso

Selecciona Frio despues de conectar fisicamente el receptor; usa Caliente
para reabrirlo mientras conserve el firmware activo. Conecta y selecciona
un canal, o realiza un escaneo para tu ubicacion.
La lista inicial procede del equipo donde se construyo esta app y puede
no corresponder a tu ciudad o antena.

REC guarda un archivo TS en la carpeta y con el nombre que elijas; STOP
finaliza la grabacion sin detener la TV. El TS contiene la frecuencia
completa, que puede incluir mas de un programa.

## Datos propios de cada usuario

Canales, respaldos y diagnosticos se guardan fuera de la app:

```text
~/Library/Application Support/PT682C TV/
    data/last-scan.tsv
    logs/
```

Las actualizaciones no reemplazan un escaneo ya guardado en esa carpeta.
Las grabaciones permanecen en la ubicacion que elegiste.
Para desinstalar, elimina la app de Aplicaciones; tus datos se conservan.
No elimines Application Support ni las grabaciones si quieres conservarlas.

## Firma y seguridad

Un paquete construido sin una identidad Developer ID usa firma ad hoc de
pruebas y puede ser rechazado por Gatekeeper al descargarlo en otro Mac.
Este proyecto no solicita desactivar Gatekeeper ni retirar la cuarentena.
Para una distribucion publica normal, el desarrollador debe preparar la
firma Developer ID y la notarizacion de Apple
([distribucion fuera de la tienda](https://developer.apple.com/developer-id/)).

Los avisos de terceros estan en los recursos de la app. El distribuidor debe
revisar sus obligaciones de licencia y su permiso de redistribuir el firmware.
