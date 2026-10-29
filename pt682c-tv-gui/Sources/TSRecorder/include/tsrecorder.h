#ifndef PT682C_TSRECORDER_H
#define PT682C_TSRECORDER_H

#include <stddef.h>
#include <stdint.h>

/* Single local UDP relay: receiver -> input -> output (embedded VLC).
   No shared bind/reuse: a second receiver/app fails explicitly.
   Recordings contain the original full multiplex, without transcoding. */
int tsrelay_start(uint16_t input_port, uint16_t output_port);
void tsrelay_shutdown(void);

/* Creates a NEW file with mode 0600. Never truncates or overwrites a file.
   Serialized with the worker: stop returns after the last write and close. */
int tsrelay_start_recording(const char *path);
int tsrelay_stop_recording(void);
int tsrelay_is_recording(void);
uint64_t tsrelay_recorded_bytes(void);
uint64_t tsrelay_forward_errors(void);
void tsrelay_copy_error(char *buffer, size_t size);

#endif
