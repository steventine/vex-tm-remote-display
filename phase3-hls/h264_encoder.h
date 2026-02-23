#pragma once

#include <stdint.h>
#include <stddef.h>

// H.264 encoder interface — mirrors jpeg_encoder.h in style.
// Default implementation uses libx264 (h264_encoder_x264.c).
// Swap for libavcodec by compiling h264_encoder_avcodec.c with -DHAVE_LIBAVCODEC.

typedef struct h264_encoder h264_encoder_t;

// Create encoder. Returns NULL on error.
// keyframe_interval: IDR frame every N frames (e.g. fps * segment_duration_sec).
h264_encoder_t* h264_encoder_create(int width, int height, int fps,
                                    int bitrate_kbps, int keyframe_interval);

void h264_encoder_destroy(h264_encoder_t* enc);

// Encode one BGRA frame.
// *nal_data points into the encoder's internal buffer — valid until next call.
// Caller must copy if needed before calling again.
// Returns NAL size (bytes), 0 on error or if encoder is buffering (flush on keyframe).
// *is_keyframe set to 1 if the output NAL starts an IDR frame.
size_t h264_encoder_encode(h264_encoder_t* enc, const uint8_t* bgra,
                           uint8_t** nal_data, int* is_keyframe);
