// test_inject.c — synthetic frame injector for phase3-hls testing.
// Simulates what TM Display does: creates the shared memory and semaphore,
// then writes BGRA frames at a specified rate so tm_stream_server can run
// without a real TM installation.
//
// Usage:
//   ./test_inject [--framerate N] [--pattern colorbars|cycle|noise]
//
// Start this BEFORE tm_stream_server (with --no-tm):
//   Terminal 1: ./test_inject --framerate 10
//   Terminal 2: ./tm_stream_server --no-tm --framerate 10 --bitrate 3000

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <errno.h>

#define FRAME_WIDTH  1920
#define FRAME_HEIGHT 1080
#define IMG_BUF_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 4)

#define SHM_FB_NAME  "/tm-remote-display-fb"
#define SHM_SEM_NAME "/tm-remote-display-sem"

typedef enum {
    PATTERN_COLORBARS,
    PATTERN_CYCLE,
    PATTERN_NOISE
} pattern_t;

static volatile int g_running = 1;

static void sig_handler(int s) { (void)s; g_running = 0; }

// ---------------------------------------------------------------------------
// Frame generators
// ---------------------------------------------------------------------------

// SMPTE-style 8-bar colour bars with a moving white pip so every frame differs.
static void draw_colorbars(uint8_t* bgra, int frame_num) {
    // RGB values for the 8 standard SMPTE bars
    static const uint8_t bars[8][3] = {
        {192, 192, 192},  // 75% white
        {192, 192,   0},  // yellow
        {  0, 192, 192},  // cyan
        {  0, 192,   0},  // green
        {192,   0, 192},  // magenta
        {192,   0,   0},  // red
        {  0,   0, 192},  // blue
        {  0,   0,   0},  // black
    };
    int bar_w = FRAME_WIDTH / 8;

    for (int y = 0; y < FRAME_HEIGHT; y++) {
        for (int x = 0; x < FRAME_WIDTH; x++) {
            int bar = x / bar_w;
            if (bar > 7) bar = 7;
            uint8_t* px = bgra + (y * FRAME_WIDTH + x) * 4;
            px[0] = bars[bar][2]; // B (BGRA order)
            px[1] = bars[bar][1]; // G
            px[2] = bars[bar][0]; // R
            px[3] = 255;
        }
    }

    // Moving white vertical stripe so the encoder sees motion
    int pip_x = (frame_num * 8) % FRAME_WIDTH;
    for (int y = 0; y < FRAME_HEIGHT; y++) {
        int x = pip_x;
        if (x < FRAME_WIDTH) {
            uint8_t* px = bgra + (y * FRAME_WIDTH + x) * 4;
            px[0] = 255; px[1] = 255; px[2] = 255; px[3] = 255;
        }
    }

    // Frame counter text (simple pixelated digits via a tiny 3x5 font)
    // Rendered as a dark band at the bottom so it's readable on any bar colour
    int band_top = FRAME_HEIGHT - 40;
    for (int y = band_top; y < FRAME_HEIGHT; y++) {
        for (int x = 0; x < FRAME_WIDTH; x++) {
            uint8_t* px = bgra + (y * FRAME_WIDTH + x) * 4;
            px[0] = 0; px[1] = 0; px[2] = 0; px[3] = 255;
        }
    }
    // Print the frame number as decimal in the band (using a tiny 4x6 bitmapped font)
    static const uint8_t digit_bmp[10][6] = {
        {0x6,0x9,0x9,0x9,0x9,0x6}, // 0
        {0x2,0x6,0x2,0x2,0x2,0x7}, // 1
        {0x6,0x9,0x1,0x2,0x4,0xF}, // 2
        {0xE,0x1,0x6,0x1,0x1,0xE}, // 3
        {0x2,0x6,0xA,0xF,0x2,0x2}, // 4
        {0xF,0x8,0xE,0x1,0x1,0xE}, // 5
        {0x3,0x4,0xE,0x9,0x9,0x6}, // 6
        {0xF,0x1,0x2,0x2,0x4,0x4}, // 7
        {0x6,0x9,0x6,0x9,0x9,0x6}, // 8
        {0x6,0x9,0x9,0x7,0x1,0x6}, // 9
    };
    char label[32];
    snprintf(label, sizeof(label), "FRAME %d", frame_num);
    int cx = 10;
    for (int ci = 0; label[ci]; ci++) {
        char ch = label[ci];
        int digit = (ch >= '0' && ch <= '9') ? ch - '0' : -1;
        for (int row = 0; row < 6 && digit >= 0; row++) {
            for (int col = 0; col < 4; col++) {
                if (digit_bmp[digit][row] & (0x8 >> col)) {
                    int px_x = cx + col * 3;
                    int px_y = band_top + 6 + row * 4;
                    if (px_x < FRAME_WIDTH && px_y < FRAME_HEIGHT) {
                        uint8_t* px = bgra + (px_y * FRAME_WIDTH + px_x) * 4;
                        px[0] = 255; px[1] = 255; px[2] = 255; px[3] = 255;
                    }
                }
            }
        }
        cx += (digit >= 0) ? 16 : 12;
    }
}

