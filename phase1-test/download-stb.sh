#!/bin/bash
# Download stb_image_write.h for PNG support

echo "Downloading stb_image_write.h..."
wget -q https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h -O stb_image_write.h

if [ $? -eq 0 ]; then
    echo "Successfully downloaded stb_image_write.h"
    ls -lh stb_image_write.h
else
    echo "ERROR: Failed to download stb_image_write.h"
    echo "Please download manually from:"
    echo "https://github.com/nothings/stb/blob/master/stb_image_write.h"
    exit 1
fi

