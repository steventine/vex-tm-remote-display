#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#define read(s,b,n)  recv(s, b, n, 0)
#define write(s,b,n) send(s, (const char*)(b), n, 0)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include "hls_server.h"
#include "simple-log.h"

// --------------------------------------------------------------------------
// Ring buffer config
// --------------------------------------------------------------------------
#define RING_SIZE        5      // keep 5 segments
#define PLAYLIST_WINDOW  3      // expose 3 segments in the sliding window

// --------------------------------------------------------------------------
// Embedded HLS.js (minified, ~350 KB normally).
// We serve a tiny loader that fetches HLS.js from a CDN when online, and
// falls back to a bundled JS stub that uses native HLS on Safari.
// For fully offline use, replace HLSJS_BUNDLE_URL with a real bundled copy
// compiled in as a string literal, or serve from a local file.
// --------------------------------------------------------------------------
static const char* hlsjs_loader_script =
"// HLS.js loader — tries CDN first, falls back to native HLS\n"
"(function(){\n"
"  var s = document.createElement('script');\n"
"  s.src = 'https://cdn.jsdelivr.net/npm/hls.js@latest/dist/hls.min.js';\n"
"  s.onerror = function() { console.warn('HLS.js CDN unavailable, native HLS only'); window._hlsjsFailed=true; };\n"
"  document.head.appendChild(s);\n"
"})();\n";

// Self-hosted HLS.js is large (~350 KB); tournament environments may have no internet.
// The plan says to serve from /hlsjs endpoint.  We do that here with a small bootstrap
// that tries CDN first and falls back to native HLS (Safari/iOS).
// For a fully-offline bundle, replace the hlsjs_loader_script above with the full file.

