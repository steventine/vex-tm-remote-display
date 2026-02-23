#ifndef HAVE_LIBAVFORMAT

// Minimal MPEG-TS muxer for H.264 baseline / HLS.
//
// Packet layout (ISO 13818-1):
//   [0x47][TEI|PUSI|TP|PID_hi][PID_lo][CC|adaptation][payload...]
//   188 bytes total; last packet in a PES is padded with adaptation stuffing.
//
// PIDs used:
//   0x0000  PAT   (Program Association Table)
//   0x0100  PMT   (Program Map Table)
//   0x0101  Video (H.264 elementary stream)
//
// Every segment starts with PAT + PMT packets so HLS clients can start
// decoding at any segment boundary.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ts_muxer.h"
#include "simple-log.h"

// --------------------------------------------------------------------------
// Constants
// --------------------------------------------------------------------------
#define TS_PACKET_SIZE   188
#define TS_SYNC_BYTE     0x47

#define PID_PAT          0x0000
#define PID_PMT          0x0100
#define PID_VIDEO        0x0101
#define PROGRAM_NUM      1
#define STREAM_TYPE_H264 0x1B   // H.264 / AVC

// Initial segment buffer size; grown with realloc as needed.
#define SEG_INITIAL_SIZE (256 * 1024)

// --------------------------------------------------------------------------
// Internal state
// --------------------------------------------------------------------------
struct ts_muxer {
    // Continuity counters (4-bit, wraps at 16)
    uint8_t cc_pat;
    uint8_t cc_pmt;
    uint8_t cc_video;

    // In-progress segment buffer
    uint8_t* seg_buf;
    size_t   seg_len;
    size_t   seg_cap;
};

// --------------------------------------------------------------------------
// CRC-32 (MPEG-2 / DVB polynomial 0x04C11DB7, MSB-first)
// --------------------------------------------------------------------------
static uint32_t crc32_table[256];
static int      crc32_ready = 0;

static void crc32_init(void) {
    if (crc32_ready) return;
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i << 24;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : (crc << 1);
        crc32_table[i] = crc;
    }
    crc32_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++)
        crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ data[i]) & 0xFF];
    return crc;
}

