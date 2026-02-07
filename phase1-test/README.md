# Phase 1: Frame Capture Test

This is a simple test program to verify that we can:
1. Start the VEX Tournament Manager display with shared memory
2. Read frames from shared memory
3. Save frames as PNG images

## Prerequisites

### Required
- C compiler (gcc or clang)
- Make
- VEX Tournament Manager installed
  - Windows: `C:\Program Files (x86)\VEX\Tournament Manager\TM.exe`
  - Linux: Flatpak package `com.dwabtech.TM`
    - See the main [README.md](../README.md#installation) for installation instructions

### Download stb_image_write.h

You need to download the `stb_image_write.h` header file (single-file library for PNG writing):

```bash
cd phase1-test
wget https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
```

Or manually download from: https://github.com/nothings/stb/blob/master/stb_image_write.h

## Building

```bash
cd phase1-test
make build
```

Or if you've already downloaded stb_image_write.h:

```bash
make
```

## Usage

### Basic usage (capture 20 frames at 10 FPS):
```bash
./test_frame_capture
```

### With Tournament Manager server:
```bash
./test_frame_capture --server 192.168.1.100 --pw mypassword
```

### Custom frame count and framerate:
```bash
./test_frame_capture --frames 50 --framerate 15
```

### Command line options:
- `--frames N` - Number of frames to capture (default: 20)
- `--framerate N` - Frame rate for TM Display (default: 10)
- `--server ADDR` - Tournament Manager server address
- `--pw PASSWORD` - Tournament Manager password
- `--help, -h` - Show help message

## Output

The program will create PNG files named `frame_000.png`, `frame_001.png`, etc. in the current directory.

## Verification

1. Run the program
2. Check that PNG files are created
3. Open one of the PNG files to verify the frame content matches what's on the TM Display
4. If frames look correct, Phase 1 is complete! Proceed to Phase 2 (streaming).

## Troubleshooting

### "Failed to start TM Display process"
- Make sure Tournament Manager is installed
- On Linux, make sure the flatpak package is installed: `flatpak install com.dwabtech.TM`
- Check that the display command path is correct in `test_frame_capture.c`

### "Failed to open shared memory" or "Failed to create semaphore"
- Make sure TM Display started successfully
- Try waiting a bit longer (the program waits 2 seconds, but you might need more)
- Check that no other process is using the same shared memory name

### No frames captured
- Make sure TM Display is actually running and showing content
- Try increasing the framerate or waiting longer
- Check that the semaphore is being signaled (TM Display might not be writing frames)

### Frames are black or corrupted
- Verify the frame dimensions match (1920x1080)
- Check that the shared memory format is BGRA (32-bit, 4 bytes per pixel)
- Make sure TM Display is actually rendering content

