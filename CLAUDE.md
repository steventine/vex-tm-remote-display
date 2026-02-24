# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This project enables remote display of VEX Tournament Manager (TM) on any web browser over a LAN. A server process starts TM's headless display mode, reads frames via shared memory, and streams them to browsers. The current default transport is H.264 over HLS (`phase3-hls/`); the original MJPEG implementation is preserved in `phase2-server/` and still available via `--mode mjpeg`.

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

### Phase 3 (H.264/HLS streaming server) — current default

```bash
cd phase3-hls

# Linux: check deps then build
make check-deps
make

# Cross-compile Windows .exe from Linux
make CROSS_COMPILE_WINDOWS=1
```

**Linux dependencies:**
```bash
sudo apt install build-essential libx264-dev libjpeg-turbo8-dev
```

**Running the server:**
```bash
cd phase3-hls
./tm_stream_server [--port 8080] [--framerate 10] [--bitrate 3000] \
                   [--segment-duration 1] [--mode hls|mjpeg] \
                   [--server ADDR] [--pw PASSWORD] [--kiosk] [--onlyscreen N]
# Then browse to http://localhost:8080
```

**Testing without TM hardware (`test_inject`):**
```bash
# Terminal 1
./tm_stream_server --no-tm --framerate 30

# Terminal 2 — synthetic BGRA frames via shared memory
./test_inject --framerate 30 --pattern colorbars   # moving stripe
./test_inject --framerate 30 --pattern cycle        # hue cycle
./test_inject --framerate 30 --pattern noise        # worst-case compression
```

### Phase 2 (MJPEG streaming server)

```bash
cd phase2-server
make build
./tm_stream_server [--port 8080] [--framerate 10] [--quality 85] \
                   [--server ADDR] [--pw PASSWORD] [--kiosk] [--onlyscreen N]
```

**Linux dependencies:**
```bash
sudo apt install build-essential libjpeg-turbo8-dev
```

## Architecture

### Data Flow (Phase 3 HLS)

```
TM / flatpak TM  →  shared memory (BGRA 1920×1080)  →  frame_capture_thread
                                                               ↓
                                                      h264_encoder_encode()
                                                               ↓
                                                      ts_muxer_write_nal()
                                                               ↓ (on keyframe)
Browser  ←  HTTP GET /seg/N.ts  ←  hls_server  ←  hls_server_push_segment()
         ←  HTTP GET /stream.m3u8
         ←  HTTP GET /  (embedded HTML + HLS.js)
```

### Key Components (Phase 3)

| File | Purpose |
|------|---------|
| `tm_stream_server.c` | Main entry point; `--mode hls` (default) or `--mode mjpeg`; dual-mode frame capture thread |
| `hls_server.[ch]` | HTTP server; 5-segment ring buffer; M3U8 playlist; embedded HTML+HLS.js page |
| `h264_encoder.h` | Interface (mirrors `jpeg_encoder.h`) |
| `h264_encoder_x264.c` | x264 implementation; baseline profile, zero-delay config |
| `h264_encoder_avcodec.c` | Stub compiled with `-DHAVE_LIBAVCODEC` |
| `ts_muxer.h` | Interface |
| `ts_muxer_custom.c` | Custom MPEG-TS muxer; 188-byte packets, PAT+PMT+PES |
| `ts_muxer_avformat.c` | Stub compiled with `-DHAVE_LIBAVFORMAT` |
| `yuv_convert.[ch]` | BT.601 BGRA→YUV420 conversion |
| `test_inject.c` | Synthetic frame source (colorbars/cycle/noise); uses same shm+sem as TM |
| `platform.h` | Cross-platform abstraction for shared memory, semaphores, process spawning |
| `platform-posix.[ch]` | POSIX — `shm_open`, POSIX semaphores, `fork`/`execv`, `sem_timedwait` |
| `platform-windows.[ch]` | Windows — `CreateFileMapping`, Win32 semaphores, `CreateProcess` |
| `simple-log.h` | Minimal logging macros (`info()`, `error()`) |

### Frame Capture Thread Design

The capture thread uses two modes to handle active vs. idle sources:

- **Active mode**: `shm_sem_timedwait(sem, 100ms)` — blocks until TM posts. 100 ms is large enough that TM always posts before the timeout at any realistic FPS, keeping the capture thread phase-locked to TM with no independent timer to drift against.
- **Idle mode**: entered after 100 ms silence. Uses `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` to inject repeated frames at exactly the configured FPS, keeping the HLS stream alive during static content (e.g., Logo View between changes).

Do **not** use `clock_nanosleep` as the sole pacemaker for active content — it creates phase drift against the source that causes periodic frame repeats and visible jitter.

### Platform Abstraction (`platform.h`)

Wraps: `shm_fd_open/close`, `shm_mmap/close`, `shm_sem_create/wait/timedwait/close`, `plat_spawn`, `plat_kill`. `shm_sem_timedwait(sem, timeout_ms)` was added for the HLS frame capture thread.

### Shared Memory Convention

- Shared memory name: `tm-remote-display` (hardcoded)
- POSIX paths: `/tm-remote-display-fb` (framebuffer), `/tm-remote-display-sem` (semaphore)
- Windows names: same without leading slash
- Frame format: BGRA, 1920×1080, 4 bytes/pixel (IMG_BUF_SIZE = 1920 × 1080 × 4)

### TM Display Invocation

- **Linux:** `flatpak run -p com.dwabtech.TM --tmdisplay --shmem tm-remote-display ...`
- **Windows:** `C:\Program Files (x86)\VEX\Tournament Manager\TM.exe --tmdisplay --shmem tm-remote-display ...`

Flags passed through: `--framerate`, `--checkversion 0`, `--preview 0`, `--kiosk`, `--onlyscreen`, `--overlay 0`, `--server`, `--pw`.

### Phase Relationship

`phase1-test/` — standalone frame-capture test, saves PNGs. `phase2-server/` — MJPEG server (untouched reference). `phase3-hls/` — H.264/HLS server (current). Phase 3 copies platform/http/jpeg files from Phase 2 and adds the H.264 pipeline on top.
