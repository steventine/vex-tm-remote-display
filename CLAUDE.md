# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This project enables remote display of VEX Tournament Manager (TM) on any web browser over a LAN. A server process starts TM's headless display mode, reads frames via shared memory, and streams them as MJPEG over HTTP. Remote clients just open a browser URL — no plugins required.

## Build Commands

### Phase 1 (frame capture test)

```bash
cd phase1-test

# First time: download stb_image_write.h then build
./download-stb.sh && make

# Or use the combined target that checks deps first
make build
```

### Phase 2 (MJPEG streaming server)

```bash
cd phase2-server

# Linux: check deps then build
make build
# or if deps already installed:
make

# Cross-compile Windows .exe from Linux
make CROSS_COMPILE_WINDOWS=1

# Cross-compile + build Windows NSIS installer
make CROSS_COMPILE_WINDOWS=1 installer

# Check JPEG library availability
make check-deps
```

**Linux dependencies:**
```bash
sudo apt install build-essential libjpeg-turbo8-dev  # libjpeg-turbo preferred
# fallback: sudo apt install libjpeg-dev
```

**Cross-compilation dependency:**
```bash
sudo apt install gcc-mingw-w64-x86-64
```

### Running the server

```bash
cd phase2-server
./tm_stream_server [--port 8080] [--framerate 10] [--quality 85] \
                   [--server ADDR] [--pw PASSWORD] [--kiosk] [--onlyscreen N]
# Then browse to http://localhost:8080
```

## Architecture

### Data Flow

```
TM.exe / flatpak TM  →  shared memory (BGRA 1920×1080)  →  frame_capture_thread
                                                                      ↓
Browser  ←  MJPEG multipart HTTP  ←  http_server  ←  get_jpeg_frame callback
                                                         (BGRA → JPEG via jpeg_encoder)
```

### Key Components

| File | Purpose |
|------|---------|
| `tm_stream_server.c` | Main entry point; spawns TM display, manages shared memory/semaphore lifecycle, wires capture thread to HTTP server |
| `http_server.[ch]` | Minimal HTTP server that serves MJPEG multipart stream; calls `frame_callback_t` per frame |
| `jpeg_encoder.[ch]` | BGRA→JPEG wrapper; uses libjpeg-turbo when available (`HAVE_LIBJPEG_TURBO`), falls back to standard libjpeg |
| `platform.h` | Cross-platform abstraction for shared memory, semaphores, process spawning/kill |
| `platform-posix.[ch]` | POSIX implementation (Linux/macOS) — uses `shm_open`, POSIX semaphores, `fork`/`execv` |
| `platform-windows.[ch]` | Windows implementation — uses `CreateFileMapping`, Win32 semaphores, `CreateProcess` |
| `simple-log.h` | Minimal logging macros (`info()`, `error()`) |

### Platform Abstraction (`platform.h`)

The abstraction wraps: `shm_fd_open`, `shm_fd_close`, `shm_mmap`, `shm_mmap_close`, `shm_sem_create`, `shm_sem_wait`, `shm_sem_close`, `plat_spawn`, `plat_kill`. Types `shm_file_t`, `shm_sem_t`, `plat_pid_t` differ per platform (defined in the platform-specific headers).

### Shared Memory Convention

- Shared memory name: `tm-remote-display` (hardcoded)
- POSIX paths: `/tm-remote-display-fb` (framebuffer) and `/tm-remote-display-sem` (semaphore)
- Windows names: `tm-remote-display-fb` / `tm-remote-display-sem` (no leading slash)
- Frame format: BGRA, 1920×1080, 4 bytes/pixel (IMG_BUF_SIZE = 1920 × 1080 × 4)

### TM Display Invocation

- **Linux:** `flatpak run -p com.dwabtech.TM --tmdisplay --shmem tm-remote-display ...`
- **Windows:** `C:\Program Files (x86)\VEX\Tournament Manager\TM.exe --tmdisplay --shmem tm-remote-display ...`

Relevant TM flags passed through: `--framerate`, `--checkversion 0`, `--preview 0`, `--kiosk`, `--onlyscreen`, `--overlay 0`, `--server`, `--pw`.

### Phase 1 vs Phase 2

`phase1-test/` is an independent standalone test program that captures frames and writes them as `frame_NNN.png` files. It shares the same platform abstraction pattern and shared-memory approach but has its own copies of the platform and logging headers. `phase2-server/` is the production streaming server.
