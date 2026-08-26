/*
 * Shared helpers for ecma167/udf_volume unit tests
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef TEST_ECMA_H_
#define TEST_ECMA_H_

#include "test_util.h"

#include "ecma167.h"
#include "logger.h"
#include "udfread.h"   /* UDFREAD_LOG_TRACE */

/* capture-log + ecma_ctx setup.
 * Level TRACE: captures errors, informational messages and traces. */
static inline void log_init(struct capture_log *cap, udf_log *log, ecma_ctx *ctx)
{
    memset(cap, 0, sizeof(*cap));
    memset(log, 0, sizeof(*log));
    log->ctx = cap;
    log->logger = capture_logger;
    log->level = UDFREAD_LOG_TRACE;
    ctx->lc = log;
}

/* write a 16-byte long allocation descriptor (extent type 1,
 * "not recorded but allocated", ECMA-167 4/14.14.2).  Deliberately
 * non-conformant when used for ICB pointers (those use extent type 0);
 * exercises best-effort parsing of minimal fixtures. */
static inline void set_long_ad(uint8_t *p, uint32_t length, uint32_t lba, uint16_t partition)
{
    set_u32(p + 0, 0x40000000 | length);
    set_u32(p + 4, lba);
    set_u16(p + 8, partition);
}

/* descriptor block builders (each writes a full 2048-byte block) */

static inline void make_vds_block(uint8_t *b, uint16_t tag_id)
{
    memset(b, 0, UDF_BLOCK_SIZE);
    set_tag(b, tag_id);
}

static inline void make_vds_block_seq(uint8_t *b, uint16_t tag_id, uint32_t seq)
{
    make_vds_block(b, tag_id);
    set_u32(b + 16, seq);
}

static inline void make_avdp(uint8_t *b, uint32_t mvds_lba, uint32_t mvds_len,
                             uint32_t rvds_lba, uint32_t rvds_len)
{
    make_vds_block(b, ECMA_AnchorVolumeDescriptorPointer);
    set_u32(b + 16, mvds_len);
    set_u32(b + 20, mvds_lba);
    set_u32(b + 24, rvds_len);
    set_u32(b + 28, rvds_lba);
}

static inline void make_vdp_block(uint8_t *b, uint32_t lba, uint32_t len)
{
    make_vds_block(b, ECMA_VolumeDescriptorPointer);
    set_u32(b + 20, len);
    set_u32(b + 24, lba);
}

static inline void make_partition_block(uint8_t *b, uint16_t number,
                                        uint32_t start_block, uint32_t num_blocks)
{
    make_vds_block(b, ECMA_PartitionDescriptor);
    set_u16(b + 22, number);
    set_u32(b + 188, start_block);
    set_u32(b + 192, num_blocks);
}

static inline void make_lvd_block(uint8_t *b, uint32_t block_size)
{
    make_vds_block(b, ECMA_LogicalVolumeDescriptor);
    set_u32(b + 212, block_size);
}

/* extended file entry with one short allocation descriptor (or inline content) */
static inline void make_ext_file_entry(uint8_t *b, uint8_t file_type, uint16_t icb_flags,
                                       uint32_t l_ad, uint64_t length,
                                       uint32_t ad_len, uint32_t ad_lba)
{
    make_vds_block(b, ECMA_ExtendedFileEntry);
    set_u16(b + 20, 4);            /* strategy type */
    b[27] = file_type;
    set_u16(b + 34, icb_flags);
    set_u64(b + 56, length);
    set_u32(b + 208, 0);           /* l_ea */
    set_u32(b + 212, l_ad);
    if ((icb_flags & 7) == 3) {
        memcpy(b + 216, "abcd", 4);   /* inline content */
    } else {
        set_u32(b + 216, 0x40000000 | ad_len);  /* extent length */
        set_u32(b + 220, ad_lba);
    }
}

/* file set descriptor with root icb */
static inline void make_fsd_block(uint8_t *b, uint32_t root_icb_lba, uint16_t root_icb_partition)
{
    make_vds_block(b, ECMA_FileSetDescriptor);
    set_long_ad(b + 400, 0, root_icb_lba, root_icb_partition);
}

/* file entry with one short allocation descriptor (or inline content).
 * The short ad is written with a plain length (extent type 0, normal). */
static inline void make_file_entry(uint8_t *b, uint8_t file_type, uint16_t icb_flags,
                                   uint32_t l_ad, uint64_t length,
                                   uint32_t ad_len, uint32_t ad_lba)
{
    make_vds_block(b, ECMA_FileEntry);
    set_u16(b + 20, 4);            /* strategy type */
    b[27] = file_type;
    set_u16(b + 34, icb_flags);
    set_u64(b + 56, length);
    set_u32(b + 168, 0);           /* l_ea */
    set_u32(b + 172, l_ad);
    if ((icb_flags & 7) != 3) {
        set_u32(b + 176, ad_len);  /* extent length (extent type 0) */
        set_u32(b + 180, ad_lba);
    }
    /* inline content (flags & 7) == 3: caller writes at b + 176 */
}