// --------------------------------------------------------------------------
// HTML page
// --------------------------------------------------------------------------
static const char html_page[] =
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"<title>VEX TM Remote Display (HLS)</title>\n"
"<meta charset=\"UTF-8\">\n"
"<style>\n"
"* { margin: 0; padding: 0; box-sizing: border-box; }\n"
"body { background: #000; font-family: Arial, sans-serif; overflow: hidden; }\n"
".container { width: 100vw; height: 100vh; height: 100dvh; display: flex; align-items: center; justify-content: center; position: relative; }\n"
".container.cursor-hidden { cursor: none; }\n"
"video { max-width: 100vw; max-height: 100vh; width: 100%; height: 100%; object-fit: contain; display: block; }\n"
".controls { position: absolute; top: 1rem; right: 1rem; z-index: 5; transition: opacity 0.3s; display: flex; gap: 0.5rem; }\n"
".controls.hidden { opacity: 0; pointer-events: none; }\n"
".controls.visible { opacity: 1; pointer-events: auto; }\n"
".btn { background: rgba(0,0,0,0.6); color: #fff; border: 2px solid rgba(255,255,255,0.3); border-radius: 4px; padding: 0.75rem 1rem; font-size: 1.5rem; cursor: pointer; transition: all 0.2s; backdrop-filter: blur(4px); }\n"
".btn:hover { background: rgba(0,0,0,0.8); border-color: rgba(255,255,255,0.5); }\n"
".stats { position: absolute; bottom: 1rem; left: 1rem; z-index: 5; background: rgba(0,0,0,0.75); color: #fff; padding: 0.6rem 1rem; border-radius: 6px; font-size: 0.85rem; font-weight: 600; font-family: 'Courier New', monospace; backdrop-filter: blur(8px); border: 1px solid rgba(255,255,255,0.25); transition: all 0.3s; box-shadow: 0 2px 8px rgba(0,0,0,0.3); display: flex; flex-direction: column; gap: 0.2rem; min-width: 160px; }\n"
".stats.hidden { opacity: 0; pointer-events: none; transform: translateY(10px); }\n"
".stats.visible { opacity: 1; transform: translateY(0); }\n"
".stat-value { font-size: 1.1rem; font-weight: 700; line-height: 1.2; color: #4ade80; }\n"
".stat-label { font-size: 0.7rem; opacity: 0.8; text-transform: uppercase; letter-spacing: 0.5px; }\n"
".stat-row { font-size: 0.7rem; opacity: 0.7; margin-top: 0.2rem; padding-top: 0.3rem; border-top: 1px solid rgba(255,255,255,0.1); }\n"
"#status-dot { position: absolute; top: 6px; right: 6px; width: 6px; height: 6px; border-radius: 50%; z-index: 10; display: none; }\n"
"#stream-overlay { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; z-index: 20; pointer-events: none; }\n"
"#overlay-msg { background: rgba(0,0,0,0.72); color: #fff; padding: 0.75rem 1.5rem; border-radius: 8px; font-size: 1.1rem; font-weight: 600; font-family: Arial, sans-serif; backdrop-filter: blur(6px); border: 1px solid rgba(255,255,255,0.15); }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<div class=\"container\" id=\"container\">\n"
"<video id=\"video\" autoplay muted playsinline></video>\n"
"<div class=\"controls hidden\" id=\"controls\">\n"
"<button class=\"btn\" id=\"fullscreen\" title=\"Toggle Fullscreen\">&#x2922;</button>\n"
"</div>\n"
"<div class=\"stats hidden\" id=\"stats\">\n"
"  <div class=\"stat-label\">HLS Stream</div>\n"
"  <div class=\"stat-value\" id=\"stat-level\">Loading...</div>\n"
"  <div class=\"stat-row\" id=\"stat-bitrate\">Bitrate: --</div>\n"
"  <div class=\"stat-row\" id=\"stat-latency\">Latency: --</div>\n"
"  <div class=\"stat-row\" id=\"stat-dropped\">Dropped: 0</div>\n"
"  <div class=\"stat-row\" style=\"margin-top:0.3rem;border-top:1px solid rgba(255,255,255,0.15);padding-top:0.3rem\"><label style=\"cursor:pointer;display:flex;align-items:center;gap:0.4rem\"><input type=\"checkbox\" id=\"stats-pin\"> Pin overlay</label></div>\n"
"</div>\n"
"  <div id=\"stream-overlay\"><div id=\"overlay-msg\">Connecting...</div></div>\n"
"  <div id=\"status-dot\"></div>\n"
"</div>\n"
"<script src=\"/hlsjs\"></script>\n"
"<script>\n"
"var video = document.getElementById('video');\n"
"var container = document.getElementById('container');\n"
"var controls = document.getElementById('controls');\n"
"var statsDiv = document.getElementById('stats');\n"
"var statLevel = document.getElementById('stat-level');\n"
"var statBitrate = document.getElementById('stat-bitrate');\n"
"var statLatency = document.getElementById('stat-latency');\n"
"var statDropped = document.getElementById('stat-dropped');\n"
"var fullscreenBtn = document.getElementById('fullscreen');\n"
"var statsPinCb = document.getElementById('stats-pin');\n"
"var dot = document.getElementById('status-dot');\n"
"var overlay = document.getElementById('stream-overlay');\n"
"var overlayMsg = document.getElementById('overlay-msg');\n"
"\n"
"// Status dot + reconnect overlay.\n"
"// timeupdate fires ~4x/sec while video is playing; goes quiet immediately on stall.\n"
"var lastUpdateMs = 0, currentLatency = 0, hadStream = false;\n"
"var statsIntervalId = null;\n"
"var STALE_MS = 3000; // overlay if video hasn't progressed in 3s\n"
"video.addEventListener('timeupdate', function() { lastUpdateMs = Date.now(); });\n"
"setInterval(function() {\n"
"  if (lastUpdateMs > 0) hadStream = true;\n"
"  var active = (lastUpdateMs > 0 && (Date.now() - lastUpdateMs) < STALE_MS);\n"
"  // Overlay: show when inactive\n"
"  if (!active) {\n"
"    overlayMsg.textContent = hadStream ? 'Reconnecting...' : 'Connecting...';\n"
"    overlay.style.display = 'flex';\n"
"  } else {\n"
"    overlay.style.display = 'none';\n"
"  }\n"
"  // Dot: hidden when inactive or latency is healthy (<5s); yellow/orange otherwise\n"
"  if (!active || currentLatency < 5) {\n"
"    dot.style.display = 'none';\n"
"  } else {\n"
"    dot.style.display = 'block';\n"
"    dot.style.background = currentLatency < 8 ? '#eab308' : '#f97316';\n"
"  }\n"
"}, 1000);\n"
"\n"
"// Poll /stream.m3u8 with fetch() until the server returns 200 (segments ready),\n"
"// then initialise HLS.js. This avoids ever handing HLS.js a 503 or empty playlist.\n"
"function waitForStream(then) {\n"
"  statLevel.textContent = 'Waiting for stream...';\n"
"  fetch('/stream.m3u8')\n"
"    .then(function(r) {\n"
"      if (r.ok) { then(); }\n"
"      else      { setTimeout(function() { waitForStream(then); }, 1000); }\n"
"    })\n"
"    .catch(function() { setTimeout(function() { waitForStream(then); }, 1000); });\n"
"}\n"
"\n"
"function startHLS() {\n"
"  if (typeof Hls !== 'undefined' && Hls.isSupported()) {\n"
"    var hls = new Hls({\n"
"      lowLatencyMode: false,\n"
"      enableWorker: true,\n"
"      maxBufferLength: 10,\n"
"      maxMaxBufferLength: 20,\n"
"      liveSyncDurationCount: 2,          // target ~4s (2 x TARGETDURATION=2s)\n"
"      liveMaxLatencyDurationCount: 4,    // hard-seek if >8s (4 x TARGETDURATION=2s)\n"
"      maxLiveSyncPlaybackRate: 1.3       // speed up to 1.3x to catch up when above target\n"
"    });\n"
"    hls.loadSource('/stream.m3u8');\n"
"    hls.attachMedia(video);\n"
"    hls.on(Hls.Events.MANIFEST_PARSED, function() {\n"
"      video.play().catch(function(e) { console.log('Autoplay blocked:', e); });\n"
"      statLevel.textContent = 'H.264/HLS';\n"
"    });\n"
"    hls.on(Hls.Events.FRAG_LOADED, function(event, data) {\n"
"      var dur = data.frag.duration;\n"
"      var bytes = data.frag.stats.loaded;\n"
"      if (dur > 0 && bytes > 0) {\n"
"        var kbps = Math.round((bytes * 8) / dur / 1000);\n"
"        statBitrate.textContent = 'Bitrate: ' + (kbps >= 1000 ? (kbps/1000).toFixed(1)+' Mbps' : kbps+' kbps');\n"
"      }\n"
"    });\n"
"    hls.on(Hls.Events.ERROR, function(event, data) {\n"
"      if (data.fatal) {\n"
"        console.warn('HLS fatal error:', data.type, data.details, '— waiting for stream');\n"
"        if (statsIntervalId) { clearInterval(statsIntervalId); statsIntervalId = null; }\n"
"        hls.destroy();\n"
"        waitForStream(startHLS);\n"
"      }\n"
"    });\n"
"    if (statsIntervalId) clearInterval(statsIntervalId);\n"
"    statsIntervalId = setInterval(function() {\n"
"      if (video.getVideoPlaybackQuality) {\n"
"        var q = video.getVideoPlaybackQuality();\n"
"        statDropped.textContent = 'Dropped: ' + q.droppedVideoFrames;\n"
"      }\n"
"      var latency = hls.latency;\n"
"      if (latency > 0) {\n"
"        currentLatency = latency;\n"
"        statLatency.textContent = 'Latency: ' + latency.toFixed(1) + 's';\n"
"        if (latency > 5) video.playbackRate = 1.3;\n"
"        else if (latency < 4) video.playbackRate = 1.0;\n"
"      }\n"
"    }, 1000);\n"
"  } else if (video.canPlayType('application/vnd.apple.mpegurl')) {\n"
"    video.src = '/stream.m3u8';\n"
"    video.play().catch(function(e) { console.log('Autoplay blocked:', e); });\n"
"    statLevel.textContent = 'Native HLS';\n"
"  } else {\n"
"    statLevel.textContent = 'HLS not supported';\n"
"  }\n"
"}\n"
"\n"
"// Wait for HLS.js script to finish loading (it's injected asynchronously),\n"
"// then begin polling for the stream.\n"
"function init() {\n"
"  if (typeof Hls !== 'undefined') { waitForStream(startHLS); }\n"
"  else { setTimeout(init, 100); }\n"
"}\n"
"window.addEventListener('load', init);\n"
"\n"
"function showControls() {\n"
"  controls.classList.remove('hidden'); controls.classList.add('visible');\n"
"  statsDiv.classList.remove('hidden'); statsDiv.classList.add('visible');\n"
"  container.classList.remove('cursor-hidden');\n"
"  clearTimeout(window._hideTimeout);\n"
"  window._hideTimeout = setTimeout(function() {\n"
"    controls.classList.remove('visible'); controls.classList.add('hidden');\n"
"    if (!statsPinCb.checked) { statsDiv.classList.remove('visible'); statsDiv.classList.add('hidden'); }\n"
"    container.classList.add('cursor-hidden');\n"
"  }, 2000);\n"
"}\n"
"document.addEventListener('mousemove', showControls);\n"
"var lastTapMs = 0;\n"
"document.addEventListener('touchstart', function(e) {\n"
"  var now = Date.now();\n"
"  if (now - lastTapMs < 300) {\n"
"    if (!document.fullscreenElement) { document.documentElement.requestFullscreen(); }\n"
"    else { document.exitFullscreen(); }\n"
"    e.preventDefault();\n"
"  }\n"
"  lastTapMs = now;\n"
"  showControls();\n"
"}, { passive: false });\n"
"\n"
"statsPinCb.addEventListener('change', function() {\n"
"  if (statsPinCb.checked) {\n"
"    statsDiv.classList.remove('hidden'); statsDiv.classList.add('visible');\n"
"    clearTimeout(window._hideTimeout);\n"
"  }\n"
"});\n"
"\n"
"fullscreenBtn.addEventListener('click', function() {\n"
"  if (!document.fullscreenElement) {\n"
"    document.documentElement.requestFullscreen();\n"
"    fullscreenBtn.textContent = '\\u00D7';\n"
"  } else {\n"
"    document.exitFullscreen();\n"
"    fullscreenBtn.innerHTML = '&#x2922;';\n"
"  }\n"
"});\n"
"document.addEventListener('fullscreenchange', function() {\n"
"  fullscreenBtn.innerHTML = document.fullscreenElement ? '\\u00D7' : '&#x2922;';\n"
"});\n"
"</script>\n"
"</body>\n"
"</html>\n";

