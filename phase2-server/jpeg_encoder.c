#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "jpeg_encoder.h"

#ifdef HAVE_LIBJPEG_TURBO
#include <turbojpeg.h>
#else
#include <jpeglib.h>
#endif

// Convert BGRA to RGB for JPEG encoding
static void convert_bgra_to_rgb(const uint8_t* bgra, uint8_t* rgb, int width, int height) {
    for(int i = 0; i < width * height; i++) {
        rgb[i * 3 + 0] = bgra[i * 4 + 2]; // R = B
        rgb[i * 3 + 1] = bgra[i * 4 + 1]; // G = G
        rgb[i * 3 + 2] = bgra[i * 4 + 0]; // B = R
    }
}

size_t encode_bgra_to_jpeg(const uint8_t* bgra_data, int width, int height, 
                          uint8_t** jpeg_data, int quality) {
    if(bgra_data == NULL || jpeg_data == NULL || width <= 0 || height <= 0) {
        return 0;
    }
    
    if(quality < 1) quality = 1;
    if(quality > 100) quality = 100;
    
    // Convert BGRA to RGB
    uint8_t* rgb_data = (uint8_t*)malloc(width * height * 3);
    if(rgb_data == NULL) {
        return 0;
    }
    convert_bgra_to_rgb(bgra_data, rgb_data, width, height);
    
#ifdef HAVE_LIBJPEG_TURBO
    // Use libjpeg-turbo for faster encoding
    tjhandle handle = tjInitCompress();
    if(handle == NULL) {
        free(rgb_data);
        return 0;
    }
    
    unsigned long jpeg_size = 0;
    uint8_t* jpeg_buf = NULL;
    
    int result = tjCompress2(handle, rgb_data, width, 0, height, TJPF_RGB,
                             &jpeg_buf, &jpeg_size, TJSAMP_444, quality, 
                             TJFLAG_FASTDCT);
    
    free(rgb_data);
    tjDestroy(handle);
    
    if(result != 0) {
        if(jpeg_buf) tjFree(jpeg_buf);
        return 0;
    }
    
    *jpeg_data = jpeg_buf;
    return (size_t)jpeg_size;
#else
    // Use standard libjpeg
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    
    // Set up memory destination (requires libjpeg 9+)
    // For older versions, we'd need to use a file, but let's assume modern libjpeg
    unsigned char* jpeg_buf = NULL;
    unsigned long jpeg_size = 0;
    
    // Try jpeg_mem_dest (available in libjpeg 9+)
    // If not available, this will need to be handled differently
    #if JPEG_LIB_VERSION >= 80 || defined(JPEG_DEST_MEM_SUPPORTED)
    jpeg_mem_dest(&cinfo, &jpeg_buf, &jpeg_size);
    #else
    // Fallback: would need to use a temporary file
    // For now, require libjpeg 9+ or libjpeg-turbo
    free(rgb_data);
    return 0;
    #endif
    
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    
    jpeg_start_compress(&cinfo, TRUE);
    
    JSAMPROW row_pointer[1];
    int row_stride = width * 3;
    
    while(cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &rgb_data[cinfo.next_scanline * row_stride];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    
    free(rgb_data);
    
    *jpeg_data = jpeg_buf;
    return (size_t)jpeg_size;
#endif
}

