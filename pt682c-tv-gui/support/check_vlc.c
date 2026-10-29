#include <stdio.h>
#include "vlcshim.h"

/* Checks the private runtime only. Does not open the PT682C or start video. */
int main(int argc, char **argv)
{
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "Uso: check-vlc RUNTIME | check-vlc LIBS PLUGINS DATA\n");
        return 2;
    }
    int configured = argc == 2 ? vlcshim_configure_runtime(argv[1])
        : vlcshim_configure_paths(argv[1], argv[2], argv[3]);
    if (configured != 0 || vlcshim_init() != 0) {
        fprintf(stderr, "VLC_RUNTIME_CHECK_FAILED: %s\n", vlcshim_last_error());
        return 1;
    }
    printf("VLC_RUNTIME_CHECK_OK: %s\n", argv[1]);
    printf("No se abrio el USB ni se inicio reproduccion.\n");
    return 0;
}
