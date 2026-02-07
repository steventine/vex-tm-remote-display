#pragma once

#include <stdint.h>

// HTTP server for MJPEG streaming
typedef struct http_server http_server_t;

// Callback function type for getting JPEG frame data
// Returns size of JPEG data, or 0 if no frame available
typedef size_t (*frame_callback_t)(uint8_t** jpeg_data, void* user_data);

http_server_t* http_server_create(int port, frame_callback_t frame_cb, void* user_data);
void http_server_destroy(http_server_t* server);
int http_server_run(http_server_t* server); // Returns 0 on success, non-zero on error

