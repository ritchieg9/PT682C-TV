/* SPDX-License-Identifier: GPL-2.0
 * Sustituto neutro de VLC para el receptor en modo aplicacion.
 * El receptor exige VLC_BIN y lanza ese ejecutable al cambiar de canal;
 * en la aplicacion el video lo reproduce libvlc embebido, asi que este
 * programa no hace nada y termina cuando el receptor le envia SIGTERM.
 */
#include <signal.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (;;)
        pause();
    return 0;
}
