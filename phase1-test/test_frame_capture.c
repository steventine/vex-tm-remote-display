#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "platform.h"
#include "simple-log.h"

// stb_image_write.h should be downloaded and placed in this directory
// Download from: https://github.com/nothings/stb/blob/master/stb_image_write.h
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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

// Convert BGRA (from shared memory) to RGBA (for PNG)
static void convert_bgra_to_rgba(const uint8_t* bgra, uint8_t* rgba, int width, int height) {
    for(int i = 0; i < width * height; i++) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2]; // R = B
        rgba[i * 4 + 1] = bgra[i * 4 + 1]; // G = G
        rgba[i * 4 + 2] = bgra[i * 4 + 0]; // B = R
        rgba[i * 4 + 3] = bgra[i * 4 + 3]; // A = A
    }
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

int main(int argc, char* argv[]) {
    int num_frames = 20;
    int framerate = 10;
    const char* server = NULL;
    const char* password = NULL;
    int kiosk_mode = 0;  // Default to disabled
    
    // Parse command line arguments
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            num_frames = atoi(argv[i + 1]);
            i++;
        } else if(strcmp(argv[i], "--framerate") == 0 && i + 1 < argc) {
            framerate = atoi(argv[i + 1]);
            i++;
        } else if(strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            server = argv[i + 1];
            i++;
        } else if(strcmp(argv[i], "--pw") == 0 && i + 1 < argc) {
            password = argv[i + 1];
            i++;
        } else if(strcmp(argv[i], "--kiosk") == 0) {
            kiosk_mode = 1;
        } else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --frames N       Number of frames to capture (default: 20)\n");
            printf("  --framerate N    Frame rate for TM Display (default: 10)\n");
            printf("  --server ADDR    Tournament Manager server address\n");
            printf("  --pw PASSWORD    Tournament Manager password\n");
            printf("  --kiosk          Enable kiosk mode (fullscreen, no window decorations)\n");
            printf("  --help, -h       Show this help message\n");
            return 0;
        }
    }

    // 1. Generate unique shared memory name
    char shmem[10];
    srand((unsigned int)time(NULL));
    for(int i = 0; i < 8; i++) {
        shmem[i] = (char)((rand() % 26) + 'a');
    }
    shmem[8] = '\0';
    info("Using shared memory name: %s", shmem);

    // 2. Build TM Display command arguments
    char* args[MAX_ARGS] = {0};
    
#ifdef __linux__
    // Linux uses flatpak
    append_arg(args, "flatpak");
    append_arg(args, "run");
    append_arg(args, "-p");
    append_arg(args, "com.dwabtech.TM");
    append_arg(args, "--tmdisplay");
#else
    // Windows just needs the command
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

    //Display the Rankings screen
    append_arg(args, "--onlyscreen");
    append_arg(args, "5");
    
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

    // 3. Spawn TM Display process
    info("Starting TM Display...");
    info("Command: %s", display_cmd);
    info("Arguments:");
    for(int i = 0; args[i] != NULL; i++) {
        info("  [%d] %s", i, args[i]);
    }
    
    // Print the full command line for debugging
    info("Full command line that will be executed:");
    printf("[DEBUG] Full command: ");
    printf("%s", display_cmd);
    for(int i = 0; args[i] != NULL; i++) {
        printf(" %s", args[i]);
    }
    printf("\n");
    fflush(stdout);
    
    // Also show what the equivalent manual command would be
    info("Equivalent manual command (for testing):");
    printf("[DEBUG] Manual command: ");
#ifdef __linux__
    printf("flatpak run -p com.dwabtech.TM");
#else
    printf("\"C:\\Program Files (x86)\\VEX\\Tournament Manager\\TM.exe\"");
#endif
    // Skip the first few args (flatpak run -p com.dwabtech.TM) and show TM Display args
    int start_idx = 0;
#ifdef __linux__
    // Skip: flatpak, run, -p, com.dwabtech.TM
    start_idx = 4;
#endif
    for(int i = start_idx; args[i] != NULL; i++) {
        printf(" %s", args[i]);
    }
    printf("\n");
    fflush(stdout);
    
