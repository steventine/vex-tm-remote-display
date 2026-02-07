#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include "http_server.h"
#include "simple-log.h"

struct http_server {
    int port;
    int sockfd;
    frame_callback_t frame_cb;
    void* user_data;
    int running;
    pthread_t server_thread;
};

// HTML page for the web client
static const char* html_page = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"<title>VEX TM Remote Display</title>\n"
"<meta charset=\"UTF-8\">\n"
"<style>\n"
"* { margin: 0; padding: 0; box-sizing: border-box; }\n"
"body { background: #000; font-family: Arial, sans-serif; overflow: hidden; }\n"
".container { width: 100vw; height: 100vh; display: flex; align-items: center; justify-content: center; position: relative; }\n"
".container.cursor-hidden { cursor: none; }\n"
"#stream { max-width: 100vw; max-height: 100vh; width: 100%; height: 100%; object-fit: contain; display: block; }\n"
".controls { position: absolute; top: 1rem; right: 1rem; z-index: 5; transition: opacity 0.3s; display: flex; gap: 0.5rem; }\n"
".controls.hidden { opacity: 0; pointer-events: none; }\n"
".controls.visible { opacity: 1; pointer-events: auto; }\n"
".btn { background: rgba(0,0,0,0.6); color: #fff; border: 2px solid rgba(255,255,255,0.3); border-radius: 4px; padding: 0.75rem 1rem; font-size: 1.5rem; cursor: pointer; transition: all 0.2s; backdrop-filter: blur(4px); }\n"
".btn:hover { background: rgba(0,0,0,0.8); border-color: rgba(255,255,255,0.5); }\n"
".fps { position: absolute; bottom: 1rem; left: 1rem; z-index: 5; background: rgba(0,0,0,0.6); color: #fff; padding: 0.5rem 0.75rem; border-radius: 4px; font-size: 0.9rem; font-weight: 600; font-family: monospace; backdrop-filter: blur(4px); border: 1px solid rgba(255,255,255,0.2); transition: opacity 0.3s; }\n"
".fps.hidden { opacity: 0; pointer-events: none; }\n"
".fps.visible { opacity: 1; pointer-events: auto; }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<div class=\"container\" id=\"container\">\n"
"<img id=\"stream\" src=\"/stream\" alt=\"VEX TM Display\">\n"
"<div class=\"controls hidden\" id=\"controls\">\n"
"<button class=\"btn\" id=\"fullscreen\" title=\"Toggle Fullscreen\">⤢</button>\n"
"</div>\n"
"<div class=\"fps hidden\" id=\"fps\">0.0 FPS</div>\n"
"</div>\n"
"<script>\n"
"let showControls = false, showCursor = true, lastFrameTime = 0, frameCount = 0, fps = 0;\n"
"const container = document.getElementById('container');\n"
"const controls = document.getElementById('controls');\n"
"const fpsDiv = document.getElementById('fps');\n"
"const stream = document.getElementById('stream');\n"
"const fullscreenBtn = document.getElementById('fullscreen');\n"
"\n"
"function updateFPS() {\n"
"  const now = Date.now();\n"
"  if (lastFrameTime > 0) {\n"
"    const dt = (now - lastFrameTime) / 1000;\n"
"    if (dt > 0) fps = 1 / dt;\n"
"  }\n"
"  lastFrameTime = now;\n"
"  fpsDiv.textContent = fps.toFixed(1) + ' FPS';\n"
"}\n"
"\n"
"stream.onload = () => { updateFPS(); frameCount++; };\n"
"stream.onerror = () => { console.error('Stream error'); };\n"
"\n"
"document.addEventListener('mousemove', () => {\n"
"  showControls = true;\n"
"  showCursor = true;\n"
"  controls.classList.remove('hidden');\n"
"  controls.classList.add('visible');\n"
"  fpsDiv.classList.remove('hidden');\n"
"  fpsDiv.classList.add('visible');\n"
"  container.classList.remove('cursor-hidden');\n"
"  clearTimeout(window.hideTimeout);\n"
"  window.hideTimeout = setTimeout(() => {\n"
"    showControls = false;\n"
"    showCursor = false;\n"
"    controls.classList.remove('visible');\n"
"    controls.classList.add('hidden');\n"
"    fpsDiv.classList.remove('visible');\n"
"    fpsDiv.classList.add('hidden');\n"
"    container.classList.add('cursor-hidden');\n"
"  }, 2000);\n"
"});\n"
"\n"
"fullscreenBtn.addEventListener('click', () => {\n"
"  if (!document.fullscreenElement) {\n"
"    document.documentElement.requestFullscreen();\n"
"    fullscreenBtn.textContent = '×';\n"
"  } else {\n"
"    document.exitFullscreen();\n"
"    fullscreenBtn.textContent = '⤢';\n"
"  }\n"
"});\n"
"\n"
"document.addEventListener('fullscreenchange', () => {\n"
"  fullscreenBtn.textContent = document.fullscreenElement ? '×' : '⤢';\n"
"});\n"
"</script>\n"
"</body>\n"
"</html>\n";

