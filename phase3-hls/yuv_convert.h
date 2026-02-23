#pragma once

#include <stdint.h>

// Convert BGRA frame to planar YUV 4:2:0
// out_yuv must be pre-allocated: width * height * 3 / 2 bytes
// Layout: Y plane (width x height), U plane (width/2 x height/2), V plane (width/2 x height/2)
// width and height must be even.
void bgra_to_yuv420(const uint8_t* bgra, int width, int height, uint8_t* out_yuv);
