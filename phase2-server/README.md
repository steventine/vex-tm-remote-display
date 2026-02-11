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
- Or cross-compile from Linux (see below)

#### Cross-Compiling from Linux

You can build Windows executables on Linux using MinGW-w64 cross-compiler. This is useful for:
- Building Windows installers on Linux
- CI/CD pipelines
- Development on Linux while targeting Windows

**Step 1: Install MinGW-w64 Cross-Compiler**

**Debian/Ubuntu:**
```bash
sudo apt update
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64
```

**Fedora/RHEL/CentOS:**
```bash
sudo dnf install mingw64-gcc mingw64-gcc-c++
```

**Arch Linux:**
```bash
sudo pacman -S mingw-w64-gcc
```

**Verify installation:**
```bash
x86_64-w64-mingw32-gcc --version
```

**Step 2: Install Windows libjpeg Libraries**

You have several options for getting libjpeg for Windows:

**Option A: Use MSYS2 packages (Recommended)**

If you have MSYS2 installed, you can use the pre-built Windows libraries:
```bash
# In MSYS2 terminal
pacman -S mingw-w64-x86_64-libjpeg-turbo
# Libraries will be in: /mingw64/lib/
# Headers will be in: /mingw64/include/
```

**Option B: Download pre-built Windows libraries**

1. Download libjpeg-turbo Windows binaries from: https://github.com/libjpeg-turbo/libjpeg-turbo/releases
2. Extract to a directory (e.g., `~/mingw-w64-libs/`)
3. Note the paths to `include/` and `lib/` directories

**Option C: Build from source (Advanced)**

1. Download libjpeg-turbo source
2. Cross-compile using MinGW-w64:
   ```bash
   # This is a simplified example - see libjpeg-turbo documentation
   ./configure --host=x86_64-w64-mingw32 --prefix=$HOME/mingw-w64-libs
   make && make install
   ```

**Step 3: Cross-Compile the Application**

The project includes a `Makefile.Windows` that handles cross-compilation automatically, similar to `vextm-obs-source`.

**Simple Method (Recommended):**

```bash
cd phase2-server

# Build Windows executable and installer in one command
make CROSS_COMPILE_WINDOWS=1 installer
```

This will:
1. Cross-compile `tm_stream_server.exe` using MinGW-w64
2. Strip the binary
3. Build the Windows installer using NSIS

**Build just the executable:**
```bash
make CROSS_COMPILE_WINDOWS=1
```

**Check if makensis is available:**
```bash
make CROSS_COMPILE_WINDOWS=1 check-makensis
```

**The Makefile.Windows automatically:**
- Detects MinGW-w64 library paths (`/mingw64` or `/usr/x86_64-w64-mingw32`)
- Finds libjpeg-turbo or falls back to standard libjpeg
- Finds `makensis` (works on both Linux and Windows)
- Strips the binary before creating the installer
- Creates the installer executable

**Alternative: Manual Method (if you need more control)**

If you prefer to set environment variables manually:

```bash
cd phase2-server

# Set cross-compiler
export CC=x86_64-w64-mingw32-gcc
export EXE=tm_stream_server.exe
export PLATFORM_SRC=platform-windows.c

# If using MSYS2 libraries, set paths:
export CFLAGS="-Wall -Wextra -O2 -std=c11 -pthread -I/mingw64/include"
export LDFLAGS="-L/mingw64/lib -ljpeg -lpthread"

# Build
make clean
make CC=$CC EXE=$EXE PLATFORM_SRC=$PLATFORM_SRC CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS"
```

**Step 4: Verify the Windows Executable**

```bash
file tm_stream_server.exe
# Should show: PE32+ executable (console) x86-64, for MS Windows

# Check if it's linked correctly (optional)
x86_64-w64-mingw32-objdump -p tm_stream_server.exe | grep -i dll
```

**Troubleshooting Cross-Compilation:**

