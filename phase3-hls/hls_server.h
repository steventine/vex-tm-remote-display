#pragma once

#include <stdint.h>
#include <stddef.h>

// HLS HTTP server: serves playlist, TS segments, and embedded HTML+HLS.js page.
// No libav dependency — pure C sockets.

typedef struct hls_server hls_server_t;

// Create server listening on `port`. Returns NULL on error.
hls_server_t* hls_server_create(int port);

void hls_server_destroy(hls_server_t* srv);

// Start the accept/listener thread. Non-blocking; returns 0 on success.
int hls_server_run(hls_server_t* srv);

// Called from the frame-capture thread each time a TS segment is complete.
// `data` is copied internally; caller can free after this returns.
// `duration_sec` is the measured duration of the segment.
void hls_server_push_segment(hls_server_t* srv,
                              const uint8_t* data, size_t size,
                              double duration_sec);
