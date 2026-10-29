# PT682C TV GUI 0.6: app autocontenida, icono TV y DMG

Parte de la GUI 0.3 con la corrección de lectura POSIX 0.3.1 que confirmaste
funcional. Conserva la gestión de cierre y reapertura, el video integrado y
el receptor C de tu carpeta v0.4. Conserva el empaquetado de VLC local de 0.4.1
y añade grabacion TS independiente de la reproduccion. Ahora incluye libusb,
firmware y ejecutables dentro de la app; los datos del usuario se guardan
en Application Support. Consulta DISTRIBUTION.md para el DMG, Universal 2,
firma Developer ID, notarizacion y los limites de validacion.

## Actualizar desde la version funcional 0.5

Cierra PT682C TV. Guarda el ZIP nuevo como `pt682c-tv-gui-v06-distribucion.zip`
en Descargas, sin descomprimir. Ejecuta este bloque para combinar los fuentes
con tu carpeta actual, conservar `vendor/VLCRuntime` y reconstruir la app:

```bash
cd "$HOME" &&
ditto -x -k "$HOME/Downloads/pt682c-tv-gui-v06-distribucion.zip" \
    "$HOME/pt682c-diagnostic/pt682c-tv-v04" &&
cd "$HOME/pt682c-diagnostic/pt682c-tv-v04/pt682c-tv-gui" &&
bash build-app.sh &&
bash check-vlc.sh &&
bash build-dmg.sh
```

No vuelvas a importar VLC ni reemplaces manualmente toda la carpeta en Finder.
No se incluye una copia del runtime en este ZIP; se reutiliza la que ya tienes.
No se modifican el firmware original, el receptor C ni la lista del proyecto.
La primera apertura copia la lista inicial a Application Support solo si no
existe una lista guardada alli. Las actualizaciones conservan esa lista.

## Grabar una transmision

- **REC:** disponible tras sintonizar un canal y arrancar su reproduccion.
  Abre un panel para elegir nombre y carpeta del archivo `.ts`, inicialmente
  Peliculas. La copia empieza cuando confirmas Guardar, no al abrir el panel.
- **STOP:** cierra solo la grabacion; el video y el audio siguen funcionando.
  Puedes iniciar otra grabacion con un nombre distinto sin reiniciar el USB.
- **Indicadores:** muestra tiempo transcurrido, bytes guardados y nombre del
  archivo. Si aun no llegaron datos, indica Esperando datos.
- **Mostrar en Finder:** localiza el ultimo TS cerrado.
- **Cambios de canal y escaneo:** bloqueados durante REC para no mezclar
  frecuencias o canales en una misma grabacion. Pulsa STOP para habilitarlos.
- **Detener TV, desconectar o salir normalmente:** tambien finalizan el TS
  antes de parar el reproductor o cerrar la aplicacion.
- **Errores:** no sobrescribe archivos existentes. Si falla una escritura
  (por ejemplo, por falta de espacio), cierra la grabacion, conserva el archivo
  parcial y muestra un error; el relay de video sigue funcionando.

### Que contiene el TS

Se guarda el flujo MPEG-TS original completo de la frecuencia sintonizada,
no solo el programa elegido para visualizar. Puede incluir otros subcanales,
pistas de audio y tablas del mismo multiplex; el nombre identifica lo que
estabas viendo. No es un filtro de un solo programa ni una conversion MP4.
Al abrir el TS en VLC puedes seleccionar el programa correspondiente.

El video en vivo sigue usando el programa seleccionado. No hay recodificacion
y los botones REC/STOP no reinician libvlc ni envian comandos al sintonizador.
El archivo comienza en el punto en que confirmas Guardar; no incluye video anterior
ni captura desde el inicio de un programa. La reproduccion del TS puede necesitar
unos segundos para encontrar las tablas y el siguiente cuadro clave recibidos.
Una mala señal, datagramas perdidos, un disco lento o un cierre forzado pueden
producir una grabacion incompleta. No es una garantia de captura sin perdidas.

### Implementacion y pruebas

El nuevo target C `TSRecorder` escucha exclusivamente en `127.0.0.1:1234`,
copia los datagramas a `127.0.0.1:1235` para el VLC embebido y, durante REC,
escribe esos mismos bytes a un archivo nuevo. No usa sockets con puertos
compartidos ni depende de opciones experimentales de grabacion de libvlc.
La escritura ocurre en el hilo del relay, no en el hilo de la interfaz.
STOP espera a la escritura en curso y cierra el descriptor antes de devolver.
Los archivos se crean privados al usuario (permisos 0600) y el descriptor no se
hereda por procesos hijos.

