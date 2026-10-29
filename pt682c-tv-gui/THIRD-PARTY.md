# Componentes de terceros y redistribución

PT682C TV no es un producto oficial de VideoLAN. Usa libVLC y sus plugins
para reproducir el flujo de televisión dentro de la ventana.

## Motor, plugins y fuentes

El motor libVLC/libVLCcore usa LGPL; la aplicación VLC y algunos módulos usan
GPL. No debe tratarse el conjunto de plugins de VLC.app como si todo tuviera
una licencia permisiva o fuera únicamente LGPL.
VideoLAN explica la distinción y advierte sobre los módulos GPL en su
[FAQ de redistribución](https://wiki.videolan.org/Frequently_Asked_Questions)
y en el [anuncio de licencia del motor](https://www.videolan.org/press/lgpl-libvlc.html).

Al compartir binarios, conserva los avisos de licencia y facilita el código
fuente correspondiente y los derechos exigidos por las licencias aplicables;
incluye también los de las bibliotecas y plugins que distribuyas. Consulta
las [indicaciones legales de VideoLAN](https://www.videolan.org/legal.html).
Este documento no sustituye los textos de licencia ni certifica que un paquete
redistribuido cumpla todos sus requisitos.

El importador conserva Resources, Frameworks y los avisos que ya estén dentro
de VLC.app. Eso NO garantiza que el paquete original contenga todas las fuentes
o todos los textos y avisos necesarios para tu distribución.
Antes de publicar, añade los que falten junto con las fuentes correspondientes.
Conserva `runtime-manifest.json`, que indica versión, arquitectura y archivos.

Referencias oficiales para conseguir la versión y sus fuentes:

- [Sitio de VLC](https://www.videolan.org/vlc/)
- [Descargas para macOS](https://www.videolan.org/vlc/download-macosx.html)
- [Código fuente de VLC](https://www.videolan.org/vlc/download-sources.html)

Los enlaces orientan sobre la obtención del código; no sustituyen por sí solos
todas las obligaciones de entrega de fuentes, incluidas las dependencias.

## Transformaciones locales

El script de importación:

- Copia el runtime sin modificar VLC.app de origen.
- Selecciona la arquitectura del Mac que realiza la importación.
- Reubica las referencias Mach-O a rutas relativas al propio runtime.
- Elimina la caché de plugins que podría corresponder al bundle anterior.
- No incluye los plugins de interfaz de escritorio `libmacosx_plugin.dylib`
  y notificaciones Growl `libosx_notifications_plugin.dylib`; no son la salida
  de video embebida que usamos.
- Sustituye las firmas originales de las copias por firmas ad hoc locales.

No modifica el código fuente de VLC. Las transformaciones de empaquetado sí
quedan reflejadas en el manifiesto y en el script incluido en este proyecto.

## Otros componentes

La capa C y el receptor derivan del proyecto del usuario y de controladores
Linux con licencia GPL. Se conservan sus atribuciones.
`libusb` es otra dependencia, separada de VLC. Desde 0.6 se copia de la instalacion
de Homebrew a la app generada y se reubica, sin cambiar su codigo fuente.
Incluimos el texto LGPL-2.1 en Assets/Legal, junto con GPL-2.0 para el receptor;
al redistribuir, facilita tambien las fuentes correspondientes de las versiones
concretas y los demas derechos exigidos. El
[texto de licencia de libusb](https://raw.githubusercontent.com/libusb/libusb/master/COPYING)
contiene LGPL-2.1 y su aviso de aplicacion con la opcion de versiones posteriores.
Los textos por si solos no completan todas las obligaciones de distribucion.

El firmware del PT682C también es independiente y no se incluye en este ZIP
de fuentes; el constructor copia el archivo verificado del proyecto del usuario
a la app compilada. Confirma los derechos antes de publicar esa app.
No asumas que el derecho a redistribuir VLC concede derechos sobre el firmware.

Las firmas ad hoc son para compilación y pruebas locales. No son firma
Developer ID ni notarización para distribuir una aplicación sin avisos de
Gatekeeper.
El empaquetador permite una identidad Developer ID del propio desarrollador,
pero no la proporciona ni envia automaticamente el paquete a Apple.
