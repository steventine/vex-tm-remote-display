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

#define FRAME_WIDTH 1920
#define FRAME_HEIGHT 1080
#define IMG_BUF_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 4)
#define MAX_ARGS 64

#ifdef _WIN32
const char* display_cmd = "C:\\Program Files (x86)\\VEX\\Tournament Manager\\TM.exe";
#elif __linux__
const char* display_cmd = "flatpak";
#else
#error Unsupported platform
#endif

// Global state for frame capture
struct frame_capture_state {
    uint8_t* imgbuf;
    shm_sem_t sem;
    pthread_mutex_t mutex;
    uint8_t* current_jpeg;
    size_t current_jpeg_size;
    int jpeg_quality;
};

static struct frame_capture_state* g_state = NULL;
static plat_pid_t g_tm_pid = 0;
static int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    info("Shutting down...");
}

static void append_arg(char** args, const char* arg) {
    for(int i = 0; i < MAX_ARGS; i++) {
        if(args[i] == NULL) {
            args[i] = malloc(strlen(arg) + 1);
            strcpy(args[i], arg);
            return;
        }
    }
    error("Too many arguments!");
}

static void free_args(char** args) {
    for(int i = 0; i < MAX_ARGS; i++) {
        if(args[i] != NULL) {
            free(args[i]);
            args[i] = NULL;
        }
    }
}

// Frame callback for HTTP server
static size_t get_jpeg_frame(uint8_t** jpeg_data, void* user_data) {
    struct frame_capture_state* state = (struct frame_capture_state*)user_data;
    if(state == NULL || state->imgbuf == NULL) return 0;
    
    pthread_mutex_lock(&state->mutex);
    
    // Encode current frame to JPEG
    if(state->current_jpeg) {
        free(state->current_jpeg);
        state->current_jpeg = NULL;
    }
    
    size_t jpeg_size = encode_bgra_to_jpeg(state->imgbuf, FRAME_WIDTH, FRAME_HEIGHT,
                                          &state->current_jpeg, state->jpeg_quality);
    
    if(jpeg_size > 0 && state->current_jpeg != NULL) {
        *jpeg_data = malloc(jpeg_size);
        if(*jpeg_data) {
            memcpy(*jpeg_data, state->current_jpeg, jpeg_size);
        } else {
            jpeg_size = 0;
        }
    }
    
    pthread_mutex_unlock(&state->mutex);
    return jpeg_size;
}

// Frame capture thread
static void* frame_capture_thread(void* arg) {
    struct frame_capture_state* state = (struct frame_capture_state*)arg;
    
    while(g_running) {
        int wait = shm_sem_wait(state->sem);
        if(wait == 0) {
            // New frame available - it's already in imgbuf
            // The HTTP server will encode it when needed
        } else {
            // No frame, wait a bit
#ifdef _WIN32
            Sleep(50); // 50ms
#else
            nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 50 * 1000000}, NULL); // 50ms
#endif
        }
    }
    
    return NULL;
}

int main(int argc, char* argv[]) {
    int port = 8080;
    int framerate = 10;
    int jpeg_quality = 85;
    const char* server = NULL;
    const char* password = NULL;
    int kiosk_mode = 0;
    int onlyscreen = 0;  // 0 means show all screens
    
    // Parse command line arguments
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        } else if(strcmp(argv[i], "--framerate") == 0 && i + 1 < argc) {
            framerate = atoi(argv[i + 1]);
            i++;
        } else if(strcmp(argv[i], "--quality") == 0 && i + 1 < argc) {
            jpeg_quality = atoi(argv[i + 1]);
            i++;
        } else if(strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            server = argv[i + 1];
            i++;
        } else if(strcmp(argv[i], "--pw") == 0 && i + 1 < argc) {
            password = argv[i + 1];
            i++;
        } else if(strcmp(argv[i], "--kiosk") == 0) {
            kiosk_mode = 1;
        } else if(strcmp(argv[i], "--onlyscreen") == 0 && i + 1 < argc) {
            onlyscreen = atoi(argv[i + 1]);
            i++;
        } else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --port N         HTTP server port (default: 8080)\n");
            printf("  --framerate N   Frame rate for TM Display (default: 10)\n");
            printf("  --quality N     JPEG quality 1-100 (default: 85)\n");
            printf("  --server ADDR   Tournament Manager server address\n");
            printf("  --pw PASSWORD   Tournament Manager password\n");
            printf("  --kiosk         Enable kiosk mode\n");
            printf("  --onlyscreen N  Show only screen N (0 = all screens, default: 0)\n");
            printf("  --help, -h      Show this help message\n");
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Allocate state
    g_state = calloc(1, sizeof(struct frame_capture_state));
    if(g_state == NULL) {
        error("Failed to allocate state");
        return 1;
    }
    g_state->jpeg_quality = jpeg_quality;
    pthread_mutex_init(&g_state->mutex, NULL);
    
    // Use fixed shared memory name
    const char* shmem = "tm-remote-display";
    info("Using shared memory name: %s", shmem);
    
    // Build TM Display command arguments (similar to phase1)
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
    
    if(kiosk_mode) {
        append_arg(args, "--kiosk");
        append_arg(args, "1");
    }
    
    if(onlyscreen > 0) {
        append_arg(args, "--onlyscreen");
        char screen_str[32];
        snprintf(screen_str, sizeof(screen_str), "%d", onlyscreen);
        append_arg(args, screen_str);
    }
    
    append_arg(args, "--overlay");
    append_arg(args, "0");

    if(server != NULL && strlen(server) > 0) {
        append_arg(args, "--server");
        append_arg(args, server);
    }

    if(password != NULL && strlen(password) > 0) {
        append_arg(args, "--pw");
        append_arg(args, password);
    }
    
    // Start TM Display
    info("Starting TM Display...");
    g_tm_pid = plat_spawn(display_cmd, args);
    
