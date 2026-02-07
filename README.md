# VEX Tournament Manager Remote Display

This project aims to enable a user to remotely run a VEX Tournament Manager (TM) display on any standard web browser.  This way any PC, smartboard or tablet that has a modern browser and IP connectivity to the Tournament Manager can instantly show a TM display (like a pit display) without having to install any software.

One PC or Raspberry Pi needs to run the remote display server which hosts the display.  Then remote devices connect to this server using just a web browser.

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