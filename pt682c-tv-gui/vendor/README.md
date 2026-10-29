# Runtime local de VLC

Ejecuta una sola vez, desde la carpeta de la aplicación:

```bash
bash import-vlc.sh "/Applications/VLC.app"
```

Esto crea `vendor/VLCRuntime/` con la versión de VLC 3 que ya probaste,
sus plugins y dependencias. La importación es local y no descarga nada.
El VLC original no se modifica. La importación puede tardar por el número
de bibliotecas que debe normalizar y firmar.

Comparte esta carpeta junto al proyecto para que otro usuario no tenga que
instalar VLC. El runtime corresponde a una arquitectura concreta:
`arm64` para Apple Silicon o `x86_64` para Intel. No son intercambiables.
Consulta `THIRD-PARTY.md` antes de redistribuir binarios.

No se sobrescribe un runtime existente. Para actualizarlo, respalda o renombra
manualmente `VLCRuntime` y vuelve a importar la nueva versión. No mezcles plugins
de distintas versiones.
