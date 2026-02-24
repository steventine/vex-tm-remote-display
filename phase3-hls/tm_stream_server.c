#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#else
#include <windows.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include "platform.h"
#include "simple-log.h"
#include "jpeg_encoder.h"
#include "http_server.h"
#include "h264_encoder.h"
#include "ts_muxer.h"
#include "hls_server.h"

// --------------------------------------------------------------------------
// Constants
// --------------------------------------------------------------------------
#define FRAME_WIDTH  1920
#define FRAME_HEIGHT 1080
#define IMG_BUF_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 4)
#define MAX_ARGS     64

typedef enum {
    MODE_HLS  = 0,
    MODE_MJPEG = 1
} stream_mode_t;

#ifdef _WIN32
static const char* display_cmd = "C:\\Program Files (x86)\\VEX\\Tournament Manager\\TM.exe";
#elif __linux__
static const char* display_cmd = "flatpak";
#else
#error Unsupported platform
#endif

// --------------------------------------------------------------------------
// Shared frame capture state
// --------------------------------------------------------------------------
struct frame_capture_state {
    uint8_t*        imgbuf;
    shm_sem_t       sem;
    pthread_mutex_t mutex;

    // MJPEG mode
    uint8_t*        current_jpeg;
    size_t          current_jpeg_size;
    int             jpeg_quality;

    // HLS mode
    h264_encoder_t* h264_enc;
    ts_muxer_t*     ts_mux;
    hls_server_t*   hls_srv;
    int             fps;
    int             segment_duration;  // seconds
    int             frame_num;         // used for PTS
};

static struct frame_capture_state* g_state   = NULL;
static plat_pid_t                  g_tm_pid  = 0;
static int                         g_running = 1;
static stream_mode_t               g_mode    = MODE_HLS;

// --------------------------------------------------------------------------
// Signal handler
// --------------------------------------------------------------------------
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// --------------------------------------------------------------------------
// Argument helpers
// --------------------------------------------------------------------------
static void append_arg(char** args, const char* arg) {
    for (int i = 0; i < MAX_ARGS; i++) {
        if (args[i] == NULL) {
            args[i] = malloc(strlen(arg) + 1);
            strcpy(args[i], arg);
            return;
        }
    }
    error("Too many arguments!");
}

static void free_args(char** args) {
    for (int i = 0; i < MAX_ARGS; i++) {
        if (args[i]) { free(args[i]); args[i] = NULL; }
    }
}

// --------------------------------------------------------------------------
// MJPEG frame callback (for http_server)
// --------------------------------------------------------------------------
static size_t get_jpeg_frame(uint8_t** jpeg_data, void* user_data) {
    struct frame_capture_state* state = (struct frame_capture_state*)user_data;
    if (!state || !state->imgbuf) return 0;

    pthread_mutex_lock(&state->mutex);

    free(state->current_jpeg);
    state->current_jpeg = NULL;

    size_t jpeg_size = encode_bgra_to_jpeg(state->imgbuf, FRAME_WIDTH, FRAME_HEIGHT,
                                           &state->current_jpeg, state->jpeg_quality);
    if (jpeg_size > 0 && state->current_jpeg) {
        *jpeg_data = malloc(jpeg_size);
        if (*jpeg_data) {
            memcpy(*jpeg_data, state->current_jpeg, jpeg_size);
        } else {
            jpeg_size = 0;
        }
    }

    pthread_mutex_unlock(&state->mutex);
    return jpeg_size;
}

// --------------------------------------------------------------------------
// Sleep helper
// --------------------------------------------------------------------------
static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

