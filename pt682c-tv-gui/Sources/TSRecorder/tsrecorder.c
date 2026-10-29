/* SPDX-License-Identifier: GPL-2.0
 * Byte-preserving loopback relay and MPEG-TS recorder. No VLC/USB dependencies.
 */
#include "tsrecorder.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
/* Serializes start/shutdown and joining, independently of recording writes. */
static pthread_mutex_t lifecycle_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static int running;
static int stopping;
static int udp_fd = -1;
static int record_fd = -1;
static int recording_failed;
static uint16_t input;
static uint16_t output;
static uint64_t recorded;
static uint64_t forward_errors;
static char error_text[1024] = "OK";
static struct sockaddr_in destination;

static void fail_locked(const char *operation, int code)
{
    snprintf(error_text, sizeof error_text, "%s: %s", operation, strerror(code));
}

static void close_recording_locked(void)
{
    if (record_fd < 0)
        return;
    int fd = record_fd;
    record_fd = -1;
    /* Do not retry close on EINTR: descriptor ownership may already be gone. */
    if (close(fd) < 0) {
        recording_failed = 1;
        fail_locked("Error cerrando la grabacion", errno);
    }
}

static void *relay_loop(void *unused)
{
    (void)unused;
    unsigned char data[65536];
    for (;;) {
        pthread_mutex_lock(&lock);
        int finish = stopping;
        int fd = udp_fd;
        pthread_mutex_unlock(&lock);
        if (finish)
            break;
        struct pollfd descriptor = {fd, POLLIN, 0};
        int ready = poll(&descriptor, 1, 100);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
            continue;
        ssize_t count = recv(fd, data, sizeof data, 0);
        if (count <= 0)
            continue;
        /* Forward before disk I/O. Only this thread owns datagram ordering. */
        ssize_t sent = sendto(fd, data, (size_t)count, 0,
                              (struct sockaddr *)&destination, sizeof destination);
        pthread_mutex_lock(&lock);
        if (sent != count)
            forward_errors++;
        if (record_fd >= 0) {
            size_t position = 0;
            while (position < (size_t)count) {
                ssize_t n = write(record_fd, data + position, (size_t)count - position);
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0) {
                    recording_failed = 1;
                    fail_locked("Error escribiendo TS (grabacion detenida)",
                                n < 0 ? errno : EIO);
                    close_recording_locked();
                    break;
                }
                position += (size_t)n;
                recorded += (uint64_t)n;
            }
        }
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int tsrelay_start(uint16_t input_port, uint16_t output_port)
{
    pthread_mutex_lock(&lifecycle_lock);
    pthread_mutex_lock(&lock);
    if (running) {
        int same = !stopping && input == input_port && output == output_port;
        if (!same)
            snprintf(error_text, sizeof error_text, "El relay ya usa otros puertos.");
        pthread_mutex_unlock(&lock);
        pthread_mutex_unlock(&lifecycle_lock);
        return same ? 0 : -1;
    }
    if (!input_port || !output_port || input_port == output_port) {
        snprintf(error_text, sizeof error_text, "Puertos UDP invalidos.");
        goto failed;
    }
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fail_locked("No se pudo crear el relay UDP", errno);
        goto failed;
    }
    /* Do not inherit this socket into the receiver or other spawned processes. */
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        fail_locked("No se pudo proteger el socket UDP", errno);
        close(fd);
        goto failed;
    }
    int buffer_size = 4 * 1024 * 1024;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof buffer_size);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(input_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&address, sizeof address) < 0) {
        fail_locked("No se pudo reservar UDP 1234; cierra otros receptores/VLC", errno);
        close(fd);
        goto failed;
    }
    memset(&destination, 0, sizeof destination);
    destination.sin_family = AF_INET;
    destination.sin_port = htons(output_port);
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    udp_fd = fd;
    input = input_port;
    output = output_port;
    stopping = 0;
    forward_errors = 0;
    int result = pthread_create(&worker, NULL, relay_loop, NULL);
    if (result != 0) {
        fail_locked("No se pudo iniciar el relay", result);
        close(fd);
        udp_fd = -1;
        goto failed;
    }
    running = 1;
    snprintf(error_text, sizeof error_text, "OK");
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&lifecycle_lock);
    return 0;
failed:
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&lifecycle_lock);
    return -1;
}

void tsrelay_shutdown(void)
{
    pthread_mutex_lock(&lifecycle_lock);
    pthread_mutex_lock(&lock);
    if (!running) {
        close_recording_locked();
        pthread_mutex_unlock(&lock);
        pthread_mutex_unlock(&lifecycle_lock);
        return;
    }
    stopping = 1;
    pthread_mutex_unlock(&lock);
    pthread_join(worker, NULL);
    pthread_mutex_lock(&lock);
    close_recording_locked();
    close(udp_fd);
    udp_fd = -1;
    running = 0;
    stopping = 0;
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&lifecycle_lock);
}

int tsrelay_start_recording(const char *path)
{
    pthread_mutex_lock(&lock);
    if (!running || stopping) {
        snprintf(error_text, sizeof error_text, "El relay UDP no esta iniciado.");
        goto failed;
    }
    if (record_fd >= 0) {
        snprintf(error_text, sizeof error_text, "Ya hay una grabacion activa.");
        goto failed;
    }
    if (!path || !*path) {
        snprintf(error_text, sizeof error_text, "Ruta de grabacion invalida.");
        goto failed;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        fail_locked("No se pudo crear el TS; usa un nombre nuevo y revisa permisos/espacio", errno);
        goto failed;
    }
    record_fd = fd;
    recording_failed = 0;
    recorded = 0;
    snprintf(error_text, sizeof error_text, "OK");
    pthread_mutex_unlock(&lock);
    return 0;
failed:
    pthread_mutex_unlock(&lock);
    return -1;
}

int tsrelay_stop_recording(void)
{
    pthread_mutex_lock(&lock);
    close_recording_locked();
    int result = recording_failed ? -1 : 0;
    pthread_mutex_unlock(&lock);
    return result;
}

int tsrelay_is_recording(void)
{
    pthread_mutex_lock(&lock);
    int result = record_fd >= 0;
    pthread_mutex_unlock(&lock);
    return result;
}

uint64_t tsrelay_recorded_bytes(void)
{
    pthread_mutex_lock(&lock);
    uint64_t result = recorded;
    pthread_mutex_unlock(&lock);
    return result;
}

uint64_t tsrelay_forward_errors(void)
{
    pthread_mutex_lock(&lock);
    uint64_t result = forward_errors;
    pthread_mutex_unlock(&lock);
    return result;
}

void tsrelay_copy_error(char *buffer, size_t size)
{
    if (!buffer || !size)
        return;
    pthread_mutex_lock(&lock);
    snprintf(buffer, size, "%s", error_text);
    pthread_mutex_unlock(&lock);
}
