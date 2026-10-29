# PT682C TV 0.6: app autocontenida, DMG e icono

Esta actualizacion conserva la reproduccion, grabacion TS y gestion del USB
de 0.5. Cambia las rutas de datos y el empaquetado para que la app compilada
no necesite la carpeta de fuentes ni Homebrew en el Mac de destino.
Incluye un icono original de TV, fuente PNG y conversion nativa a ICNS.

## Actualizar el proyecto que ya funciona

Cierra PT682C TV. Descarga el ZIP como `pt682c-tv-gui-v06-distribucion.zip`
en Descargas, sin descomprimir. Este bloque combina los fuentes conservando
el VLC que ya importaste:

```bash
cd "$HOME" &&
ditto -x -k "$HOME/Downloads/pt682c-tv-gui-v06-distribucion.zip" \
    "$HOME/pt682c-diagnostic/pt682c-tv-v04" &&
cd "$HOME/pt682c-diagnostic/pt682c-tv-v04/pt682c-tv-gui" &&
bash build-app.sh &&
bash check-vlc.sh &&
bash build-dmg.sh
```

No vuelvas a ejecutar import-vlc.sh si ya tienes vendor/VLCRuntime.
El Mac donde compilas aun necesita las herramientas, Homebrew/libusb,
el runtime importado, el receptor C y el firmware original verificado.
El ZIP contiene fuentes y scripts, no un DMG compilado ni el runtime VLC.
No sube tu app a Apple ni publica o instala nada por su cuenta.

En Apple Silicon, el resultado es:

```text
dist/PT682C-TV-0.6-arm64.dmg
```

El DMG contiene la app, un acceso a Aplicaciones y una guia de instalacion.
Se abre y se arrastra la app a Aplicaciones; no requiere un instalador PKG
con privilegios de administrador. Instalar en la carpeta Aplicaciones del
sistema podria solicitar autenticacion segun los permisos del usuario.

## Autocontenida no es lo mismo que universal

- **Autocontenida:** incluye las dependencias; el otro Mac no necesita los
  fuentes, VLC, Homebrew, Python ni libusb instalados.
- **Universal 2:** incluye arm64 y x86_64 en ejecutables, bibliotecas,
  frameworks y plugins relevantes, no solo en el ejecutable Swift.
- **Notarizada:** el paquete firmado ha pasado por el servicio de Apple.
  Crear un DMG o usar firma ad hoc no hace que este requisito desaparezca.