**Issue: "jpeglib.h: No such file or directory"**
- Solution: Ensure `-I` flag points to the correct include directory
- Check: `ls $(MINGW_PREFIX)/include/jpeglib.h` or `ls $(MINGW_PREFIX)/include/turbojpeg.h`

**Issue: "undefined reference to `jpeg_*`"**
- Solution: Ensure `-L` flag points to the correct lib directory and `-ljpeg` is included
- Check: `ls $(MINGW_PREFIX)/lib/libjpeg*.a` or `ls $(MINGW_PREFIX)/lib/libturbojpeg*.a`

**Issue: "cannot find -lpthread"**
- Solution: MinGW-w64 uses `-lpthread` but may need `-lwinpthread` instead
- Try: Replace `-lpthread` with `-lwinpthread` in LDFLAGS

**Issue: Executable doesn't run on Windows**
- Solution: Ensure all required DLLs are present (libjpeg DLLs if using dynamic linking)
- Use `x86_64-w64-mingw32-objdump -p tm_stream_server.exe | grep DLL` to check dependencies
- Consider static linking or bundling DLLs with the installer

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

**Cross-Compile from Linux (with installer):**
```bash
cd phase2-server
# Build Windows executable and installer
make CROSS_COMPILE_WINDOWS=1 installer
```

**Cross-Compile from Linux (executable only):**
```bash
cd phase2-server
# Build just the Windows executable
make CROSS_COMPILE_WINDOWS=1
```

See "Cross-Compiling from Linux" section above for detailed dependency installation instructions.

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

**Cross-Compilation (Linux to Windows):**

**Issue: "jpeglib.h: No such file or directory" when cross-compiling**
- Solution: Ensure `-I` flag points to the correct include directory
- Check: `ls $(MINGW_PREFIX)/include/jpeglib.h` or `ls $(MINGW_PREFIX)/include/turbojpeg.h`
- If using MSYS2: `ls /mingw64/include/jpeglib.h`

**Issue: "undefined reference to `jpeg_*`" when cross-compiling**
- Solution: Ensure `-L` flag points to the correct lib directory and `-ljpeg` is included
- Check: `ls $(MINGW_PREFIX)/lib/libjpeg*.a` or `ls $(MINGW_PREFIX)/lib/libturbojpeg*.a`
- If using MSYS2: `ls /mingw64/lib/libjpeg*.a`

**Issue: "cannot find -lpthread" when cross-compiling**
- Solution: MinGW-w64 uses `-lpthread` but may need `-lwinpthread` instead
- Try: Replace `-lpthread` with `-lwinpthread` in LDFLAGS

**Issue: Executable doesn't run on Windows**
- Solution: Ensure all required DLLs are present (libjpeg DLLs if using dynamic linking)
- Use `x86_64-w64-mingw32-objdump -p tm_stream_server.exe | grep DLL` to check dependencies
- Consider static linking or bundling DLLs with the installer

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

## Windows Installer

A Windows installer is available for easy deployment on Windows systems. The installer provides a standard Windows installation experience with Start Menu shortcuts, optional desktop shortcut, and optional PATH integration.

### Prerequisites for Building the Installer

