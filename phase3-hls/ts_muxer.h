#pragma once

#include <stdint.h>
#include <stddef.h>

// MPEG-TS muxer for H.264 HLS segments.
// Default implementation: ts_muxer_custom.c (no external deps).
// Swap for libavformat by compiling ts_muxer_avformat.c with -DHAVE_LIBAVFORMAT.

typedef struct ts_muxer ts_muxer_t;

ts_muxer_t* ts_muxer_create(void);
void        ts_muxer_destroy(ts_muxer_t* mux);

// Append a NAL unit (Annex B, with start codes) to the in-progress segment.
//
// On keyframe (is_keyframe != 0):
//   - Finalises the previous segment (if any) into *seg_out / *seg_size.
//   - Starts a new segment beginning with PAT + PMT + this frame.
//   - Caller must call ts_muxer_free_segment(*seg_out) when done with the data.
//
// On non-keyframe:
//   - Appends data to the current segment.
//   - *seg_out = NULL, *seg_size = 0.
//
// pts_90khz: presentation timestamp in 90 kHz units (frame_number * 90000 / fps).
// Returns 0 on success, -1 on error.
int ts_muxer_write_nal(ts_muxer_t* mux,
                       const uint8_t* nal_data, size_t nal_size,
                       int64_t pts_90khz, int is_keyframe,
                       uint8_t** seg_out, size_t* seg_size);

// Flush the current in-progress segment (call at shutdown).
// *seg_out / *seg_size filled if there is pending data, else seg_size=0.
int ts_muxer_flush(ts_muxer_t* mux, uint8_t** seg_out, size_t* seg_size);

void ts_muxer_free_segment(uint8_t* seg);