La suite de Python incluye pruebas del relay real compilado en C, con sockets
UDP locales y paquetes sinteticos: igualdad byte a byte, continuidad tras STOP,
grabaciones sucesivas, cierre, conflicto de puerto, proteccion contra
sobrescritura y error de escritura. Incluye las pruebas anteriores del runtime.
Se ejecuta con `python3 -m unittest discover -s Tests -p 'test_*.py' -v`.
Estas pruebas no sustituyen la compilacion Swift/AppKit, la firma macOS ni
la prueba de recepcion y grabacion en tu Mac con el dispositivo real.

## Resultado

```text
pt682c-tv-v04/
├── core/
├── firmware/dvb-demod-mxl692.fw
├── data/last-scan.tsv
├── pt682c_tv.c
└── pt682c-tv-gui/
    ├── import-vlc.sh
    ├── build-app.sh
    ├── check-vlc.sh
    ├── vendor/
    │   └── VLCRuntime/             ← se crea UNA VEZ con el importador
    │       ├── lib/
    │       ├── plugins/
    │       ├── Frameworks/         ← si el paquete de origen los incluye
    │       ├── Resources/
    │       └── runtime-manifest.json
    └── PT682C TV.app/
        └── Contents/
            ├── Frameworks/VLCLibraries/
            ├── PlugIns/VLCPlugins/
            └── Resources/VLCRuntime/
```

VLC no se descarga ni se incluye ya importado en este ZIP. El primer paso copia
una vez el VLC 3 que ya funciona en tu Mac, sin modificarlo. Después la
compilación no consulta esa instalación: otra persona que reciba el proyecto
con `vendor/VLCRuntime` puede compilar sin tener VLC instalado.
La aplicación usa únicamente su copia incluida. Si falta, informa el error;
no recurre silenciosamente a otro VLC del equipo.

## Si ya importaste VLC y fallo la firma de un JAR

No repitas la importación ni borres `vendor/VLCRuntime`.
Actualiza los fuentes con los archivos de este paquete, conservando esa carpeta,
y ejecuta `bash build-app.sh`. La compilación reutiliza el runtime importado y
lo reempaqueta en el bundle generado. El código queda en Frameworks/PlugIns
y los datos, incluidos los JAR, en Resources. El `.app` generado se reconstruye;
el runtime de vendor, el firmware y los canales no se modifican.

La firma omite los ejecutables principales de los bundles durante el pase de
bibliotecas. Firma cada bundle anidado de dentro hacia fuera y la aplicación
exterior solo al final, sin usar `--deep` como sustituto de ese orden.
La verificación final sí usa `--deep --strict`.

## Instalación en tu Mac

1. Cierra PT682C TV. Conserva tu carpeta funcional `pt682c-tv-gui` como respaldo.
2. Descomprime la nueva carpeta junto a `core/`, `data/` y `firmware/`.
3. Ejecuta:

```bash
cd ~/pt682c-diagnostic/pt682c-tv-v04/pt682c-tv-gui

# Solo la primera vez. Puede tardar varios minutos.
bash import-vlc.sh "/Applications/VLC.app"

# Importado el runtime, ya no necesita acceder al VLC de Applications.
bash build-app.sh

# Comprobacion sin abrir el USB ni iniciar television.
bash check-vlc.sh

open "PT682C TV.app"
```

Si falta Python 3, instala `brew install python`. Los compiladores de Apple,
Homebrew y `libusb` siguen siendo requisitos para compilar. No se requieren
CocoaPods, VLCKit ni una compilación completa del código de VLC.
La actualización no exige desconectar el USB: usa Caliente si el firmware
sigue activo; Frío solo tras una conexión física nueva.

Si `check-vlc.sh` funciona, debe imprimir:

```text
VLC_RUNTIME_CHECK_OK: .../Contents/Frameworks/VLCLibraries
No se abrio el USB ni se inicio reproduccion.
```

Si la importación falla, no sigas compilando ni reinstales VLC a ciegas:
comparte el error completo. El importador conserva el origen y elimina solo
su directorio temporal de trabajo. Nunca pisa un runtime existente.

## Para compartir la app o los fuentes

Para usuarios finales, comparte el DMG generado; no necesitan los fuentes
ni instalar Homebrew, VLC o libusb. El DMG normal tiene la arquitectura del
Mac donde compilaste. Para arm64 + Intel consulta DISTRIBUTION.md.