static uint32_t crc32_buf(const uint8_t* data, size_t len) {
    crc32_init();
    return crc32_update(0xFFFFFFFFu, data, len);
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------
static int seg_reserve(struct ts_muxer* mux, size_t extra) {
    if (mux->seg_len + extra <= mux->seg_cap) return 0;
    size_t new_cap = mux->seg_cap * 2;
    if (new_cap < mux->seg_len + extra) new_cap = mux->seg_len + extra + SEG_INITIAL_SIZE;
    uint8_t* p = realloc(mux->seg_buf, new_cap);
    if (!p) return -1;
    mux->seg_buf = p;
    mux->seg_cap = new_cap;
    return 0;
}

static int seg_append(struct ts_muxer* mux, const uint8_t* data, size_t len) {
    if (seg_reserve(mux, len) < 0) return -1;
    memcpy(mux->seg_buf + mux->seg_len, data, len);
    mux->seg_len += len;
    return 0;
}

// Emit one 188-byte TS packet for PID `pid`.
// payload_len must be in [1, 184].
// If payload_len < 184, the gap is filled with an adaptation-field stuffing header
// so the packet is always exactly 188 bytes.
// cc is incremented on every call.
static int emit_ts_packet(struct ts_muxer* mux, uint16_t pid, uint8_t* cc,
                          int pusi, const uint8_t* payload, int payload_len) {
    uint8_t pkt[TS_PACKET_SIZE];
    memset(pkt, 0xFF, sizeof(pkt)); // 0xFF is the standard stuffing byte

    pkt[0] = TS_SYNC_BYTE;
    pkt[1] = (uint8_t)((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
    pkt[2] = (uint8_t)(pid & 0xFF);

    if (payload_len == 184) {
        // Full packet — payload only, no adaptation field needed.
        pkt[3] = (uint8_t)(0x10 | (*cc & 0x0F));
        memcpy(pkt + 4, payload, 184);
    } else {
        // Short payload — use an adaptation field to pad to 188 bytes.
        // Layout: 4-byte header | 1-byte af_length | af_length bytes of af data | payload
        // Constraint: 4 + 1 + af_length + payload_len = 188  =>  af_length = 183 - payload_len
        int af_len = 183 - payload_len; // bytes in adaptation field after the length byte
        pkt[3] = (uint8_t)(0x30 | (*cc & 0x0F)); // adaptation_field_control = 11
        pkt[4] = (uint8_t)af_len;
        if (af_len > 0) {
            pkt[5] = 0x00; // adaptation field flags: nothing set (no PCR, no splice, etc.)
            // pkt[6 .. 4+af_len] are already 0xFF stuffing bytes from memset
        }
        memcpy(pkt + 4 + 1 + af_len, payload, (size_t)payload_len);
    }

    *cc = (*cc + 1) & 0x0F;
    return seg_append(mux, pkt, TS_PACKET_SIZE);
}

// --------------------------------------------------------------------------
// PSI tables
// --------------------------------------------------------------------------

// Build and write a PAT packet.
static int write_pat(struct ts_muxer* mux) {
    // PAT section:
    // table_id=0x00, section_syntax=1, section_length=13
    // transport_stream_id=1, version=0, current=1, section_number=0, last_section=0
    // program_num=1, PMT_PID=0x0100
    // CRC32
    uint8_t section[12];
    section[0]  = 0x00;         // table_id: PAT
    section[1]  = 0xB0;         // section_syntax_indicator=1, '0', reserved=11, section_length hi (0)
    section[2]  = 0x0D;         // section_length lo: 13 (= 4 fixed + 4 program entry + 4 CRC)
    section[3]  = 0x00;         // transport_stream_id hi
    section[4]  = 0x01;         // transport_stream_id lo
    section[5]  = 0xC1;         // reserved=11, version=0, current_next=1
    section[6]  = 0x00;         // section_number
    section[7]  = 0x00;         // last_section_number
    // Program entry: program_number=1, PMT_PID=0x0100
    section[8]  = 0x00;         // program_num hi
    section[9]  = PROGRAM_NUM;  // program_num lo
    section[10] = 0xE1;         // reserved=111, PMT_PID hi
    section[11] = 0x00;         // PMT_PID lo

    uint32_t crc = crc32_buf(section, 12);
    uint8_t payload[17];
    payload[0] = 0x00; // pointer_field
    memcpy(payload + 1, section, 12);
    payload[13] = (uint8_t)(crc >> 24);
    payload[14] = (uint8_t)(crc >> 16);
    payload[15] = (uint8_t)(crc >>  8);
    payload[16] = (uint8_t)(crc);

    return emit_ts_packet(mux, PID_PAT, &mux->cc_pat, 1, payload, 17);
}

// Build and write a PMT packet.
static int write_pmt(struct ts_muxer* mux) {
    // PMT section:
    // table_id=0x02, section_length=18
    // program_number=1, PCR_PID=0x0101
    // elementary stream: stream_type=H264, PID=0x0101, ES_info_length=0
    uint8_t section[17];
    section[0]  = 0x02;         // table_id: PMT
    section[1]  = 0xB0;         // section_syntax=1, reserved, section_length hi
    section[2]  = 0x12;         // section_length lo = 18 (4 fixed + 4 PCR + 5 ES + 4 CRC - accounting below)
    // Actually: section_length = bytes after the 3-byte header (table_id + section_length field)
    // section_length = 2 (prog num) + 1 (version) + 1 (sect num) + 1 (last sect) +
    //                  2 (PCR PID) + 2 (prog info len) + 5 (ES) + 4 (CRC) = 18
    section[3]  = 0x00;         // program_number hi
    section[4]  = PROGRAM_NUM;  // program_number lo
    section[5]  = 0xC1;         // reserved, version=0, current=1
    section[6]  = 0x00;         // section_number
    section[7]  = 0x00;         // last_section_number
    section[8]  = 0xE1;         // reserved, PCR_PID hi
    section[9]  = 0x01;         // PCR_PID lo (0x0101)
    section[10] = 0xF0;         // reserved, program_info_length hi
    section[11] = 0x00;         // program_info_length lo = 0
    // ES entry
    section[12] = STREAM_TYPE_H264;
    section[13] = 0xE1;         // reserved, elementary_PID hi
    section[14] = 0x01;         // elementary_PID lo (0x0101)
    section[15] = 0xF0;         // reserved, ES_info_length hi
    section[16] = 0x00;         // ES_info_length lo = 0

    uint32_t crc = crc32_buf(section, 17);
    uint8_t payload[22];
    payload[0] = 0x00; // pointer_field
    memcpy(payload + 1, section, 17);
    payload[18] = (uint8_t)(crc >> 24);
    payload[19] = (uint8_t)(crc >> 16);
    payload[20] = (uint8_t)(crc >>  8);
    payload[21] = (uint8_t)(crc);

    return emit_ts_packet(mux, PID_PMT, &mux->cc_pmt, 1, payload, 22);
}

// --------------------------------------------------------------------------
// PES packetisation
// --------------------------------------------------------------------------

// Write PES header + NAL data as a sequence of TS packets.
// First packet has PUSI=1 and the 14-byte PES header; subsequent packets are
// plain continuations.  emit_ts_packet handles stuffing for the last packet.
static int write_pes(struct ts_muxer* mux,
                     const uint8_t* nal_data, size_t nal_size,
                     int64_t pts_90khz, int is_keyframe) {
    (void)is_keyframe; // no longer needed — random_access_indicator removed

    // Build 14-byte PES header (PTS only, no DTS — correct for baseline H.264)
    uint8_t pes_hdr[14];
    pes_hdr[0] = 0x00; pes_hdr[1] = 0x00; pes_hdr[2] = 0x01; // packet_start_code_prefix
    pes_hdr[3] = 0xE0;  // stream_id: video
    pes_hdr[4] = 0x00;  // PES_packet_length = 0 (unbounded, required for video)
    pes_hdr[5] = 0x00;
    pes_hdr[6] = 0x80;  // '10' marker, no flags
    pes_hdr[7] = 0x80;  // PTS_DTS_flags = 10 (PTS only)
    pes_hdr[8] = 0x05;  // PES_header_data_length = 5

    int64_t pts = pts_90khz & 0x1FFFFFFFFll;
    pes_hdr[9]  = (uint8_t)(0x21 | ((pts >> 29) & 0x0E));
    pes_hdr[10] = (uint8_t)((pts >> 22) & 0xFF);
    pes_hdr[11] = (uint8_t)(0x01 | ((pts >> 14) & 0xFE));
    pes_hdr[12] = (uint8_t)((pts >>  7) & 0xFF);
    pes_hdr[13] = (uint8_t)(0x01 | ((pts <<  1) & 0xFE));

    const uint8_t* src    = nal_data;
    size_t         remain = nal_size;

    // --- First TS packet: PUSI=1, PES header + first chunk of NAL ---
    {
        uint8_t buf[184];
        memcpy(buf, pes_hdr, 14);
        int take = (int)remain < 170 ? (int)remain : 170; // 184 - 14 = 170
        memcpy(buf + 14, src, (size_t)take);
        src    += take;
        remain -= (size_t)take;
        if (emit_ts_packet(mux, PID_VIDEO, &mux->cc_video, 1, buf, 14 + take) < 0)
            return -1;
    }

    // --- Continuation packets: PUSI=0 ---
    while (remain > 0) {
        int take = (int)remain < 184 ? (int)remain : 184;
        if (emit_ts_packet(mux, PID_VIDEO, &mux->cc_video, 0, src, take) < 0)
            return -1;
        src    += take;
        remain -= (size_t)take;
    }

    return 0;
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

ts_muxer_t* ts_muxer_create(void) {
    crc32_init();
    ts_muxer_t* mux = calloc(1, sizeof(ts_muxer_t));
    if (!mux) return NULL;

    mux->seg_buf = malloc(SEG_INITIAL_SIZE);
    if (!mux->seg_buf) { free(mux); return NULL; }
    mux->seg_cap = SEG_INITIAL_SIZE;
    mux->seg_len = 0;
    return mux;
}

void ts_muxer_destroy(ts_muxer_t* mux) {
    if (!mux) return;
    free(mux->seg_buf);
    free(mux);
}

int ts_muxer_write_nal(ts_muxer_t* mux,
                       const uint8_t* nal_data, size_t nal_size,
                       int64_t pts_90khz, int is_keyframe,
                       uint8_t** seg_out, size_t* seg_size) {
    if (!mux || !nal_data || !seg_out || !seg_size) return -1;

    *seg_out  = NULL;
    *seg_size = 0;

    if (is_keyframe && mux->seg_len > 0) {
        // Finalise the current segment and hand it to the caller.
        *seg_out  = mux->seg_buf;
        *seg_size = mux->seg_len;

        // Allocate a fresh buffer for the next segment
        mux->seg_buf = malloc(SEG_INITIAL_SIZE);
        if (!mux->seg_buf) {
            mux->seg_cap = 0;
            mux->seg_len = 0;
            error("ts_muxer: failed to allocate new segment buffer");
            return -1;
        }
        mux->seg_cap = SEG_INITIAL_SIZE;
        mux->seg_len = 0;
    }

    // Every new segment (or very first) starts with PAT + PMT
    if (mux->seg_len == 0) {
        if (write_pat(mux) < 0) return -1;
        if (write_pmt(mux) < 0) return -1;
    }

    // Write PES-wrapped NAL data
    return write_pes(mux, nal_data, nal_size, pts_90khz, is_keyframe);
}

int ts_muxer_flush(ts_muxer_t* mux, uint8_t** seg_out, size_t* seg_size) {
    if (!mux || !seg_out || !seg_size) return -1;
    *seg_out  = NULL;
    *seg_size = 0;
    if (mux->seg_len == 0) return 0;

    *seg_out  = mux->seg_buf;
    *seg_size = mux->seg_len;

    mux->seg_buf = malloc(SEG_INITIAL_SIZE);
    if (!mux->seg_buf) {
        mux->seg_cap = 0;
        mux->seg_len = 0;
    } else {
        mux->seg_cap = SEG_INITIAL_SIZE;
        mux->seg_len = 0;
    }
    return 0;
}

void ts_muxer_free_segment(uint8_t* seg) {
    free(seg);
}

#endif /* !HAVE_LIBAVFORMAT */
