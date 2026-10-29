/* SPDX-License-Identifier: GPL-2.0
 * Capa minima sobre libvlc (VLC 3.x) con dlopen, para video embebido en macOS.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vlcshim.h"

typedef struct libvlc_instance_t libvlc_instance_t;
typedef struct libvlc_media_t libvlc_media_t;
typedef struct libvlc_media_player_t libvlc_media_player_t;

static void *lib = NULL;
static libvlc_instance_t *instance = NULL;
static libvlc_media_player_t *player = NULL;
static void *view = NULL;
static char last_error[4096] = "sin inicializar";
static char library_root[4096] = {0};
static char plugin_root[4096] = {0};
static char data_root[4096] = {0};
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static libvlc_instance_t *(*sym_instance_new)(int, const char *const *);
static void (*sym_instance_release)(libvlc_instance_t *);
static libvlc_media_t *(*sym_media_new_location)(libvlc_instance_t *, const char *);
static void (*sym_media_add_option)(libvlc_media_t *, const char *);
static void (*sym_media_release)(libvlc_media_t *);
static libvlc_media_player_t *(*sym_player_new)(libvlc_instance_t *);
static void (*sym_player_release)(libvlc_media_player_t *);
static void (*sym_player_set_media)(libvlc_media_player_t *, libvlc_media_t *);
static void (*sym_player_set_nsview)(libvlc_media_player_t *, void *);
static int (*sym_player_play)(libvlc_media_player_t *);
static void (*sym_player_stop)(libvlc_media_player_t *);
static int (*sym_audio_set_volume)(libvlc_media_player_t *, int);
static void (*sym_audio_set_mute)(libvlc_media_player_t *, int);
static int (*sym_player_get_state)(libvlc_media_player_t *);

static int resolve(const char *name, void **slot)
{
    *slot = dlsym(lib, name);
    if (!*slot) {
        snprintf(last_error, sizeof last_error, "Simbolo ausente: %s", name);
        return -1;
    }
    return 0;
}

static int load_symbols(void)
{
    if (resolve("libvlc_new", (void **)&sym_instance_new) < 0 ||
        resolve("libvlc_release", (void **)&sym_instance_release) < 0 ||
        resolve("libvlc_media_new_location", (void **)&sym_media_new_location) < 0 ||
        resolve("libvlc_media_add_option", (void **)&sym_media_add_option) < 0 ||
        resolve("libvlc_media_release", (void **)&sym_media_release) < 0 ||
        resolve("libvlc_media_player_new", (void **)&sym_player_new) < 0 ||
        resolve("libvlc_media_player_release", (void **)&sym_player_release) < 0 ||
        resolve("libvlc_media_player_set_media", (void **)&sym_player_set_media) < 0 ||
        resolve("libvlc_media_player_set_nsobject", (void **)&sym_player_set_nsview) < 0 ||
        resolve("libvlc_media_player_play", (void **)&sym_player_play) < 0 ||
        resolve("libvlc_media_player_stop", (void **)&sym_player_stop) < 0 ||
        resolve("libvlc_audio_set_volume", (void **)&sym_audio_set_volume) < 0 ||
        resolve("libvlc_audio_set_mute", (void **)&sym_audio_set_mute) < 0 ||
        resolve("libvlc_media_player_get_state", (void **)&sym_player_get_state) < 0)
        return -1;
    return 0;
}

int vlcshim_configure_paths(const char *libraries, const char *plugins, const char *data)
{
    pthread_mutex_lock(&mutex);
    if (instance) {
        snprintf(last_error, sizeof last_error, "El runtime ya esta inicializado.");
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    if (!libraries || !*libraries || strlen(libraries) >= sizeof library_root - 64 ||
        !plugins || !*plugins || strlen(plugins) >= sizeof plugin_root - 64 ||
        !data || !*data || strlen(data) >= sizeof data_root - 64) {
        snprintf(last_error, sizeof last_error, "Ruta de runtime local invalida.");
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    snprintf(library_root, sizeof library_root, "%s", libraries);
    snprintf(plugin_root, sizeof plugin_root, "%s", plugins);
    snprintf(data_root, sizeof data_root, "%s", data);
    pthread_mutex_unlock(&mutex);
    return 0;
}

int vlcshim_configure_runtime(const char *root)
{
    if (!root || !*root || strlen(root) >= sizeof library_root - 128)
        return -1;
    char libraries[4096], plugins[4096], data[4096];
    snprintf(libraries, sizeof libraries, "%s/lib", root);
    snprintf(plugins, sizeof plugins, "%s/plugins", root);
    snprintf(data, sizeof data, "%s/share", root);
    return vlcshim_configure_paths(libraries, plugins, data);
}

int vlcshim_init(void)
{
    pthread_mutex_lock(&mutex);
    if (instance) {
        pthread_mutex_unlock(&mutex);
        return 0;
    }

    if (!library_root[0]) {
        snprintf(last_error, sizeof last_error, "Falta configurar el runtime VLC local.");
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    char unversioned[4160], versioned[4160];
    snprintf(unversioned, sizeof unversioned, "%s/libvlc.dylib", library_root);
    snprintf(versioned, sizeof versioned, "%s/libvlc.5.dylib", library_root);
    struct stat info;
    if (stat(plugin_root, &info) != 0 || !S_ISDIR(info.st_mode)) {
        snprintf(last_error, sizeof last_error,
                 "Faltan plugins del runtime local. Ejecuta import-vlc.sh y recompila.");
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    setenv("VLC_PLUGIN_PATH", plugin_root, 1);
    if (stat(data_root, &info) == 0 && S_ISDIR(info.st_mode))
        setenv("VLC_DATA_PATH", data_root, 1);
    else
        unsetenv("VLC_DATA_PATH");
    const char *candidates[] = {unversioned, versioned};
    char load_error[2048] = "sin detalle";
    for (int i = 0; i < 2; i++) {
        lib = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if (lib)
            break;
        const char *reason = dlerror();
        if (reason)
            snprintf(load_error, sizeof load_error, "%s", reason);
    }
    if (!lib) {
        snprintf(last_error, sizeof last_error,
                 "No se pudo cargar el VLC local: %s", load_error);
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    if (load_symbols() < 0) {
        dlclose(lib);
        lib = NULL;
        pthread_mutex_unlock(&mutex);
        return -2;
    }

    const char *vlc_args[] = {
        "--verbose=2",
        "--no-video-title-show",
        "--vout=macosx"
    };

    instance = sym_instance_new(
        (int)(sizeof(vlc_args) / sizeof(vlc_args[0])),
        vlc_args
    );
    if (!instance) {
        snprintf(last_error, sizeof last_error, "libvlc_new fallo");
        dlclose(lib);
        lib = NULL;
        pthread_mutex_unlock(&mutex);
        return -3;
    }

    player = sym_player_new(instance);
    if (!player) {
        snprintf(last_error, sizeof last_error, "libvlc_media_player_new fallo");
        sym_instance_release(instance);
        instance = NULL;
        dlclose(lib);
        lib = NULL;
        pthread_mutex_unlock(&mutex);
        return -4;
    }

    if (view)
        sym_player_set_nsview(player, view);

    snprintf(last_error, sizeof last_error, "OK");
    pthread_mutex_unlock(&mutex);
    return 0;
}

int vlcshim_play(const char *url, int program, int caching_ms)
{
    pthread_mutex_lock(&mutex);
    if (!player) {
        snprintf(last_error, sizeof last_error, "vlcshim_init no se ha ejecutado");
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    libvlc_media_t *media = sym_media_new_location(instance, url);
    if (!media) {
        snprintf(last_error, sizeof last_error, "No se pudo crear el medio");
        pthread_mutex_unlock(&mutex);
        return -2;
    }

    char option[64];
    if (caching_ms > 0) {
        snprintf(option, sizeof option, ":network-caching=%d", caching_ms);
        sym_media_add_option(media, option);
    }
    /* Seleccion de programa en flujos multiprograma (opcion documentada de VLC). */
    if (program > 0) {
        snprintf(option, sizeof option, ":program=%d", program);
        sym_media_add_option(media, option);
    }

    sym_player_stop(player);
    sym_player_set_media(player, media);
    sym_media_release(media);
    if (view)
        sym_player_set_nsview(player, view);
    int rc = sym_player_play(player);
    if (rc < 0)
        snprintf(last_error, sizeof last_error, "libvlc_media_player_play fallo");
    pthread_mutex_unlock(&mutex);
    return rc;
}

void vlcshim_stop(void)
{
    pthread_mutex_lock(&mutex);
    if (player)
        sym_player_stop(player);
    pthread_mutex_unlock(&mutex);
}

void vlcshim_set_view(void *nsview)
{
    pthread_mutex_lock(&mutex);
    view = nsview;
    if (player && view)
        sym_player_set_nsview(player, view);
    pthread_mutex_unlock(&mutex);
}

void vlcshim_set_volume(int volume)
{
    pthread_mutex_lock(&mutex);
    if (player)
        sym_audio_set_volume(player, volume);
    pthread_mutex_unlock(&mutex);
}

void vlcshim_set_mute(int mute)
{
    pthread_mutex_lock(&mutex);
    if (player)
        sym_audio_set_mute(player, mute ? 1 : 0);
    pthread_mutex_unlock(&mutex);
}

int vlcshim_get_state(void)
{
    int state = -1;
    pthread_mutex_lock(&mutex);
    if (player)
        state = sym_player_get_state(player);
    pthread_mutex_unlock(&mutex);
    return state;
}

const char *vlcshim_last_error(void)
{
    return last_error;
}