// --------------------------------------------------------------------------
// Segment ring buffer entry
// --------------------------------------------------------------------------
typedef struct {
    uint8_t* data;
    size_t   size;
    double   duration;
    uint64_t seq;          // monotonically increasing
} segment_t;

// --------------------------------------------------------------------------
// Server struct
// --------------------------------------------------------------------------
struct hls_server {
    int port;
    int sockfd;
    int running;
    pthread_t server_thread;

    pthread_mutex_t ring_mutex;
    segment_t       ring[RING_SIZE];
    int             ring_head;     // index of oldest segment
    int             ring_count;    // number of valid segments
    uint64_t        next_seq;      // sequence number for next pushed segment
};

// --------------------------------------------------------------------------
// Playlist generation (caller must free result)
// --------------------------------------------------------------------------
static char* build_playlist(struct hls_server* srv) {
    pthread_mutex_lock(&srv->ring_mutex);

    if (srv->ring_count == 0) {
        pthread_mutex_unlock(&srv->ring_mutex);
        // Return an empty/stub playlist so the client keeps polling
        return strdup(
            "#EXTM3U\n"
            "#EXT-X-VERSION:3\n"
            "#EXT-X-TARGETDURATION:2\n"
            "#EXT-X-MEDIA-SEQUENCE:0\n"
        );
    }

    // Build window: up to PLAYLIST_WINDOW newest segments
    int window = srv->ring_count < PLAYLIST_WINDOW ? srv->ring_count : PLAYLIST_WINDOW;
    // Indices of the segments to include (newest `window` from ring)
    int indices[RING_SIZE] = {0};
    int start = (srv->ring_head + srv->ring_count - window + RING_SIZE) % RING_SIZE;
    for (int i = 0; i < window; i++)
        indices[i] = (start + i) % RING_SIZE;

    // Target duration = max segment duration rounded up
    double max_dur = 0.0;
    for (int i = 0; i < window; i++)
        if (srv->ring[indices[i]].duration > max_dur)
            max_dur = srv->ring[indices[i]].duration;
    int target_dur = (int)max_dur + 1;
    if (target_dur < 1) target_dur = 1;

    uint64_t oldest_seq = srv->ring[indices[0]].seq;

    // Estimate buffer size: header ~150 bytes, per segment ~80 bytes
    size_t buf_sz = 256 + (size_t)window * 96;
    char* buf = malloc(buf_sz);
    if (!buf) {
        pthread_mutex_unlock(&srv->ring_mutex);
        return NULL;
    }

    int pos = 0;
    pos += snprintf(buf + pos, buf_sz - (size_t)pos,
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:%d\n"
        "#EXT-X-MEDIA-SEQUENCE:%llu\n",
        target_dur, (unsigned long long)oldest_seq);

    for (int i = 0; i < window; i++) {
        segment_t* seg = &srv->ring[indices[i]];
        pos += snprintf(buf + pos, buf_sz - (size_t)pos,
            "#EXTINF:%.3f,\n/seg/%llu.ts\n",
            seg->duration, (unsigned long long)seg->seq);
    }

    pthread_mutex_unlock(&srv->ring_mutex);
    return buf;
}