struct client_handler_data {
    int client_fd;
    http_server_t* server;
};

static void* handle_client(void* arg) {
    struct client_handler_data* data = (struct client_handler_data*)arg;
    int client_fd = data->client_fd;
    http_server_t* server = data->server;
    free(data);
    
    char buffer[4096];
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    if(n <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[n] = '\0';
    
    // Simple HTTP request parsing
    if(strncmp(buffer, "GET / ", 6) == 0 || strncmp(buffer, "GET / HTTP", 10) == 0) {
        // Serve HTML page
        const char* response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n";
        char header[512];
        snprintf(header, sizeof(header), response, strlen(html_page));
        ssize_t written = write(client_fd, header, strlen(header));
        (void)written; // Ignore return value for simplicity
        written = write(client_fd, html_page, strlen(html_page));
        (void)written;
    } else if(strncmp(buffer, "GET /stream", 11) == 0) {
        // MJPEG stream
        const char* response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=--frame\r\n"
            "Cache-Control: no-cache\r\n"
            "Pragma: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        ssize_t written = write(client_fd, response, strlen(response));
        (void)written; // Ignore return value for simplicity
        
        // Stream frames
        uint8_t* jpeg_data = NULL;
        size_t jpeg_size = 0;
        
        while(server->running) {
            // Get frame from callback
            jpeg_size = server->frame_cb(&jpeg_data, server->user_data);
            
            if(jpeg_size > 0 && jpeg_data != NULL) {
                char frame_header[256];
                int header_len = snprintf(frame_header, sizeof(frame_header),
                    "--frame\r\n"
                    "Content-Type: image/jpeg\r\n"
                    "Content-Length: %zu\r\n"
                    "\r\n", jpeg_size);
                
                if(write(client_fd, frame_header, header_len) < 0) break;
                if(write(client_fd, jpeg_data, jpeg_size) < 0) break;
                if(write(client_fd, "\r\n", 2) < 0) break;
                
                // Free the JPEG data (allocated by callback)
                free(jpeg_data);
                jpeg_data = NULL;
            } else {
                // No frame available, wait a bit
                nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 50 * 1000000}, NULL); // 50ms
            }
        }
        
        if(jpeg_data) free(jpeg_data);
    } else {
        // 404
        const char* response = "HTTP/1.1 404 Not Found\r\n\r\n";
        ssize_t written = write(client_fd, response, strlen(response));
        (void)written; // Ignore return value for simplicity
    }
    
    close(client_fd);
    return NULL;
}

static void* server_thread_func(void* arg) {
    http_server_t* server = (http_server_t*)arg;
    
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    server->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(server->sockfd < 0) {
        error("Failed to create socket: %s", strerror(errno));
        return NULL;
    }
    
    int opt = 1;
    setsockopt(server->sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server->port);
    
    if(bind(server->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        error("Failed to bind socket: %s", strerror(errno));
        close(server->sockfd);
        return NULL;
    }
    
    if(listen(server->sockfd, 5) < 0) {
        error("Failed to listen: %s", strerror(errno));
        close(server->sockfd);
        return NULL;
    }
    
    info("HTTP server listening on port %d", server->port);
    
    server->running = 1;
    while(server->running) {
        int client_fd = accept(server->sockfd, (struct sockaddr*)&client_addr, &client_len);
        if(client_fd < 0) {
            if(server->running) {
                warn("Accept failed: %s", strerror(errno));
            }
            continue;
        }
        
        // Handle client in a new thread
        struct client_handler_data* data = malloc(sizeof(struct client_handler_data));
        data->client_fd = client_fd;
        data->server = server;
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, data);
        pthread_detach(thread);
    }
    
    close(server->sockfd);
    return NULL;
}

http_server_t* http_server_create(int port, frame_callback_t frame_cb, void* user_data) {
    http_server_t* server = malloc(sizeof(http_server_t));
    if(server == NULL) return NULL;
    
    server->port = port;
    server->sockfd = -1;
    server->frame_cb = frame_cb;
    server->user_data = user_data;
    server->running = 0;
    
    return server;
}

void http_server_destroy(http_server_t* server) {
    if(server == NULL) return;
    
    server->running = 0;
    if(server->sockfd >= 0) {
        close(server->sockfd);
    }
    
    if(server->running) {
        pthread_join(server->server_thread, NULL);
    }
    
    free(server);
}

int http_server_run(http_server_t* server) {
    if(server == NULL) return -1;
    
    pthread_create(&server->server_thread, NULL, server_thread_func, server);
    return 0;
}

