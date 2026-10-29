/* Test fixture only. NOT a media engine and never packaged in the app. */
#include <stdlib.h>
#include <string.h>
static char token;
void *libvlc_new(int argc, const char *const *argv)
{
    (void)argc; (void)argv;
    const char *plugins = getenv("VLC_PLUGIN_PATH");
    return plugins && strstr(plugins, "/plugins") ? &token : NULL;
}
void libvlc_release(void *p) { (void)p; }
void *libvlc_media_new_location(void *p, const char *s) { (void)p; (void)s; return &token; }
void libvlc_media_add_option(void *p, const char *s) { (void)p; (void)s; }
void libvlc_media_release(void *p) { (void)p; }
void *libvlc_media_player_new(void *p) { (void)p; return &token; }
void libvlc_media_player_release(void *p) { (void)p; }
void libvlc_media_player_set_media(void *p, void *m) { (void)p; (void)m; }
#ifndef OMIT_NSOBJECT
void libvlc_media_player_set_nsobject(void *p, void *v) { (void)p; (void)v; }
#endif
int libvlc_media_player_play(void *p) { (void)p; return 0; }
void libvlc_media_player_stop(void *p) { (void)p; }
int libvlc_audio_set_volume(void *p, int v) { (void)p; (void)v; return 0; }
void libvlc_audio_set_mute(void *p, int v) { (void)p; (void)v; }
int libvlc_media_player_get_state(void *p) { (void)p; return 3; }
