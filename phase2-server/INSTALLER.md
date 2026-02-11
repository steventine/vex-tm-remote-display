# Windows Installer

This directory contains files for building a Windows installer for the VEX TM Remote Display Server.

## Prerequisites

### On Windows

1. **NSIS (Nullsoft Scriptable Install System)**
   - Download from: https://nsis.sourceforge.io/Download
   - Install NSIS (typically to `C:\Program Files (x86)\NSIS\`)
   - Ensure `makensis.exe` is in your PATH, or use the full path

2. **Built Windows executable**
   - Build `tm_stream_server.exe` using MinGW/MSYS2 or Visual Studio
   - Place it in this directory before building the installer

### On Linux

1. **NSIS (Nullsoft Scriptable Install System)**
   - Install via package manager:
     ```bash
     sudo apt update
     sudo apt install nsis
     ```
   - Verify installation:
     ```bash
     makensis -VERSION
     ```

2. **Built Windows executable**
   - You need a compiled `tm_stream_server.exe` (Windows binary)
   - Options:
     - Cross-compile on Linux using MinGW-w64 (see Windows build instructions in README)
     - Build on Windows and copy the `.exe` to Linux
     - Use a Windows VM or CI/CD service
   - Place it in this directory before building the installer

## Building the Installer

### On Windows

#### Option 1: Using the batch script (Recommended)

```batch
build-installer.bat
```

#### Option 2: Manual build

```batch
makensis installer.nsi
```

Or with verbose output for debugging:

```batch
makensis /V4 installer.nsi
```

### On Linux

```bash
cd phase2-server
makensis installer.nsi
```

Or with verbose output for debugging:

```bash
makensis /V4 installer.nsi
```

**Verify the output:**
```bash
ls -lh VEXTMRemoteDisplayServer-*.exe
```

**Note:** The generated installer is a Windows executable and must be tested on a Windows system (VM or physical machine) before distribution.

The installer will be created as: `VEXTMRemoteDisplayServer-1.0.0-Setup.exe`

## Installer Features

- **Standard Windows Installer**: Uses NSIS for a professional installer experience
- **Start Menu Shortcuts**: Creates shortcuts in the Start Menu
- **Optional Desktop Shortcut**: User can choose to create a desktop shortcut
- **Optional PATH Addition**: User can choose to add to system PATH
- **Uninstaller**: Includes a proper uninstaller
- **Registry Entries**: Properly registers the application

## Installation Options

Users can choose:
1. **Main Application** (required) - Installs the executable
2. **Desktop Shortcut** (optional) - Creates a desktop shortcut
3. **Add to PATH** (optional) - Adds installation directory to system PATH

## Installation Location

Default installation directory:
```
C:\Program Files\VEX TM Remote Display Server\
```

## Building the Windows Executable

Before building the installer, you need to compile the Windows version:

### Cross-Compile from Linux (Recommended):

The project includes `Makefile.Windows` that automatically handles cross-compilation and installer creation:

```bash
cd phase2-server
# Build Windows executable and installer in one command
make CROSS_COMPILE_WINDOWS=1 installer
```

This will:
1. Cross-compile `tm_stream_server.exe` using MinGW-w64
2. Strip the binary
3. Build the Windows installer using NSIS

**Prerequisites:**
- MinGW-w64 cross-compiler: `sudo apt install gcc-mingw-w64-x86-64`
- Windows libjpeg libraries (see README for installation options)
- NSIS: `sudo apt install nsis`

See the main README for detailed cross-compilation instructions.

### Using MSYS2/MinGW (Windows):

```bash
# In MSYS2 terminal
cd phase2-server
make
# This creates tm_stream_server.exe
```

### Using Visual Studio (Windows):

1. Create a Visual Studio project
2. Add all source files
3. Link against libjpeg-turbo or libjpeg
4. Build Release configuration

## Notes

- The installer requires administrator privileges (for PATH modification)
- The installer script assumes the executable is named `tm_stream_server.exe`
- If using DLLs (like libjpeg DLLs), add them to the installer script
- The installer version number is set in `installer.nsi` - update it when releasing new versions
- PATH modification uses standard NSIS registry functions (no external plugins required)
- Building on Linux requires the native NSIS package (available via `apt install nsis`)

## Optional: Including Dependencies

If libjpeg is distributed as DLLs, add them to the installer:

```nsis
; In the Main Application section
File "libjpeg-62.dll"  ; or whatever the DLL is named
File "libturbojpeg.dll"  ; if using libjpeg-turbo
```

## Testing the Installer

1. Build the installer
2. Run it on a clean Windows system (or VM)
3. Test installation
4. Test running the application
5. Test uninstallation