/* file entry with extended attribute area (ECMA 167, 4/14.9):
 * allocation descriptors start at 176 + l_ea */
static inline void make_file_entry_ea(uint8_t *b, uint8_t file_type, uint16_t icb_flags,
                                      uint32_t l_ea, uint32_t l_ad, uint64_t length,
                                      uint32_t ad_len, uint32_t ad_lba)
{
    make_vds_block(b, ECMA_FileEntry);
    set_u16(b + 20, 4);            /* strategy type */
    b[27] = file_type;
    set_u16(b + 34, icb_flags);
    set_u64(b + 56, length);
    set_u32(b + 168, l_ea);
    set_u32(b + 172, l_ad);
    if ((icb_flags & 7) != 3) {
        set_u32(b + 176 + l_ea, ad_len);   /* extent length (extent type 0) */
        set_u32(b + 180 + l_ea, ad_lba);
    }
}

/* extended file entry with extended attribute area (ECMA 167, 4/14.17):
 * allocation descriptors start at 216 + l_ea */
static inline void make_ext_file_entry_ea(uint8_t *b, uint8_t file_type, uint16_t icb_flags,
                                          uint32_t l_ea, uint32_t l_ad, uint64_t length,
                                          uint32_t ad_len, uint32_t ad_lba)
{
    make_vds_block(b, ECMA_ExtendedFileEntry);
    set_u16(b + 20, 4);            /* strategy type */
    b[27] = file_type;
    set_u16(b + 34, icb_flags);
    set_u64(b + 56, length);
    set_u32(b + 208, l_ea);
    set_u32(b + 212, l_ad);
    if ((icb_flags & 7) != 3) {
        set_u32(b + 216 + l_ea, ad_len);   /* extent length (extent type 0) */
        set_u32(b + 220 + l_ea, ad_lba);
    }
}

/* file identifier with implementation use area (UDF 2.3.4.3/2.3.4.4):
 * filename is written at 38 + l_iu. Returns the dword-padded length. */
static inline uint32_t make_fid_iu_at(uint8_t *p, uint8_t characteristic,
                                      const char *filename, uint32_t icb_lba, uint32_t icb_len,
                                      const uint8_t *iu, uint16_t iu_len)
{
    size_t len = strlen(filename);

    set_tag(p, ECMA_FileIdentifierDescriptor);
    p[18] = characteristic;
    p[19] = (uint8_t)(1 + len);    /* filename_len includes the cs0 encoding byte */
    set_long_ad(p + 20, icb_len, icb_lba, 0);
    set_u16(p + 36, iu_len);
    if (iu_len) {
        memcpy(p + 38, iu, iu_len);
    }
    p[38 + iu_len] = 8;            /* cs0 encoding: 8-bit */
    memcpy(p + 39 + iu_len, filename, len);
    return (uint32_t)(4 * (((size_t)38 + iu_len + 1 + len + 3) / 4));
}

/* spec-conformant parent FID (UDF 2.3.4.6): no filename
 * (filename_len = 0), PARENT|DIR characteristics. Returns padded length. */
static inline uint32_t make_parent_fid_at(uint8_t *p, uint32_t icb_lba)
{
    set_tag(p, ECMA_FileIdentifierDescriptor);
    p[18] = CHAR_FLAG_PARENT | CHAR_FLAG_DIR;
    p[19] = 0;                     /* filename_len = 0 */
    set_long_ad(p + 20, 0, icb_lba, 0);
    set_u16(p + 36, 0);            /* l_iu */
    return 40;                     /* 4 * ((38 + 0 + 3) / 4) */
}

/* file identifier descriptor at an arbitrary offset (no memset; for inline
 * directories). Returns the dword-padded length used.
 * File Version Number (bytes 16..17) is left 0; UDF 2.3.4.1 requires 1.
 * For a conformant parent FID use make_parent_fid_at(). */
static inline uint32_t make_fid_at(uint8_t *p, uint8_t characteristic,
                                   const char *filename, uint32_t icb_lba, uint32_t icb_len)
{
    size_t len = strlen(filename);

    set_tag(p, ECMA_FileIdentifierDescriptor);
    p[18] = characteristic;
    p[19] = (uint8_t)(1 + len);    /* filename_len includes the cs0 encoding byte */
    set_long_ad(p + 20, icb_len, icb_lba, 0);
    set_u16(p + 36, 0);            /* l_iu */
    p[38] = 8;                     /* cs0 encoding: 8-bit */
    memcpy(p + 39, filename, len);
    return (uint32_t)(4 * ((38 + 1 + len + 3) / 4));
}

/* file identifier descriptor in a full block */
static inline uint32_t make_fid(uint8_t *b, uint8_t characteristic,
                                const char *filename, uint32_t icb_lba, uint32_t icb_len)
{
    make_vds_block(b, ECMA_FileIdentifierDescriptor);
    return make_fid_at(b, characteristic, filename, icb_lba, icb_len);
}

#endif /* TEST_ECMA_H_ */

