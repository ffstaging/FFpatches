#ifndef AVCODEC_H264_MB_INFO_H
#define AVCODEC_H264_MB_INFO_H

#include <stdint.h>

typedef struct H264MBInfo {
    uint32_t mb_type; // The base macroblock type from H.264 specs

    union {
        // Information for Intra-coded macroblocks
        struct {
            int8_t intra4x4_pred_mode[16];
            uint8_t intra16x16_pred_mode;
            uint8_t chroma_pred_mode;
        } intra;

        // Information for Inter-coded macroblocks
        struct {
            uint8_t sub_mb_type[4]; // Type for each 8x8 partition
            // For each of the 16 4x4 blocks, store ref_idx and MV for L0 and L1
            int8_t  ref_idx[2][16];
            int16_t mv[2][16][2];
        } inter;
    };
} H264MBInfo;

#endif /* AVCODEC_H264_MB_INFO_H */
