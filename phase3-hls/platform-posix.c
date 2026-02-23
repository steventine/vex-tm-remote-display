#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "simple-log.h"
#include "platform.h"

shm_file_t shm_fd_open(char* name, size_t len) {
    (void)len; // Parameter used for Windows compatibility
    shm_file_t fd = shm_open(name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        warn("shm_open failed: %d", errno);
        return SHM_FD_INVALID;
    }

    return fd;
}

void shm_fd_close(shm_file_t fd, char* name) {
    close(fd);
    shm_unlink(name);
}

uint8_t* shm_mmap(shm_file_t fd, size_t len) {
    // Set size of shared memory file
    if (ftruncate(fd, len) == -1) {
        warn("ftruncate failed: %d", errno);
        return NULL;
    }

    // Obtain a pointer to memory-mapped file data
    uint8_t* imgbuf = (uint8_t*) mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(imgbuf == MAP_FAILED) {
        warn("mmap failed: %d", errno);
        return NULL;
    }

    return imgbuf;
}

void shm_mmap_close(uint8_t* mmap, size_t len) {
    munmap(mmap, len);
}

shm_sem_t shm_sem_create(char* name) {
    shm_sem_t sem = sem_open(name, O_CREAT, S_IRUSR | S_IWUSR, 0);
    if(sem == SEM_FAILED) {
        warn("sem_open failed: %d", errno);
        return NULL;
    }

    return sem;
}

int shm_sem_wait(shm_sem_t sem) {
    int ret;
    for(int i = 0; i < 20; i++) {
        ret = sem_trywait(sem);
        if (ret == 0) {
            break;
        }

        // Semaphore not ready, sleep for 5ms
        nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 5 * 1000000}, NULL);
    }
    if(ret == 0) {
        return 0;
    }
    return 1;
}

void shm_sem_close(shm_sem_t sem, char* name) {
    sem_close(sem);
    sem_unlink(name);
}

plat_pid_t plat_spawn(const char* file, char* const args[]) {
    pid_t pid;

    // Check if executable exists in PATH first
    // Try to find it using access() with PATH
    int found = 0;
    char* path_env = getenv("PATH");
    
    if(path_env != NULL) {
        char* path_copy = strdup(path_env);
        char* token = strtok(path_copy, ":");
        char test_path[1024];
        
        while(token != NULL) {
            snprintf(test_path, sizeof(test_path), "%s/%s", token, file);
            if(access(test_path, X_OK) == 0) {
                found = 1;
                break;
            }
            token = strtok(NULL, ":");
        }
        free(path_copy);
    }
    
    // Also check if it's an absolute path or in current directory
    if(!found) {
        if(file[0] == '/' && access(file, X_OK) == 0) {
            found = 1;
        } else if(access(file, X_OK) == 0) {
            found = 1;
        }
    }
    
    if(!found) {
        warn("Executable '%s' not found in PATH", file);
        char* path = getenv("PATH");
        if(path) {
            debug("Current PATH: %s", path);
        } else {
            warn("PATH environment variable not set");
        }
        return -1;
    }

    pid = fork();
    if(pid == 0) {
        // Child process
        // Debug: print what we're about to execute
        fprintf(stderr, "[DEBUG] Child process executing: %s", file);
        for(int i = 0; args[i] != NULL; i++) {
            fprintf(stderr, " %s", args[i]);
        }
        fprintf(stderr, "\n");
        fflush(stderr);
        
        execvp(file, args);
        // If execvp fails, print error and exit
        fprintf(stderr, "[ERROR] execvp('%s') failed: %s\n", file, strerror(errno));
        fprintf(stderr, "[ERROR] Attempted to run: %s", file);
        for(int i = 0; args[i] != NULL; i++) {
            fprintf(stderr, " %s", args[i]);
        }
        fprintf(stderr, "\n");
        _exit(1);
    } else if(pid < 0) {
        // Fork failed
        warn("fork failed: %d", errno);
        return -1;
    }
    
    // Wait a brief moment to see if child exited immediately (execvp failure)
    nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 100 * 1000000}, NULL); // 100ms
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if(result == pid) {
        // Child already exited (execvp probably failed)
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        warn("Process exited immediately (exit code: %d) - execvp likely failed", exit_code);
        return -1;
    } else if(result < 0 && errno != ECHILD) {
        warn("waitpid failed: %d", errno);
    }

    return pid;
}

void plat_kill(plat_pid_t pid) {
    kill(pid, SIGTERM);
}