#ifdef _WIN32
    if(g_tm_pid == NULL) {
        error("Failed to start TM Display");
        free_args(args);
        return 1;
    }
#else
    if(g_tm_pid <= 0) {
        error("Failed to start TM Display");
        free_args(args);
        return 1;
    }
    info("Started TM Display with PID: %d", (int)g_tm_pid);
#endif
    
    // Wait for TM Display to initialize
#ifdef _WIN32
    Sleep(3000); // 3 seconds
#else
    sleep(3);
#endif
    
    // Open shared memory
    char fb_name[32];
#ifdef _WIN32
    snprintf(fb_name, sizeof(fb_name), "%s-fb", shmem);
#else
    snprintf(fb_name, sizeof(fb_name), "/%s-fb", shmem);
#endif
    
    shm_file_t fd = shm_fd_open(fb_name, IMG_BUF_SIZE);
    if(fd == SHM_FD_INVALID) {
        error("Failed to open shared memory");
        plat_kill(g_tm_pid);
        free_args(args);
        return 1;
    }
    
    g_state->imgbuf = shm_mmap(fd, IMG_BUF_SIZE);
    if(g_state->imgbuf == NULL) {
        error("Failed to map shared memory");
        shm_fd_close(fd, fb_name);
        plat_kill(g_tm_pid);
        free_args(args);
        return 1;
    }
    
    // Open semaphore
    char sem_name[32];
#ifdef _WIN32
    snprintf(sem_name, sizeof(sem_name), "%s-sem", shmem);
#else
    snprintf(sem_name, sizeof(sem_name), "/%s-sem", shmem);
#endif
    
    g_state->sem = shm_sem_create(sem_name);
    if(g_state->sem == NULL) {
        error("Failed to create semaphore");
        shm_mmap_close(g_state->imgbuf, IMG_BUF_SIZE);
        shm_fd_close(fd, fb_name);
        plat_kill(g_tm_pid);
        free_args(args);
        return 1;
    }
    
    // Start frame capture thread
    pthread_t capture_thread;
    pthread_create(&capture_thread, NULL, frame_capture_thread, g_state);
    
    // Create and start HTTP server
    http_server_t* http_server = http_server_create(port, get_jpeg_frame, g_state);
    if(http_server == NULL) {
        error("Failed to create HTTP server");
        g_running = 0;
        pthread_join(capture_thread, NULL);
        shm_sem_close(g_state->sem, sem_name);
        shm_mmap_close(g_state->imgbuf, IMG_BUF_SIZE);
        shm_fd_close(fd, fb_name);
        plat_kill(g_tm_pid);
        free_args(args);
        return 1;
    }
    
    info("Starting HTTP server on port %d", port);
    info("Open http://localhost:%d in your web browser", port);
    
    http_server_run(http_server);
    
    // Main loop - wait for shutdown signal
    while(g_running) {
#ifdef _WIN32
        Sleep(1000); // 1 second
#else
        sleep(1);
#endif
    }
    
    // Cleanup
    info("Shutting down...");
    http_server_destroy(http_server);
    g_running = 0;
    pthread_join(capture_thread, NULL);
    
    if(g_state->current_jpeg) {
        free(g_state->current_jpeg);
    }
    shm_sem_close(g_state->sem, sem_name);
    shm_mmap_close(g_state->imgbuf, IMG_BUF_SIZE);
    shm_fd_close(fd, fb_name);
    plat_kill(g_tm_pid);
    free_args(args);
    free(g_state);
    
    info("Shutdown complete");
    return 0;
}