#ifdef __linux__
    // Check if flatpak is available before trying to use it
    info("Checking if flatpak is available...");
    char* path = getenv("PATH");
    if(path) {
        debug("Program PATH: %s", path);
    }
    
    // Try a simple test to see if flatpak works
    pid_t test_pid = fork();
    if(test_pid == 0) {
        // Child: try to run flatpak --version
        execlp("flatpak", "flatpak", "--version", NULL);
        _exit(1);
    } else if(test_pid > 0) {
        int status;
        waitpid(test_pid, &status, 0);
        if(!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            error("flatpak not found or not working");
            error("Please install flatpak:");
            error("  sudo apt install flatpak  # Debian/Ubuntu");
            error("  sudo dnf install flatpak  # Fedora");
            error("  sudo pacman -S flatpak    # Arch");
            error("");
            error("After installing, you may need to restart your terminal/IDE");
            free_args(args);
            return 1;
        } else {
            info("flatpak is available");
        }
    }
    
    // Check if Tournament Manager is installed
    info("Checking if Tournament Manager is installed...");
    pid_t check_tm_pid = fork();
    if(check_tm_pid == 0) {
        // Child: try to run flatpak list to check for TM
        // Redirect stderr to /dev/null to suppress "not installed" messages
        int fd = open("/dev/null", O_WRONLY);
        if(fd >= 0) {
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        execlp("flatpak", "flatpak", "run", "-p", "com.dwabtech.TM", "--help", NULL);
        _exit(1);
    } else if(check_tm_pid > 0) {
        int status;
        waitpid(check_tm_pid, &status, 0);
        if(!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            error("Tournament Manager (com.dwabtech.TM) is not installed");
            error("");
            error("To install Tournament Manager:");
            error("  1. Add the Flathub repository (if not already added):");
            error("     flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo");
            error("");
            error("  2. Install the required runtime:");
            error("     flatpak install --user flathub org.freedesktop.Platform/x86_64/23.08");
            error("");
            error("  3. Install Tournament Manager from flatpak file:");
            error("     flatpak install --user /path/to/VEXTournamentManager.flatpak");
            error("");
            error("  4. See the main README.md for detailed installation instructions.");
            error("");
            error("After installing, run this program again.");
            free_args(args);
            return 1;
        } else {
            info("Tournament Manager is installed");
        }
    }
#endif
    
    plat_pid_t pid = plat_spawn(display_cmd, args);
#ifdef _WIN32
    if(pid == NULL) {
        error("Failed to start TM Display process");
        free_args(args);
        return 1;
    }
    info("Started TM Display process");
#else
    if(pid <= 0) {
        error("Failed to start TM Display process");
        free_args(args);
        return 1;
    }
    info("Started TM Display with PID: %d", (int)pid);
#endif
    
    // Give TM Display a moment to initialize
    info("Waiting for TM Display to initialize...");
    sleep(3);

    // Check if process is still running
#ifdef _WIN32
    DWORD exit_code;
    if (GetExitCodeProcess(pid, &exit_code) && exit_code != STILL_ACTIVE) {
        error("TM Display process exited early with code: %ld", exit_code);
        error("This usually means TM Display failed to start. Check:");
        error("  1. Is Tournament Manager installed?");
        error("  2. Are the command-line arguments correct?");
        error("  3. Check Windows Event Viewer for TM Display errors");
        free_args(args);
        return 1;
    }
#else
    // Check if process is still running (kill with 0 signal just checks)
    int kill_result = kill(pid, 0);
    if (kill_result != 0) {
        if (errno == ESRCH) {
            error("TM Display process is not running (may have failed to start)");
            error("This usually means TM Display failed to start. Check:");
            error("  1. Is flatpak installed? (run: flatpak --version)");
            error("  2. Is Tournament Manager installed? (run: flatpak list | grep dwabtech)");
            error("  3. Try running the flatpak command manually to see error messages");
            error("  4. Check if TM Display supports --shmem argument (may need newer version)");
            info("To test manually, try:");
            info("  flatpak run -p com.dwabtech.TM --tmdisplay --shmem test123");
            free_args(args);
            return 1;
        } else {
            warn("Could not check TM Display process status: %d", errno);
        }
    } else {
        info("TM Display process is running (PID: %d)", (int)pid);
    }
#endif

    // 4. Open shared memory (with retry since TM Display might create it)
    char fb_name[32];
#ifdef _WIN32
    snprintf(fb_name, sizeof(fb_name), "%s-fb", shmem);
#else
    snprintf(fb_name, sizeof(fb_name), "/%s-fb", shmem);
#endif
    info("Opening shared memory: %s", fb_name);
    
    // Try opening shared memory with retries (TM Display might need time to create it)
    shm_file_t fd = SHM_FD_INVALID;
    for(int retry = 0; retry < 10; retry++) {
        fd = shm_fd_open(fb_name, IMG_BUF_SIZE);
        if(fd != SHM_FD_INVALID) {
            break;
        }
        if(retry < 9) {
            info("Shared memory not ready, retrying in 500ms... (attempt %d/10)", retry + 1);
            nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 500 * 1000000}, NULL);
        }
    }
    
    if(fd == SHM_FD_INVALID) {
        error("Failed to open shared memory after retries");
        error("Make sure TM Display is installed and the --shmem argument is supported");
        plat_kill(pid);
        free_args(args);
        return 1;
    }

    uint8_t* imgbuf = shm_mmap(fd, IMG_BUF_SIZE);
    if(imgbuf == NULL) {
        error("Failed to map shared memory");
        shm_fd_close(fd, fb_name);
        plat_kill(pid);
        free_args(args);
        return 1;
    }
    info("Mapped shared memory buffer");

    // 5. Open semaphore
    char sem_name[32];