// --------------------------------------------------------------------------
// Frame capture thread
// --------------------------------------------------------------------------
static void* frame_capture_thread(void* arg) {
    struct frame_capture_state* state = (struct frame_capture_state*)arg;

    int dbg_frames = 0, dbg_nals = 0, dbg_segs = 0;

    // Persistent BGRA frame buffer owned by this thread.
    // Populated from shared memory whenever TM posts a new frame, then held
    // so we can re-encode it on subsequent loop iterations when TM is idle
    // (static content).  This ensures the encoder keeps ticking at the
    // configured FPS regardless of whether the source image is changing.
    uint8_t* last_bgra = malloc(IMG_BUF_SIZE);
    if (!last_bgra) {
        error("frame_capture_thread: failed to allocate frame buffer");
        return NULL;
    }
    int have_last  = 0;
    int in_idle    = 0;   // false = blocking on semaphore; true = injecting repeats

    // Precise frame interval in nanoseconds (no integer-division drift).
    long frame_ns = (state->fps > 0) ? (1000000000L / state->fps) : 100000000L;

#ifndef _WIN32
    struct timespec idle_tick;  // absolute MONOTONIC deadline for idle injections
#else
    int frame_interval_ms = (state->fps > 0) ? (1000 / state->fps) : 100;
#endif

    while (g_running) {
        int got_new_frame;

        if (!in_idle) {
            // ── Active mode ────────────────────────────────────────────────
            // Block on the semaphore for up to 100 ms.  TM (or test_inject)
            // running at any realistic FPS always posts within this window for
            // active content, so we stay perfectly phase-locked to the source
            // with no independent timer to drift against.
            // If nothing arrives in 100 ms the source is genuinely idle.
            got_new_frame = (shm_sem_timedwait(state->sem, 100) == 0);

            if (!got_new_frame && have_last) {
                // Source went idle — switch to fixed-rate injection
                in_idle = 1;
#ifndef _WIN32
                clock_gettime(CLOCK_MONOTONIC, &idle_tick);
#endif
            }
        } else {
            // ── Idle mode ──────────────────────────────────────────────────
            // Inject repeated frames at exactly the configured FPS using a
            // drift-free absolute timer.  Check the semaphore non-blocking on
            // every tick; if the source posts again, return to active mode.
#ifdef _WIN32
            got_new_frame = (shm_sem_timedwait(state->sem, frame_interval_ms) == 0);
            if (got_new_frame) in_idle = 0;
#else
            idle_tick.tv_nsec += frame_ns;
            if (idle_tick.tv_nsec >= 1000000000L) {
                idle_tick.tv_sec++;
                idle_tick.tv_nsec -= 1000000000L;
            }
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &idle_tick, NULL) == EINTR)
                ;

            // Drain any posts that arrived while we slept
            got_new_frame = 0;
            while (sem_trywait(state->sem) == 0)
                got_new_frame = 1;
            if (got_new_frame) in_idle = 0;
#endif
        }

        if (got_new_frame) {
            pthread_mutex_lock(&state->mutex);
            memcpy(last_bgra, state->imgbuf, IMG_BUF_SIZE);
            pthread_mutex_unlock(&state->mutex);
            have_last = 1;
        }

        if (!have_last)
            continue;

        dbg_frames++;

        if (g_mode == MODE_HLS) {
            uint8_t* nal_data    = NULL;
            int      is_keyframe = 0;
            size_t   nal_size    = h264_encoder_encode(state->h264_enc, last_bgra,
                                                        &nal_data, &is_keyframe);

            if (nal_size == 0 || !nal_data) {
                if (dbg_frames <= 15)
                    info("frame %d: encoder returned 0 bytes (buffering?)", dbg_frames);
                continue;
            }
            dbg_nals++;
            if (dbg_nals <= 5 || is_keyframe)
                info("frame %d: nal=%zu bytes keyframe=%d", dbg_frames, nal_size, is_keyframe);

            // PTS in 90 kHz units
            int64_t pts = (int64_t)state->frame_num * 90000 / state->fps;
            state->frame_num++;

            uint8_t* seg_data = NULL;
            size_t   seg_size = 0;
            ts_muxer_write_nal(state->ts_mux, nal_data, nal_size,
                               pts, is_keyframe, &seg_data, &seg_size);

            if (seg_data && seg_size > 0) {
                dbg_segs++;
                info("segment %d pushed: %zu bytes", dbg_segs, seg_size);
                double duration = (double)state->segment_duration;
                hls_server_push_segment(state->hls_srv, seg_data, seg_size, duration);
                ts_muxer_free_segment(seg_data);
            }
        }
        // MJPEG mode: HTTP server encodes on demand via get_jpeg_frame callback
    }

    free(last_bgra);

    // Flush any remaining HLS segment
    if (g_mode == MODE_HLS && state->ts_mux) {
        uint8_t* seg_data = NULL;
        size_t   seg_size = 0;
        ts_muxer_flush(state->ts_mux, &seg_data, &seg_size);
        if (seg_data && seg_size > 0) {
            hls_server_push_segment(state->hls_srv, seg_data, seg_size,
                                    (double)state->segment_duration);
            ts_muxer_free_segment(seg_data);
        }
    }

    return NULL;
}