// Solid colour that slowly cycles through hues — good for checking H.264 colour fidelity.
// Changes by 1 degree per frame, full cycle every 360 frames (~36 s at 10 fps).
static void draw_cycle(uint8_t* bgra, int frame_num) {
    double hue = fmod((double)frame_num, 360.0);
    double h6  = hue / 60.0;
    int    i   = (int)h6 % 6;
    double f   = h6 - (int)h6;

    uint8_t r = 0, g = 0, b = 0;
    uint8_t p = 0;
    uint8_t q_v = (uint8_t)((1.0 - f) * 255.0);
    uint8_t t_v = (uint8_t)(f * 255.0);

    switch (i) {
        case 0: r = 255;  g = t_v;  b = p;   break;
        case 1: r = q_v;  g = 255;  b = p;   break;
        case 2: r = p;    g = 255;  b = t_v; break;
        case 3: r = p;    g = q_v;  b = 255; break;
        case 4: r = t_v;  g = p;    b = 255; break;
        default:r = 255;  g = p;    b = q_v; break;
    }

    uint8_t pixel[4] = {b, g, r, 255};
    for (int i2 = 0; i2 < FRAME_WIDTH * FRAME_HEIGHT; i2++) {
        memcpy(bgra + i2 * 4, pixel, 4);
    }

    // Draw the hue angle as a number in the top-left corner (white text on solid bg)
    // Just fill a small rectangle as a luminance reference
    int box_w = (int)(hue / 360.0 * FRAME_WIDTH);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < box_w; x++) {
            uint8_t* px = bgra + (y * FRAME_WIDTH + x) * 4;
            px[0] = 255; px[1] = 255; px[2] = 255; px[3] = 255;
        }
    }
}

// Random noise — useful for confirming the encoder handles worst-case frames.
// Note: noise is nearly incompressible; bitrate will spike above target.
static void draw_noise(uint8_t* bgra, int frame_num) {
    uint32_t seed = (uint32_t)frame_num * 2654435761u;
    for (int i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; i++) {
        // xorshift32
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        bgra[i * 4 + 0] = (uint8_t)(seed & 0xFF);
        bgra[i * 4 + 1] = (uint8_t)((seed >> 8) & 0xFF);
        bgra[i * 4 + 2] = (uint8_t)((seed >> 16) & 0xFF);
        bgra[i * 4 + 3] = 255;
    }
}

