#ifdef HAVE_LIBAVCODEC
// Stub: libavcodec-backed H.264 encoder.
// Compile with -DHAVE_LIBAVCODEC to activate this implementation instead of
// h264_encoder_x264.c.  This file currently just declares the interface so the
// linker is satisfied; a real implementation would follow the same pattern as
// h264_encoder_x264.c but use avcodec_find_encoder(AV_CODEC_ID_H264),
// avcodec_alloc_context3(), avcodec_open2(), and avcodec_send_frame() /
// avcodec_receive_packet().

#include <stdlib.h>
#include "h264_encoder.h"
#include "simple-log.h"

struct h264_encoder {
    int placeholder;
};

h264_encoder_t* h264_encoder_create(int width, int height, int fps,
                                    int bitrate_kbps, int keyframe_interval) {
    (void)width; (void)height; (void)fps; (void)bitrate_kbps; (void)keyframe_interval;
    error("h264_encoder_avcodec: not yet implemented");
    return NULL;
}

void h264_encoder_destroy(h264_encoder_t* enc) {
    free(enc);
}

size_t h264_encoder_encode(h264_encoder_t* enc, const uint8_t* bgra,
                           uint8_t** nal_data, int* is_keyframe) {
    (void)enc; (void)bgra; (void)nal_data; (void)is_keyframe;
    return 0;
}

#endif /* HAVE_LIBAVCODEC */
