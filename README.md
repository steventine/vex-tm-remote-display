# VEX Tournament Manager Remote Display

This project aims to enable a user to remotely run a VEX Tournament Manager (TM) display on any standard web browser.  This way any PC, smartboard or tablet that has a modern browser and IP connectivity to the Tournament Manager can instantly show a TM display (like a pit display) without having to install any software.

One PC or Raspberry Pi needs to run the remote display server which hosts the display.  Then remote devices connect to this server using just a web browser.

# Considerations

* Latency is somewhat important, as we want to keep the display consistently under 3-5 sec of latency

* CPU usage of the server is also important as it would be ideal to run this on a lower powered device (like a Raspberry Pi)

* Portability is also useful so we can run this a Linux based Raspberry Pi or a Windows PC

# References

## vex-tm-rpi-web

(vex-tm-rpi-web)[file://../vext-tm-rpi-web/] has a very similar purpose but it simply fetches the `screen.png` file as fast as it can. 

Cons:
* This approach is basically using an MJPEG approach so it is not very smooth to view and isn't very bandwidth efficient 
* The `screen.png` file is large so each remote display consumes more than 1 Mbps of wireless bandwidth
* The `screen.png` file can only be refrehed at a max rate of about 0.9 FPS, and that rate drops when multiple remote displays are connected to the same Raspberry Pi

Pros: 
* The user interface in the web browser is really nice (with support for fullscreen viewing and overlays that disapper when the mouse isn't moving)
* No software needs to be installed at all

## vex-obs-source

(vextm-obs-source)[file://../vextm-obs-source/] has code for starting a display from Tournament Manager and putting each frame into a shared memory buffer so it can be easily accessed in code.  This could be a good reference for how to get raw access to the Tournament Manager display.  From this, we should be able to use some open source libraries (like FFMPEG) to delivery this content efficiently to the remote web browser using HLS or WebRTC.

* Code that starts a local TM display with the `--shmem <name>` argument which includes a name for the shared memory buffer and semaphore is (here) [file://../vextm-obs-source/vextm-source.c#L139]

* The local TM display also supports a `--framerate` argument that could be used to lower the framerate for performance reasons (like 10-15 FPS would likely be sufficient for this purpose)

* Code that opens the shared memory buffer and reads it (with the semaphore) in a loop is (here)[file://../vextm-obs-source/vextm-thread.c#L36]

NOTE: This code works on both Windows and Linux which is nice.

# Installation

## Installing Tournament Manager (Linux)

If you have the Tournament Manager flatpak file, follow these steps to install it:

1. **Add the Flathub repository** (required for the runtime dependencies):
   ```bash
   flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
   ```

2. **Install the required runtime** (Tournament Manager requires org.freedesktop.Platform 23.08):
   ```bash
   flatpak install --user flathub org.freedesktop.Platform/x86_64/23.08
   ```
   Note: This runtime is end-of-life but required by Tournament Manager. You'll need to confirm the installation when prompted.

3. **Install Tournament Manager from the flatpak file**:
   ```bash
   flatpak install --user /path/to/VEXTournamentManager.flatpak
   ```
   For example, if the file is in `../vex-tm/`:
   ```bash
   flatpak install --user ../vex-tm/VEXTournamentManager.flatpak
   ```
   Note: If the runtime from step 2 is not already installed, flatpak will prompt you to install it. Answer 'Y' to proceed.

4. **Verify the installation**:
   ```bash
   flatpak list | grep dwabtech
   ```
   You should see `com.dwabtech.TM` listed.

## Installing Tournament Manager (Windows)

On Windows, Tournament Manager is typically installed to:
```
C:\Program Files (x86)\VEX\Tournament Manager\TM.exe
```

The program will automatically detect this location.

# Implementation Phases

## Phase 1: Frame Capture Test (Current)

The first phase is a simple test program to verify we can:
1. Start the TM display with shared memory
2. Read frames from shared memory
3. Save frames as PNG images locally

See [phase1-test/README.md](phase1-test/README.md) for details on building and running the test program.

Once Phase 1 is verified (frames are captured correctly), we can proceed to Phase 2 (streaming to web browsers).