// ---------------------------------------------------------------------------
// Timing helper
// ---------------------------------------------------------------------------
static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleep_ns(int64_t ns) {
    if (ns <= 0) return;
    struct timespec ts = {
        .tv_sec  = (time_t)(ns / 1000000000LL),
        .tv_nsec = (long)(ns % 1000000000LL)
    };
    nanosleep(&ts, NULL);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    int       framerate = 10;
    pattern_t pattern   = PATTERN_COLORBARS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--framerate") == 0 && i + 1 < argc) {
            framerate = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "cycle") == 0)        pattern = PATTERN_CYCLE;
            else if (strcmp(argv[i], "noise") == 0)   pattern = PATTERN_NOISE;
            else                                       pattern = PATTERN_COLORBARS;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--framerate N] [--pattern colorbars|cycle|noise]\n", argv[0]);
            printf("Start this before tm_stream_server --no-tm.\n");
            return 0;
        }
    }
    if (framerate <= 0) framerate = 10;

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("[inject] pattern=%s framerate=%d fps\n",
           pattern == PATTERN_CYCLE ? "cycle" :
           pattern == PATTERN_NOISE ? "noise" : "colorbars",
           framerate);

    // -----------------------------------------------------------------------
    // Create shared memory
    // -----------------------------------------------------------------------
    // Clean up any stale entries from a previous run
    shm_unlink(SHM_FB_NAME);
    sem_unlink(SHM_SEM_NAME);

    int shm_fd = shm_open(SHM_FB_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (shm_fd < 0) {
        fprintf(stderr, "[inject] shm_open failed: %s\n", strerror(errno));
        return 1;
    }
    if (ftruncate(shm_fd, IMG_BUF_SIZE) < 0) {
        fprintf(stderr, "[inject] ftruncate failed: %s\n", strerror(errno));
        close(shm_fd);
        shm_unlink(SHM_FB_NAME);
        return 1;
    }

    uint8_t* fbuf = mmap(NULL, IMG_BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (fbuf == MAP_FAILED) {
        fprintf(stderr, "[inject] mmap failed: %s\n", strerror(errno));
        close(shm_fd);
        shm_unlink(SHM_FB_NAME);
        return 1;
    }
    close(shm_fd); // fd no longer needed once mapped

    // -----------------------------------------------------------------------
    // Create semaphore (initial value 0 — same as platform-posix.c)
    // -----------------------------------------------------------------------
    sem_t* sem = sem_open(SHM_SEM_NAME, O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 0);
    if (sem == SEM_FAILED) {
        fprintf(stderr, "[inject] sem_open failed: %s\n", strerror(errno));
        munmap(fbuf, IMG_BUF_SIZE);
        shm_unlink(SHM_FB_NAME);
        return 1;
    }

    printf("[inject] Shared memory ready: %s  semaphore: %s\n", SHM_FB_NAME, SHM_SEM_NAME);
    printf("[inject] Now start:  ./tm_stream_server --no-tm --framerate %d --bitrate 3000\n", framerate);
    printf("[inject] Press Ctrl+C to stop.\n\n");

    // -----------------------------------------------------------------------
    // Frame loop
    // -----------------------------------------------------------------------
    int64_t frame_interval_ns = 1000000000LL / framerate;
    int64_t next_frame_ns     = now_ns();
    int     frame_num         = 0;
    int64_t report_ns         = now_ns() + 5000000000LL; // first report in 5 s

    while (g_running) {
        // Generate frame into shared memory
        switch (pattern) {
            case PATTERN_COLORBARS: draw_colorbars(fbuf, frame_num); break;
            case PATTERN_CYCLE:     draw_cycle(fbuf, frame_num);     break;
            case PATTERN_NOISE:     draw_noise(fbuf, frame_num);     break;
        }

        // Signal the server that a new frame is ready
        sem_post(sem);
        frame_num++;

        // Periodic status
        int64_t now = now_ns();
        if (now >= report_ns) {
            printf("[inject] frame %d\n", frame_num);
            report_ns = now + 5000000000LL;
        }

        // Sleep until next frame deadline
        next_frame_ns += frame_interval_ns;
        sleep_ns(next_frame_ns - now_ns());
    }

    printf("\n[inject] Stopping after %d frames.\n", frame_num);

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------
    sem_close(sem);
    sem_unlink(SHM_SEM_NAME);
    munmap(fbuf, IMG_BUF_SIZE);
    shm_unlink(SHM_FB_NAME);

    printf("[inject] Done.\n");
    return 0;
}