#ifdef _WIN32
    snprintf(sem_name, sizeof(sem_name), "%s-sem", shmem);
#else
    snprintf(sem_name, sizeof(sem_name), "/%s-sem", shmem);
#endif
    info("Opening semaphore: %s", sem_name);
    shm_sem_t sem = shm_sem_create(sem_name);
    if(sem == NULL) {
        error("Failed to create semaphore");
        shm_mmap_close(imgbuf, IMG_BUF_SIZE);
        shm_fd_close(fd, fb_name);
        plat_kill(pid);
        free_args(args);
        return 1;
    }

    // Allocate buffer for RGBA conversion
    uint8_t* rgba_buf = (uint8_t*)malloc(IMG_BUF_SIZE);
    if(rgba_buf == NULL) {
        error("Failed to allocate RGBA buffer");
        shm_sem_close(sem, sem_name);
        shm_mmap_close(imgbuf, IMG_BUF_SIZE);
        shm_fd_close(fd, fb_name);
        plat_kill(pid);
        free_args(args);
        return 1;
    }

    // 6. Read frames and save them
    info("Capturing %d frames...", num_frames);
    info("Waiting for frames from TM Display...");
    info("Note: TM Display must be running and rendering content to generate frames.");
    info("If no frames appear, ensure TM Display is connected to a Tournament Manager server.");
    int frame_count = 0;
    int timeout_count = 0;
    const int max_timeout = 200; // Wait up to 10 seconds (200 * 50ms)
    
    while(frame_count < num_frames) {
        int wait = shm_sem_wait(sem);
        if(wait == 0) {
            // New frame available!
            timeout_count = 0;
            info("Received frame %d", frame_count + 1);
            
            // Convert BGRA to RGBA
            convert_bgra_to_rgba(imgbuf, rgba_buf, FRAME_WIDTH, FRAME_HEIGHT);
            
            // Save as PNG
            char filename[64];
            snprintf(filename, sizeof(filename), "frame_%03d.png", frame_count);
            
            int result = stbi_write_png(filename, FRAME_WIDTH, FRAME_HEIGHT, 4, 
                                       rgba_buf, FRAME_WIDTH * 4);
            if(result != 0) {
                info("Saved frame %d: %s", frame_count, filename);
                frame_count++;
            } else {
                warn("Failed to save frame %d", frame_count);
            }
        } else {
            timeout_count++;
            if(timeout_count >= max_timeout) {
                warn("Timeout waiting for frames. Captured %d/%d frames.", frame_count, num_frames);
            warn("Possible causes:");
            warn("  1. TM Display is not connected to a Tournament Manager server");
            warn("  2. TM Display has no content to display (no active matches/screens)");
            warn("  3. TM Display may not support --shmem argument (check TM version)");
            warn("Try running with --server and --pw options to connect to a TM server.");
                break;
            }
            nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 50 * 1000000}, NULL); // Sleep 50ms
        }
    }

    info("Captured %d frames", frame_count);

    // 7. Cleanup
    free(rgba_buf);
    shm_sem_close(sem, sem_name);
    shm_mmap_close(imgbuf, IMG_BUF_SIZE);
    shm_fd_close(fd, fb_name);
    
    info("Stopping TM Display...");
    plat_kill(pid);
    
    free_args(args);
    
    info("Test complete! Check frame_*.png files");
    return 0;
}