// --------------------------------------------------------------------------
// main
// --------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    int         port             = 8080;
    int         framerate        = 10;
    int         jpeg_quality     = 85;
    int         bitrate_kbps     = 3000;
    int         segment_duration = 1;
    const char* server_addr      = NULL;
    const char* password         = NULL;
    int         kiosk_mode       = 0;
    int         onlyscreen       = 0;
    int         no_tm            = 0;
    g_mode = MODE_HLS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--framerate") == 0 && i + 1 < argc) {
            framerate = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--quality") == 0 && i + 1 < argc) {
            jpeg_quality = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--bitrate") == 0 && i + 1 < argc) {
            bitrate_kbps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--segment-duration") == 0 && i + 1 < argc) {
            segment_duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "mjpeg") == 0)      g_mode = MODE_MJPEG;
            else if (strcmp(argv[i], "hls") == 0)   g_mode = MODE_HLS;
            else { error("Unknown mode '%s' (use hls or mjpeg)", argv[i]); return 1; }
        } else if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            server_addr = argv[++i];
        } else if (strcmp(argv[i], "--pw") == 0 && i + 1 < argc) {
            password = argv[++i];
        } else if (strcmp(argv[i], "--no-tm") == 0) {
            no_tm = 1;
        } else if (strcmp(argv[i], "--kiosk") == 0) {
            kiosk_mode = 1;
        } else if (strcmp(argv[i], "--onlyscreen") == 0 && i + 1 < argc) {
            onlyscreen = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --port N              HTTP port (default: 8080)\n");
            printf("  --mode MODE           Stream mode: hls (default) or mjpeg\n");
            printf("  --framerate N         Frame rate (default: 10)\n");
            printf("  --bitrate N           H.264 bitrate in kbps, HLS mode (default: 3000)\n");
            printf("  --segment-duration N  HLS segment duration in sec (default: 1)\n");
            printf("  --quality N           JPEG quality 1-100, MJPEG mode (default: 85)\n");
            printf("  --server ADDR         TM server address\n");
            printf("  --pw PASSWORD         TM password\n");
            printf("  --kiosk               Enable kiosk mode\n");
            printf("  --onlyscreen N        Show only screen N (default: 0 = all)\n");
            printf("  --no-tm               Skip launching TM Display (use with test_inject)\n");
            printf("  --help, -h            Show this help\n");
            return 0;
        }
    }

    if (framerate <= 0) framerate = 10;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // Allocate shared state
    g_state = calloc(1, sizeof(struct frame_capture_state));
    if (!g_state) { error("Failed to allocate state"); return 1; }
    g_state->jpeg_quality     = jpeg_quality;
    g_state->fps              = framerate;
    g_state->segment_duration = segment_duration;
    pthread_mutex_init(&g_state->mutex, NULL);

    // Build TM Display arguments
    const char* shmem = "tm-remote-display";
    char* args[MAX_ARGS] = {0};

#ifdef __linux__
    append_arg(args, "flatpak");
    append_arg(args, "run");
    append_arg(args, "-p");
    append_arg(args, "com.dwabtech.TM");
    append_arg(args, "--tmdisplay");
#else
    append_arg(args, "--tmdisplay");