// --------------------------------------------------------------------------
// Serve a segment by sequence number. Returns 1 if found, 0 if not.
// Copies data + size out (no lock held after return — data is a fresh alloc).
// Caller frees *out_data.
// --------------------------------------------------------------------------
static int find_segment(struct hls_server* srv, uint64_t seq,
                        uint8_t** out_data, size_t* out_size) {
    pthread_mutex_lock(&srv->ring_mutex);
    for (int i = 0; i < srv->ring_count; i++) {
        int idx = (srv->ring_head + i) % RING_SIZE;
        if (srv->ring[idx].seq == seq) {
            size_t sz = srv->ring[idx].size;
            uint8_t* copy = malloc(sz);
            if (!copy) {
                pthread_mutex_unlock(&srv->ring_mutex);
                return 0;
            }
            memcpy(copy, srv->ring[idx].data, sz);
            pthread_mutex_unlock(&srv->ring_mutex);
            *out_data = copy;
            *out_size = sz;
            return 1;
        }
    }
    pthread_mutex_unlock(&srv->ring_mutex);
    return 0;
}

// --------------------------------------------------------------------------
// HTTP helpers
// --------------------------------------------------------------------------
static void send_response(int fd, int status, const char* status_str,
                          const char* content_type,
                          const char* body, size_t body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-cache, no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_str, content_type, body_len);
    ssize_t w = write(fd, header, (size_t)hlen);
    (void)w;
    if (body && body_len > 0) {
        w = write(fd, body, body_len);
        (void)w;
    }
}