Incluye `vendor/VLCRuntime` completo junto con los fuentes y los avisos/licencias
correspondientes; revisa `THIRD-PARTY.md` antes de redistribuir binarios.
VideoLAN permite redistribución sujeta a los términos aplicables, pero algunos
plugins son GPL y no todo el paquete es solamente LGPL
([FAQ de VideoLAN](https://wiki.videolan.org/Frequently_Asked_Questions)).

En el otro Mac se ejecuta `bash build-app.sh`, sin importar VLC otra vez.
Las rutas de configuración se generan en ese equipo.
No hace falta compartir `.build/`, `build-gui/` ni el ejecutable `.app` que
compilaste tú; de hecho, compartir esas cachés puede causar errores de rutas.

El runtime importado en un Mac M4 es **arm64**. Se puede usar para compilar
nativamente en otros Mac Apple Silicon compatibles, sujeto a sus versiones de
macOS. Para Intel hay que importar un VLC 3 que contenga x86_64 en un Mac Intel.
La compilacion normal no genera ambas arquitecturas automaticamente.
El combinador opcional requiere las dos apps nativas compatibles; no se
ha validado el hardware en todos los modelos o versiones de macOS.

La carpeta `vendor` puede ocupar cientos de MB: bibliotecas y códecs no son
solo el archivo `libvlc.dylib`.

## Qué hace técnicamente

- Copia bibliotecas, plugins, frameworks y recursos de la misma versión.
- Excluye dos plugins de escritorio ajenos al reproductor embebido (interfaz
  completa de VLC y notificaciones Growl).
- Selecciona la arquitectura actual y normaliza dependencias Mach-O a
  `@loader_path`; las dependencias del sistema Apple siguen siendo del sistema.
- Rechaza dependencias externas no resueltas, enlaces fuera del runtime y
  mezclas de arquitectura, en vez de depender del Mac que hizo la copia.
- Elimina la caché anterior de plugins y registra hashes SHA-256.
- Empaqueta las bibliotecas en `Contents/Frameworks/VLCLibraries`, los plugins
  nativos en `Contents/PlugIns/VLCPlugins`, los frameworks auxiliares en
  `Contents/Frameworks` y los JAR, manifiestos y datos en `Contents/Resources/VLCRuntime`.
- Ajusta nuevamente las referencias relativas al distribuirlas en el bundle,
  sin modificar `vendor/VLCRuntime`.
- Firma las copias con firma ad hoc local y comprueba sus dependencias.
- El código Swift encuentra el runtime respecto al propio bundle; la capa C
  no busca VLC en Applications, Homebrew, HOME ni variables de bibliotecas.

`check-vlc.sh` audita dependencias e inicializa libvlc con rutas privadas,
limpiando las variables de entorno habituales de búsqueda. No elimina,
renombra ni modifica tu VLC instalado.

## Alcance y pendientes

La app generada incluye VLC, libusb y sus dependencias no pertenecientes al
sistema, los receptores y el firmware. La compilacion necesita el proyecto y
sus herramientas; la app instalada no. Los datos y logs de la app se guardan
en `~/Library/Application Support/PT682C TV`.

La firma predeterminada es ad hoc. Existe soporte opcional para una identidad
Developer ID propia, pero no se incluye una cuenta ni se realiza notarizacion
automatica. El firmware tiene su propia situacion de redistribucion,
independiente de VLC; no se incluye en este ZIP de fuentes, pero si se copia
desde tu proyecto a la app generada.
No borres VLC de tu Mac para probar: el cargador ya no dispone de fallback y
la auditoría rechaza referencias externas.

## Verificación realizada y pendiente

La suite incluye pruebas Python de resolución de rutas, rechazo de dependencias,
separación de JAR/código y orden de firma, además de ausencia de fallback al
VLC instalado. Cuatro de ellas compilan y ejercitan la
capa C con una biblioteca de prueba mínima, que NO es VLC ni reproduce video.
También se compiló la capa C en Linux con advertencias tratadas como errores.
Estos resultados no validan el procesamiento real de Mach-O ni el video.

Se incluye una regresión para el orden de `lipo archivo -verify_arch arm64`,
que evita interpretar la ruta del archivo como otro nombre de arquitectura.
Las pruebas de orden de firma y empaquetado usan herramientas simuladas, no
validan la firma real de Apple. La organización respeta la separación entre
código y recursos y el orden de firma descritos por
[Apple TN2206](https://developer.apple.com/library/archive/technotes/tn2206/_index.html).

La importación con `otool/install_name_tool`, firma, compilación Swift y prueba
con el PT682C deben realizarse en tu Mac. Esta versión de empaquetado todavía
no se ha validado físicamente. La compatibilidad con todos los paquetes
VLC 3.x no está garantizada.

Pruebas opcionales:

```bash
python3 -m unittest discover -s Tests -p 'test_*.py' -v
swift test --filter SessionLifecycleTests
```

El lector POSIX 0.3.1 permanece intacto. Los registros de la app estan en
`~/Library/Application Support/PT682C TV/logs/gui-*.txt`.
El modo de desarrollo sin bundle conserva la configuracion local del proyecto.
