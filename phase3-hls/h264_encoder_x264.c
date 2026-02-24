#ifndef HAVE_LIBAVCODEC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <x264.h>
#include "h264_encoder.h"
#include "yuv_convert.h"
#include "simple-log.h"

struct h264_encoder {
    x264_t*         handle;
    x264_picture_t  pic_in;
    x264_picture_t  pic_out;
    int             width;
    int             height;
    int             frame_num;
    // Accumulated NAL output buffer (concatenated from x264 nal array)
    uint8_t*        nal_buf;
    size_t          nal_buf_size;
    // Scratch YUV buffer
    uint8_t*        yuv_buf;
};

h264_encoder_t* h264_encoder_create(int width, int height, int fps,
                                    int bitrate_kbps, int keyframe_interval) {
    h264_encoder_t* enc = calloc(1, sizeof(h264_encoder_t));
    if (!enc) return NULL;

    enc->width  = width;
    enc->height = height;

    // YUV 4:2:0 scratch buffer
    enc->yuv_buf = malloc((size_t)width * height * 3 / 2);
    if (!enc->yuv_buf) {
        free(enc);
        return NULL;
    }

    x264_param_t param;
    // veryfast preset, baseline profile — broadest device compatibility
    if (x264_param_default_preset(&param, "veryfast", NULL) < 0) {
        error("x264_param_default_preset failed");
        free(enc->yuv_buf);
        free(enc);
        return NULL;
    }

    param.i_width            = width;
    param.i_height           = height;
    param.i_fps_num          = (uint32_t)fps;
    param.i_fps_den          = 1;
    param.i_keyint_max       = keyframe_interval;
    param.i_keyint_min       = keyframe_interval;
    param.b_open_gop         = 0;        // closed GOP — required for HLS seekability
    param.i_bframe           = 0;        // baseline: no B-frames
    param.rc.i_rc_method       = X264_RC_ABR;
    param.rc.i_bitrate         = bitrate_kbps;
    param.rc.i_vbv_max_bitrate = 0;  // No VBV peak constraint — lets x264 spike
    param.rc.i_vbv_buffer_size = 0;  // for complex frames (sudden content changes)
    param.i_log_level        = X264_LOG_WARNING;
    param.b_annexb           = 1;        // Annex B start codes (needed for TS muxer)
    param.b_repeat_headers   = 1;        // Include SPS/PPS before every IDR (required for HLS segments)
    // Zero-delay settings: x264 must emit a NAL for every input frame with no
    // internal buffering, otherwise HLS segments never complete.
    param.rc.b_mb_tree       = 0;        // mb-tree lookahead requires buffering many frames — disable it
    param.rc.i_lookahead     = 0;        // No rate-control lookahead
    param.i_sync_lookahead   = 0;        // No threaded lookahead queue
    param.i_threads          = 1;        // Single encode thread — eliminates per-thread frame buffering

    if (x264_param_apply_profile(&param, "baseline") < 0) {
        error("x264_param_apply_profile failed");
        free(enc->yuv_buf);
        free(enc);
        return NULL;
    }

    enc->handle = x264_encoder_open(&param);
    if (!enc->handle) {
        error("x264_encoder_open failed");
        free(enc->yuv_buf);
        free(enc);
        return NULL;
    }

    x264_picture_init(&enc->pic_in);
    enc->pic_in.img.i_csp    = X264_CSP_I420;
    enc->pic_in.img.i_plane  = 3;

    info("H.264 encoder created: %dx%d @ %d fps, %d kbps, keyint=%d",
         width, height, fps, bitrate_kbps, keyframe_interval);
    return enc;
}

void h264_encoder_destroy(h264_encoder_t* enc) {
    if (!enc) return;
    if (enc->handle) x264_encoder_close(enc->handle);
    free(enc->yuv_buf);
    free(enc->nal_buf);
    free(enc);
}

size_t h264_encoder_encode(h264_encoder_t* enc, const uint8_t* bgra,
                           uint8_t** nal_data, int* is_keyframe) {
    if (!enc || !bgra || !nal_data || !is_keyframe) return 0;

    *nal_data   = NULL;
    *is_keyframe = 0;

    // Convert BGRA → YUV 4:2:0
    bgra_to_yuv420(bgra, enc->width, enc->height, enc->yuv_buf);

    // Point x264 picture planes at our YUV buffer
    enc->pic_in.img.plane[0] = enc->yuv_buf;
    enc->pic_in.img.plane[1] = enc->yuv_buf + enc->width * enc->height;
    enc->pic_in.img.plane[2] = enc->yuv_buf + enc->width * enc->height * 5 / 4;
    enc->pic_in.img.i_stride[0] = enc->width;
    enc->pic_in.img.i_stride[1] = enc->width / 2;
    enc->pic_in.img.i_stride[2] = enc->width / 2;
    enc->pic_in.i_pts = enc->frame_num++;

    x264_nal_t* nals    = NULL;
    int         nal_cnt = 0;

    int frame_size = x264_encoder_encode(enc->handle, &nals, &nal_cnt,
                                         &enc->pic_in, &enc->pic_out);
    if (frame_size < 0) {
        error("x264_encoder_encode returned %d", frame_size);
        return 0;
    }
    if (frame_size == 0 || nal_cnt == 0) {
        // Encoder is buffering (shouldn't happen with baseline, but handle it)
        return 0;
    }

    // Concatenate all NAL units into a single flat buffer.
    // x264 Annex B output: each nal->p_payload already contains start codes.
    size_t total = (size_t)frame_size;
    if (total > enc->nal_buf_size) {
        free(enc->nal_buf);
        enc->nal_buf = malloc(total);
        if (!enc->nal_buf) {
            enc->nal_buf_size = 0;
            return 0;
        }
        enc->nal_buf_size = total;
    }

    uint8_t* dst = enc->nal_buf;
    for (int i = 0; i < nal_cnt; i++) {
        memcpy(dst, nals[i].p_payload, (size_t)nals[i].i_payload);
        dst += nals[i].i_payload;
    }

    // IDR slice → keyframe
    *is_keyframe = (enc->pic_out.b_keyframe != 0);
    *nal_data    = enc->nal_buf;
    return total;
}

#endif /* !HAVE_LIBAVCODEC */