#endif

    append_arg(args, "--shmem");
    append_arg(args, shmem);

    char framerate_str[32];
    snprintf(framerate_str, sizeof(framerate_str), "%d", framerate);
    append_arg(args, "--framerate");
    append_arg(args, framerate_str);

    append_arg(args, "--checkversion");
    append_arg(args, "0");
    append_arg(args, "--preview");
    append_arg(args, "0");

    if (kiosk_mode) { append_arg(args, "--kiosk"); append_arg(args, "1"); }

    if (onlyscreen > 0) {
        char screen_str[32];
        snprintf(screen_str, sizeof(screen_str), "%d", onlyscreen);
        append_arg(args, "--onlyscreen");
        append_arg(args, screen_str);
    }

    append_arg(args, "--overlay");
    append_arg(args, "0");

    if (server_addr && strlen(server_addr) > 0) {
        append_arg(args, "--server");
        append_arg(args, server_addr);
    }
    if (password && strlen(password) > 0) {
        append_arg(args, "--pw");
        append_arg(args, password);
    }

    if (no_tm) {
        info("--no-tm: skipping TM Display launch (expecting test_inject or external source)");
    } else {
        info("Starting TM Display (mode: %s)...", g_mode == MODE_HLS ? "hls" : "mjpeg");
        g_tm_pid = plat_spawn(display_cmd, args);

#ifdef _WIN32
        if (g_tm_pid == NULL) {
#else
        if (g_tm_pid <= 0) {
#endif
            error("Failed to start TM Display");
            free_args(args);
            free(g_state);
            return 1;
        }
#ifndef _WIN32
        info("TM Display PID: %d", (int)g_tm_pid);
#endif
        // Wait for TM to initialise
        sleep_ms(3000);
    }

    // Open shared memory
    char fb_name[64], sem_name[64];
#ifdef _WIN32
    snprintf(fb_name,  sizeof(fb_name),  "%s-fb",  shmem);
    snprintf(sem_name, sizeof(sem_name), "%s-sem", shmem);
#else
    snprintf(fb_name,  sizeof(fb_name),  "/%s-fb",  shmem);
    snprintf(sem_name, sizeof(sem_name), "/%s-sem", shmem);
#endif

    shm_file_t fd = shm_fd_open(fb_name, IMG_BUF_SIZE);
    if (fd == SHM_FD_INVALID) {
        error("Failed to open shared memory");
        plat_kill(g_tm_pid);
        free_args(args);
        free(g_state);
        return 1;
    }

    g_state->imgbuf = shm_mmap(fd, IMG_BUF_SIZE);
    if (!g_state->imgbuf) {
        error("Failed to map shared memory");
        shm_fd_close(fd, fb_name);
        plat_kill(g_tm_pid);
        free_args(args);
        free(g_state);
        return 1;
    }

    g_state->sem = shm_sem_create(sem_name);
    if (!g_state->sem) {
        error("Failed to create semaphore");
        shm_mmap_close(g_state->imgbuf, IMG_BUF_SIZE);
        shm_fd_close(fd, fb_name);
        plat_kill(g_tm_pid);
        free_args(args);
        free(g_state);
        return 1;
    }

    // ---------- Initialise streaming subsystems ----------
    int rc = 0;

    if (g_mode == MODE_HLS) {
        int keyframe_interval = framerate * segment_duration;
        g_state->h264_enc = h264_encoder_create(FRAME_WIDTH, FRAME_HEIGHT, framerate,
                                                 bitrate_kbps, keyframe_interval);
        if (!g_state->h264_enc) {
            error("Failed to create H.264 encoder");
            rc = 1; goto cleanup_shm;
        }

        g_state->ts_mux = ts_muxer_create();
        if (!g_state->ts_mux) {
            error("Failed to create TS muxer");
            rc = 1; goto cleanup_h264;
        }

        g_state->hls_srv = hls_server_create(port);
        if (!g_state->hls_srv) {
            error("Failed to create HLS server");
            rc = 1; goto cleanup_ts;
        }

        if (hls_server_run(g_state->hls_srv) != 0) {
            error("Failed to start HLS server");
            rc = 1; goto cleanup_hls;
        }

        info("HLS streaming started — open http://localhost:%d", port);

    } else {
        // MJPEG mode
        http_server_t* http_srv = http_server_create(port, get_jpeg_frame, g_state);
        if (!http_srv) {
            error("Failed to create HTTP server");
            rc = 1; goto cleanup_shm;
        }
        if (http_server_run(http_srv) != 0) {
            error("Failed to start HTTP server");
            http_server_destroy(http_srv);
            rc = 1; goto cleanup_shm;
        }
        info("MJPEG streaming started — open http://localhost:%d", port);
        // Note: for MJPEG mode we leave http_srv alive; it's destroyed on process exit.
        // (Phase 2 pattern — no clean destroy path needed for this mode.)
    }

    // Start frame capture thread
    pthread_t capture_thread;
    pthread_create(&capture_thread, NULL, frame_capture_thread, g_state);

    // Main loop
    while (g_running) {
        sleep_ms(500);
    }

    info("Shutting down...");
    g_running = 0;
    pthread_join(capture_thread, NULL);

    // Teardown in reverse order
    if (g_mode == MODE_HLS) {
cleanup_hls:
        if (g_state->hls_srv) hls_server_destroy(g_state->hls_srv);
cleanup_ts:
        if (g_state->ts_mux)  ts_muxer_destroy(g_state->ts_mux);
cleanup_h264:
        if (g_state->h264_enc) h264_encoder_destroy(g_state->h264_enc);
    }

cleanup_shm:
    shm_sem_close(g_state->sem, sem_name);
    shm_mmap_close(g_state->imgbuf, IMG_BUF_SIZE);
    shm_fd_close(fd, fb_name);
    if (!no_tm) plat_kill(g_tm_pid);
    free_args(args);
    if (g_state->current_jpeg) free(g_state->current_jpeg);
    pthread_mutex_destroy(&g_state->mutex);
    free(g_state);

    info("Shutdown complete");
    return rc;
}