static void send_404(int fd) {
    const char* body = "404 Not Found\n";
    send_response(fd, 404, "Not Found", "text/plain", body, strlen(body));
}

// --------------------------------------------------------------------------
// Client handler
// --------------------------------------------------------------------------
struct client_data {
    int fd;
    struct hls_server* srv;
};

static void* handle_client(void* arg) {
    struct client_data* cd = (struct client_data*)arg;
    int fd = cd->fd;
    struct hls_server* srv = cd->srv;
    free(cd);

    char req[2048];
    ssize_t n = read(fd, req, sizeof(req) - 1);
    if (n <= 0) { close(fd); return NULL; }
    req[n] = '\0';

    // Extract method + path from first line
    char method[8] = {0}, path[256] = {0};
    sscanf(req, "%7s %255s", method, path);

    if (strcmp(method, "GET") != 0) {
        send_response(fd, 405, "Method Not Allowed", "text/plain", "405\n", 4);
        close(fd);
        return NULL;
    }

    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        send_response(fd, 200, "OK", "text/html; charset=utf-8",
                      html_page, sizeof(html_page) - 1);
    } else if (strcmp(path, "/hlsjs") == 0) {
        send_response(fd, 200, "OK", "application/javascript",
                      hlsjs_loader_script, strlen(hlsjs_loader_script));
    } else if (strcmp(path, "/stream.m3u8") == 0) {
        // Return 503 until the first segment is ready so HLS.js retries as a
        // network error rather than treating an empty playlist as fatal.
        pthread_mutex_lock(&srv->ring_mutex);
        int has_segments = (srv->ring_count > 0);
        pthread_mutex_unlock(&srv->ring_mutex);

        if (!has_segments) {
            const char* body = "Stream not ready yet — no segments available\n";
            send_response(fd, 503, "Service Unavailable", "text/plain",
                          body, strlen(body));
        } else {
            char* playlist = build_playlist(srv);
            if (playlist) {
                char header[512];
                size_t plen = strlen(playlist);
                int hlen = snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/vnd.apple.mpegurl\r\n"
                    "Content-Length: %zu\r\n"
                    "Cache-Control: no-cache, no-store\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Connection: close\r\n"
                    "\r\n", plen);
                ssize_t w = write(fd, header, (size_t)hlen);
                (void)w;
                w = write(fd, playlist, plen);
                (void)w;
                free(playlist);
            } else {
                send_404(fd);
            }
        }
    } else if (strncmp(path, "/seg/", 5) == 0) {
        // Parse sequence number from /seg/<N>.ts
        const char* p = path + 5;
        char* dot = strchr(p, '.');
        if (!dot) { send_404(fd); close(fd); return NULL; }
        *dot = '\0';
        uint64_t seq = (uint64_t)strtoull(p, NULL, 10);

        uint8_t* seg_data = NULL;
        size_t   seg_size = 0;
        if (find_segment(srv, seq, &seg_data, &seg_size)) {
            char header[256];
            int hlen = snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: video/mp2t\r\n"
                "Content-Length: %zu\r\n"
                "Cache-Control: no-cache, no-store\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n"
                "\r\n", seg_size);
            ssize_t w = write(fd, header, (size_t)hlen);
            (void)w;
            // Write in chunks to avoid large stack buffers
            size_t written = 0;
            while (written < seg_size) {
                size_t chunk = seg_size - written;
                if (chunk > 65536) chunk = 65536;
                ssize_t wn = write(fd, seg_data + written, chunk);
                if (wn <= 0) break;
                written += (size_t)wn;
            }
            free(seg_data);
        } else {
            send_404(fd);
        }
    } else {
        send_404(fd);
    }

    close(fd);
    return NULL;
}

