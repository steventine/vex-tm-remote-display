#ifdef HAVE_LIBAVFORMAT
// Stub: libavformat-backed MPEG-TS muxer.
// Compile with -DHAVE_LIBAVFORMAT to activate this implementation instead of
// ts_muxer_custom.c.  A real implementation would use avformat_alloc_context(),
// av_guess_format("mpegts"), avio_open_dyn_buf(), avformat_write_header(),
// av_interleaved_write_frame(), and avio_close_dyn_buf().

#include <stdlib.h>
#include "ts_muxer.h"
#include "simple-log.h"

struct ts_muxer {
    int placeholder;
};

ts_muxer_t* ts_muxer_create(void) {
    error("ts_muxer_avformat: not yet implemented");
    return NULL;
}

void ts_muxer_destroy(ts_muxer_t* mux) {
    free(mux);
}

int ts_muxer_write_nal(ts_muxer_t* mux,
                       const uint8_t* nal_data, size_t nal_size,
                       int64_t pts_90khz, int is_keyframe,
                       uint8_t** seg_out, size_t* seg_size) {
    (void)mux; (void)nal_data; (void)nal_size;
    (void)pts_90khz; (void)is_keyframe;
    if (seg_out) *seg_out = NULL;
    if (seg_size) *seg_size = 0;
    return -1;
}

int ts_muxer_flush(ts_muxer_t* mux, uint8_t** seg_out, size_t* seg_size) {
    (void)mux;
    if (seg_out) *seg_out = NULL;
    if (seg_size) *seg_size = 0;
    return 0;
}

void ts_muxer_free_segment(uint8_t* seg) {
    free(seg);
}

#endif /* HAVE_LIBAVFORMAT */