1. **NSIS (Nullsoft Scriptable Install System)**
   - Download from: https://nsis.sourceforge.io/Download
   - Install NSIS (typically to `C:\Program Files (x86)\NSIS\`)
   - Ensure `makensis.exe` is in your system PATH
   - To verify NSIS is installed correctly:
     ```batch
     makensis /VERSION
     ```

2. **Built Windows Executable**
   - You must have a compiled `tm_stream_server.exe` in the `phase2-server` directory
   - See the "Building" section above for instructions on compiling the Windows version
   - Verify the executable exists:
     ```batch
     dir tm_stream_server.exe
     ```

### Building the Installer

#### Step 1: Prepare the Build Environment

1. **Navigate to the phase2-server directory:**
   ```batch
   cd phase2-server
   ```

2. **Verify prerequisites:**
   - Ensure `tm_stream_server.exe` exists in the current directory
   - Ensure NSIS is installed and `makensis.exe` is accessible

#### Step 2: Build Using the Batch Script (Recommended)

The easiest way to build the installer is using the provided batch script:

```batch
build-installer.bat
```

This script will:
- Check if NSIS (`makensis`) is available
- Build the installer using `installer.nsi`
- Report success or failure

**Expected output:**
```
Building Windows installer for VEX TM Remote Display Server...
Processing script file: "installer.nsi"
...
Output: VEXTMRemoteDisplayServer-1.0.0-Setup.exe
Installation log: C:\Users\...\install.log
```

#### Step 3: Build Manually (Alternative)

If you prefer to build manually or the batch script doesn't work:

```batch
makensis installer.nsi
```

Or with verbose output for debugging:

```batch
makensis /V4 installer.nsi
```

#### Step 4: Verify the Installer

After building, verify the installer was created:

```batch
dir VEXTMRemoteDisplayServer-*.exe
```

You should see: `VEXTMRemoteDisplayServer-1.0.0-Setup.exe`

**Test the installer:**
- Right-click the installer and select "Run as administrator" (required for PATH modification)
- Or double-click to run (will prompt for admin privileges if needed)
- Follow the installation wizard
- Verify the installation in `C:\Program Files\VEX TM Remote Display Server\`

### Installer Features

The installer includes:

- **Main Application** (required)
  - Installs `tm_stream_server.exe` to `C:\Program Files\VEX TM Remote Display Server\`
  - Creates Start Menu shortcuts
  - Registers uninstaller in Windows

- **Desktop Shortcut** (optional)
  - Creates a desktop shortcut for quick access

- **Add to PATH** (optional)
  - Adds the installation directory to the system PATH
  - Allows running `tm_stream_server.exe` from any command prompt
  - Requires administrator privileges

### Troubleshooting Installer Build

**Issue: "makensis: command not found" or "makensis is not recognized"**

- **Solution 1:** Add NSIS to your PATH:
  - Add `C:\Program Files (x86)\NSIS\` to your system PATH environment variable
  - Or use the full path: `"C:\Program Files (x86)\NSIS\makensis.exe" installer.nsi`

- **Solution 2:** Use the NSIS command prompt:
  - From the Start Menu, open "NSIS" → "NSIS Command Prompt"
  - Navigate to `phase2-server` directory
  - Run `makensis installer.nsi`

**Issue: "File: tm_stream_server.exe not found"**

- **Solution:** Build the Windows executable first:
  ```bash
  # In MSYS2 terminal
  cd phase2-server
  make
  ```
  Or compile using Visual Studio or your preferred build system.

**Issue: Installer builds but fails to run**

- **Solution:** Ensure you're running the installer as administrator (right-click → "Run as administrator")
- Check Windows Event Viewer for detailed error messages
- Verify the executable works when run directly from the installation directory

**Issue: "Error writing to registry" or PATH modification fails**

- **Solution:** The installer requires administrator privileges. Right-click the installer and select "Run as administrator"

### Customizing the Installer

To customize the installer (version number, product name, etc.), edit `installer.nsi`:

- **Version number:** Change `!define PRODUCT_VERSION "1.0.0"`
- **Product name:** Change `!define PRODUCT_NAME "VEX TM Remote Display Server"`
- **Installation directory:** Change `InstallDir "$PROGRAMFILES\VEX TM Remote Display Server"`

After making changes, rebuild the installer using the steps above.

### Distribution

The generated installer (`VEXTMRemoteDisplayServer-1.0.0-Setup.exe`) is a standalone executable that can be:
- Distributed to end users
- Run on any Windows system (Windows 7 or later)
- Installed without requiring additional dependencies

**Note:** The installer does not include the Tournament Manager application itself. Users must have Tournament Manager installed separately (see main README for installation instructions).

For more detailed information, see [INSTALLER.md](INSTALLER.md).

