#include <stdint.h>
#include "yuv_convert.h"

// BT.601 limited-range coefficients (standard for H.264 baseline)
// Y  =  16 + 65.481*R + 128.553*G + 24.966*B  (scaled to 0..235)
// Cb = 128 - 37.797*R - 74.203*G + 112.0*B    (scaled to 16..240)
// Cr = 128 + 112.0*R  - 93.786*G - 18.214*B   (scaled to 16..240)
//
// Using integer arithmetic with >>8 shift (256 multiplier):
//   Y  = (( 66*R + 129*G +  25*B + 128) >> 8) + 16
//   Cb = ((-38*R -  74*G + 112*B + 128) >> 8) + 128
//   Cr = ((112*R -  94*G -  18*B + 128) >> 8) + 128

void bgra_to_yuv420(const uint8_t* bgra, int width, int height, uint8_t* out_yuv) {
    uint8_t* y_plane = out_yuv;
    uint8_t* u_plane = out_yuv + width * height;
    uint8_t* v_plane = u_plane + (width / 2) * (height / 2);

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            // BGRA order: byte0=B, byte1=G, byte2=R, byte3=A
            const uint8_t* px = bgra + (row * width + col) * 4;
            int b = px[0];
            int g = px[1];
            int r = px[2];

            int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            y_plane[row * width + col] = (uint8_t)(y < 16 ? 16 : y > 235 ? 235 : y);

            // Subsample U/V: one sample per 2x2 block (top-left pixel)
            if ((row & 1) == 0 && (col & 1) == 0) {
                int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                int uv_idx = (row / 2) * (width / 2) + (col / 2);
                u_plane[uv_idx] = (uint8_t)(u < 16 ? 16 : u > 240 ? 240 : u);
                v_plane[uv_idx] = (uint8_t)(v < 16 ? 16 : v > 240 ? 240 : v);
            }
        }
    }
}