Estas distinciones siguen la documentacion de Apple sobre
[binarios universales](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary)
y [Developer ID](https://developer.apple.com/developer-id/).
La compilacion normal en tu Mac genera arm64, no Intel automaticamente.

## Que se incluye y donde se escribe

```text
PT682C TV.app/Contents/
    MacOS/PT682CTV
    MacOS/pt682c_tv_cold
    MacOS/pt682c_tv_warm
    MacOS/vlc-neutral
    Frameworks/PT682CDeps/          libusb y su cierre de dependencias
    Frameworks/VLCLibraries/
    PlugIns/VLCPlugins/
    Resources/Firmware/
    Resources/Defaults/last-scan.tsv
    Resources/Legal/
    Resources/PT682CTV.icns
```

La app resuelve estas rutas en tiempo de ejecucion. No lleva un
app-config.json con rutas /Users/... de la maquina del desarrollador.
Canales, respaldos y logs se crean en
`~/Library/Application Support/PT682C TV`. La lista inicial se copia solo
si aun no existe una lista en Application Support. La app no escribe dentro
de su propio bundle ni necesita modificar /Applications.

La lista inicial se toma de tu ultimo escaneo al compilar. Un nuevo escaneo
hecho en esta version se guarda en Application Support, no en la carpeta
data del proyecto. Para sembrar una distribucion con ese ultimo escaneo,
copia conscientemente su TSV al data/last-scan.tsv de los fuentes antes
de compilar. No reemplaces una lista existente sin querer conservarla.

El empaquetador copia libusb sin modificar la instalacion de Homebrew,
reubica sus referencias Mach-O y audita todo el codigo antes de firmar.
Rechaza dependencias ausentes o fuera del bundle, salvo las bibliotecas
del sistema. La comprobacion comprueba tambien el firmware y el icono.

## Crear Universal 2 cuando tengas ambas compilaciones

Necesitas una app autocontenida arm64 y otra x86_64, compiladas con estos
mismos fuentes, el mismo firmware, la misma lista inicial y versiones
compatibles de las dependencias. La via mas sencilla es compilar
nativamente una en Apple Silicon y otra en Intel, con la misma version de VLC.
No basta con copiar vendor/VLCRuntime arm64 a un Mac Intel.

Conserva sus nombres de app dentro de dos carpetas separadas:

```text
builds/AppleSilicon/PT682C TV.app
builds/Intel/PT682C TV.app
```

Desde pt682c-tv-gui:

```bash
python3 support/distribution.py universal \
    "builds/AppleSilicon/PT682C TV.app" \
    "builds/Intel/PT682C TV.app" \
    "builds/Universal/PT682C TV.app" &&
bash build-dmg.sh "builds/Universal/PT682C TV.app"
```

El resultado sera `dist/PT682C-TV-0.6-universal2.dmg`. El combinador extrae
la arquitectura correcta de cada archivo Mach-O, combina con lipo, audita
todas las imagenes en ambas arquitecturas y vuelve a firmar. Nunca modifica
las dos apps de entrada y no acepta una salida ya existente.

Es una fusion estricta: si faltan plugins en una version de VLC o sus datos
son distintos, se detiene. No descarta un modulo silenciosamente ni presenta
una app incompleta como universal. Algunos builds oficiales de VLC por
arquitectura pueden tener inventarios diferentes; en ese caso hay que
proporcionar runtimes compatibles antes de combinar. El proceso no descarga
ni compila automaticamente VLC para la otra arquitectura.

## Developer ID y notarizacion, opcionales para las pruebas locales

Para distribuir publicamente fuera de la tienda, Apple describe el uso de
Developer ID, hardened runtime y sellado de tiempo, seguido de notarizacion
([requisitos de Apple](https://developer.apple.com/documentation/Security/notarizing-macos-software-before-distribution?language=o_4)).
La identidad y las credenciales pertenecen al desarrollador y no vienen
incluidas. No hay que pegarlas en un chat ni en los fuentes.

Si ya tienes un certificado Developer ID Application instalado en tu llavero:

```bash
export CODE_SIGN_IDENTITY="Developer ID Application: TU IDENTIDAD (TEAMID)"
bash build-app.sh &&
bash build-dmg.sh
```

Para una app universal, pasa esa misma identidad al combinador con
`--identity "$CODE_SIGN_IDENTITY"` y conserva la variable al crear el DMG.
Se firman todos los componentes de dentro hacia fuera con la misma identidad,
con hardened runtime y timestamp. Ningun script de este paquete solicita
una contrasena ni envia la app al servicio de Apple.

Tras probar realmente la app firmada, puedes configurar un perfil de llavero
con las herramientas oficiales y ejecutar por tu cuenta:

```bash
xcrun notarytool submit "dist/PT682C-TV-0.6-arm64.dmg" \
    --keychain-profile "PT682C-NOTARY" --wait &&
xcrun stapler staple "dist/PT682C-TV-0.6-arm64.dmg"
```

Ese comando envia el DMG y su contenido a Apple. Sustituye el nombre por
el paquete que quieras enviar y ejecutalo solo cuando quieras hacerlo.
El perfil debe existir previamente. La notarizacion no se considera completa
si el servicio devuelve errores; revisa el log, no desactives Gatekeeper.
El proyecto no incluye una cuenta Apple Developer ni garantiza que todos
los plugins importados cumplan los requisitos de notarizacion sin ajustes.

## Licencias y firmware antes de compartir

Se incluyen textos GPL-2.0 y LGPL-2.1 y los avisos de terceros. Eso no
sustituye la entrega de fuentes correspondientes y demas obligaciones de
VLC, sus plugins, libusb y el receptor. Revisa THIRD-PARTY.md.
La distribucion local incorpora tu firmware verificado; antes de publicarla,
confirma que tienes permiso para redistribuir ese firmware.

## Validacion y limites

En el Mac de compilacion, ejecuta:

```bash
python3 support/distribution.py audit "PT682C TV.app" &&
codesign --verify --deep --strict "PT682C TV.app"
```

Prueba la app copiada a Aplicaciones y, preferentemente, en otro usuario o
Mac sin las dependencias de desarrollo. Comprueba reproduccion, escaneo,
guardado de canales, REC/STOP, cierre y reapertura caliente.
No borres tu proyecto ni desinstales Homebrew solo para probar esta version.

Las pruebas locales del paquete incluyen rutas y politicas de empaquetado,
relays reales C/UDP y fixtures del cargador. Las llamadas Mach-O, la firma,
Swift/AppKit, DMG y el funcionamiento RF requieren validacion nativa en Mac.
El ZIP no certifica compatibilidad de recepcion en Intel ni notarizacion.
