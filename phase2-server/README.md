# Phase 2: MJPEG Streaming Server

This phase implements an HTTP server that streams Tournament Manager display frames to web browsers using MJPEG (Motion JPEG).

## Architecture

The server:
1. Reads frames from shared memory (same approach as Phase 1)
2. Encodes frames to JPEG format
3. Streams frames as MJPEG over HTTP (multipart response)
4. Serves a web client that displays the stream

## Features

- Low latency (~100-200ms)
- Efficient JPEG encoding (lower CPU than H.264)
- Works with standard web browsers (no plugins)
- Supports multiple simultaneous clients
- Reuses UI from vex-tm-rpi-web

## Building

### Dependencies

#### Linux

**Required:**
- C compiler (gcc or clang)
- Make
- libjpeg or libjpeg-turbo (for JPEG encoding)
- pthread library (usually included with glibc)

**Installation:**

**Debian/Ubuntu:**
```bash
# Install build tools
sudo apt update
sudo apt install build-essential

# Install libjpeg-turbo (recommended - faster)
sudo apt install libjpeg-turbo8-dev

# OR install standard libjpeg (alternative - slower)
# sudo apt install libjpeg-dev
```

**Fedora/RHEL/CentOS:**
```bash
# Install build tools
sudo dnf groupinstall "Development Tools"

# Install libjpeg-turbo (recommended - faster)
sudo dnf install libjpeg-turbo-devel

# OR install standard libjpeg (alternative - slower)
# sudo dnf install libjpeg-devel
```

**Arch Linux:**
```bash
# Install build tools
sudo pacman -S base-devel

# Install libjpeg-turbo (recommended - faster)
sudo pacman -S libjpeg-turbo

# OR install standard libjpeg (alternative - slower)
# sudo pacman -S libjpeg
```

**Raspberry Pi (Raspbian):**
```bash
# Install build tools
sudo apt update
sudo apt install build-essential

# Install libjpeg-turbo (recommended)
sudo apt install libjpeg-turbo8-dev

# OR install standard libjpeg
# sudo apt install libjpeg-dev
```

**Note:** If using standard libjpeg, ensure you have version 9+ (for `jpeg_mem_dest` support). Most modern distributions include this.

#### Windows

**Required:**
- C compiler (MinGW-w64 or MSVC)
- Make (or use CMake/Ninja)
- libjpeg or libjpeg-turbo

**Installation Options:**

**Option 1: Using MSYS2/MinGW-w64 (Recommended)**
```bash
# Install MSYS2 from https://www.msys2.org/
# Then in MSYS2 terminal:

# Update package database
pacman -Syu

# Install build tools
pacman -S base-devel mingw-w64-x86_64-toolchain

# Install libjpeg-turbo
pacman -S mingw-w64-x86_64-libjpeg-turbo

# OR install standard libjpeg
# pacman -S mingw-w64-x86_64-libjpeg
```

**Option 2: Using vcpkg**
```bash
# Install vcpkg from https://github.com/Microsoft/vcpkg
# Then:

# Install libjpeg-turbo
vcpkg install libjpeg-turbo:x64-windows

# OR install libjpeg
# vcpkg install libjpeg:x64-windows
```

**Option 3: Manual Installation**
1. Download libjpeg-turbo from: https://github.com/libjpeg-turbo/libjpeg-turbo/releases
2. Extract and build following their instructions
3. Update Makefile to point to the installation location

**Note:** The Makefile is currently configured for Linux. For Windows, you may need to:
- Use a Windows-compatible build system (CMake, Visual Studio, etc.)
- Or modify the Makefile to work with MinGW/MSYS2

### Build

**Linux:**
```bash
cd phase2-server
make build
```

Or if dependencies are already installed:
```bash
make
```

**Windows (MSYS2/MinGW):**
```bash
cd phase2-server
# May need to adjust Makefile paths for Windows
make
```

**Verify Installation:**
```bash
# Check if libjpeg-turbo is available
pkg-config --exists libturbojpeg && echo "libjpeg-turbo found" || echo "libjpeg-turbo not found"

# Check if standard libjpeg is available
pkg-config --exists libjpeg && echo "libjpeg found" || echo "libjpeg not found"

# Or check for header files directly
ls /usr/include/jpeglib.h /usr/include/turbojpeg.h 2>/dev/null
```

### Troubleshooting

**Linux:**

**Issue: "jpeglib.h: No such file or directory"**
- Solution: Install the development package: `sudo apt install libjpeg-dev` or `sudo apt install libjpeg-turbo8-dev`

**Issue: "undefined reference to `jpeg_*`"**
- Solution: Ensure you're linking against libjpeg: `-ljpeg` or `-lturbojpeg` in LDFLAGS

**Issue: "jpeg_mem_dest not found"**
- Solution: You need libjpeg 9+ or libjpeg-turbo. Install libjpeg-turbo: `sudo apt install libjpeg-turbo8-dev`

**Windows:**

**Issue: "Cannot find jpeglib.h"**
- Solution: Ensure libjpeg-turbo is installed via MSYS2 or vcpkg, and update include paths in Makefile/CMakeLists.txt

**Issue: Makefile doesn't work on Windows**
- Solution: Use MSYS2/MinGW environment, or convert to CMake/Visual Studio project

## Usage

```bash
./tm_stream_server [options]
```

Options:
- `--port N` - HTTP server port (default: 8080)
- `--framerate N` - Frame rate for TM Display (default: 10)
- `--server ADDR` - Tournament Manager server address
- `--pw PASSWORD` - Tournament Manager password
- `--kiosk` - Enable kiosk mode for TM Display
- `--help` - Show help message

## Web Client

Once the server is running, open a web browser and navigate to:
```
http://localhost:8080
```

Or if running on a remote machine:
```
http://<server-ip>:8080
```

The web client will automatically connect to the MJPEG stream and display it.

