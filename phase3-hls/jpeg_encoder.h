#pragma once

#include <stdint.h>
#include <stddef.h>

// JPEG encoder using libjpeg-turbo
// Returns size of encoded JPEG data, or 0 on error
// jpeg_data must be freed by caller using free()
size_t encode_bgra_to_jpeg(const uint8_t* bgra_data, int width, int height, 
                          uint8_t** jpeg_data, int quality);