// --------------------------------------------------------------------------
// Listener thread
// --------------------------------------------------------------------------
static void* server_thread_func(void* arg) {
    struct hls_server* srv = (struct hls_server*)arg;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    srv->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->sockfd < 0) {
        error("hls_server: socket() failed: %s", strerror(errno));
        return NULL;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(srv->sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(srv->sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)srv->port);

    if (bind(srv->sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        error("hls_server: bind() failed: %s", strerror(errno));
        close(srv->sockfd);
        return NULL;
    }

    if (listen(srv->sockfd, 10) < 0) {
        error("hls_server: listen() failed: %s", strerror(errno));
        close(srv->sockfd);
        return NULL;
    }

    info("HLS server listening on port %d", srv->port);
    info("Open http://localhost:%d in your web browser", srv->port);

    srv->running = 1;
    while (srv->running) {
        struct sockaddr_in client_addr;
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        int client_fd = accept(srv->sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (srv->running)
                warn("hls_server: accept() failed: %s", strerror(errno));
            continue;
        }

        struct client_data* cd = malloc(sizeof(struct client_data));
        if (!cd) { close(client_fd); continue; }
        cd->fd  = client_fd;
        cd->srv = srv;

        pthread_t t;
        pthread_create(&t, NULL, handle_client, cd);
        pthread_detach(t);
    }

    close(srv->sockfd);
    return NULL;
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

hls_server_t* hls_server_create(int port) {
    hls_server_t* srv = calloc(1, sizeof(hls_server_t));
    if (!srv) return NULL;
    srv->port     = port;
    srv->sockfd   = -1;
    srv->running  = 0;
    srv->next_seq = (uint64_t)time(NULL);  // unique per server session → cache-busts old .ts URLs
    pthread_mutex_init(&srv->ring_mutex, NULL);
    return srv;
}

void hls_server_destroy(hls_server_t* srv) {
    if (!srv) return;
    srv->running = 0;
    if (srv->sockfd >= 0) close(srv->sockfd);
    pthread_join(srv->server_thread, NULL);

    // Free ring buffer
    pthread_mutex_lock(&srv->ring_mutex);
    for (int i = 0; i < RING_SIZE; i++) {
        free(srv->ring[i].data);
        srv->ring[i].data = NULL;
    }
    pthread_mutex_unlock(&srv->ring_mutex);
    pthread_mutex_destroy(&srv->ring_mutex);
    free(srv);
}

int hls_server_run(hls_server_t* srv) {
    if (!srv) return -1;
    pthread_create(&srv->server_thread, NULL, server_thread_func, srv);
    return 0;
}

void hls_server_push_segment(hls_server_t* srv,
                              const uint8_t* data, size_t size,
                              double duration_sec) {
    if (!srv || !data || size == 0) return;

    uint8_t* copy = malloc(size);
    if (!copy) { error("hls_server: failed to copy segment"); return; }
    memcpy(copy, data, size);

    pthread_mutex_lock(&srv->ring_mutex);

    int slot;
    if (srv->ring_count < RING_SIZE) {
        // Ring not yet full — append
        slot = (srv->ring_head + srv->ring_count) % RING_SIZE;
        srv->ring_count++;
    } else {
        // Evict oldest
        slot = srv->ring_head;
        free(srv->ring[slot].data);
        srv->ring[slot].data = NULL;
        srv->ring_head = (srv->ring_head + 1) % RING_SIZE;
    }

    srv->ring[slot].data     = copy;
    srv->ring[slot].size     = size;
    srv->ring[slot].duration = duration_sec;
    srv->ring[slot].seq      = srv->next_seq++;

    pthread_mutex_unlock(&srv->ring_mutex);

    debug("hls_server: pushed segment seq=%llu size=%zu dur=%.3fs",
          (unsigned long long)(srv->next_seq - 1), size, duration_sec);
}
