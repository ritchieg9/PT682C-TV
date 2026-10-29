#ifndef VLC_SHIM_H
#define VLC_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pequena capa sobre libvlc (API C de VLC 3.x) para reproducir el flujo
 * MPEG-TS del receptor dentro de un NSView de la aplicacion.
 * La biblioteca se carga con dlopen en tiempo de ejecucion, sin enlace
 * en compilacion. Solo se usa el runtime local importado al proyecto/bundle.
 */

/* Configura la carpeta VLCRuntime antes del primer init. No busca VLC externo. */
int vlcshim_configure_runtime(const char *root);
/* Layout convencional del .app: bibliotecas, plugins y datos en carpetas distintas. */
int vlcshim_configure_paths(const char *libraries, const char *plugins, const char *data);

/* Carga libvlc y crea instancia + reproductor. 0 = OK, <0 = error. */
int vlcshim_init(void);

/* URL a reproducir (udp://@127.0.0.1:1234), programa (>0 aplica :program=N)
   y cache de red en milisegundos. Recrea el medio en cada llamada. */
int vlcshim_play(const char *url, int program, int caching_ms);

/* Detiene la reproduccion. */
void vlcshim_stop(void);

/* Vista de AppKit donde libvlc dibuja el video. Llamar desde el hilo principal. */
void vlcshim_set_view(void *nsview);

/* Volumen 0-100. */
void vlcshim_set_volume(int volume);

/* 1 = silencio, 0 = sonido. */
void vlcshim_set_mute(int mute);

/* Estado del reproductor: 3=Playing 4=Paused 5=Stopped 7=Error, -1=sin datos. */
int vlcshim_get_state(void);

/* Ultimo mensaje de error de esta capa (o "OK"). */
const char *vlcshim_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* VLC_SHIM_H */
