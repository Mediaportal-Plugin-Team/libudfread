/*
 * Crafted UDF image builder for tests
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Builds valid UDF discs in memory (and optionally writes them to files)
 * for conformance and API tests. The builder only creates legal
 * structures; broken structures are created by corrupting the image
 * afterwards (img_corrupt_checksum, direct img_set_* calls).
 *
 * Disc layout (fixed):
 *   lba 16..18   Volume Recognition Sequence (BEA01, NSR02, TEA01)
 *   lba 256      Anchor Volume Descriptor Pointer
 *   lba 257..260 Main Volume Descriptor Sequence (PVD, PD, LVD, TD;
 *                one descriptor per block)
 *   lba 261..264 Reserve Volume Descriptor Sequence (same)
 *   part_lba     File Set Descriptor
 *   part_lba + 1 root directory File Entry
 *   part_lba + 2 root directory data
 *   ...          files and subdirectories (img_add_file / img_add_dir)
 *
 * All ICB / extent block addresses are partition-relative. The partition
 * number (Partition Descriptor + type-1 partition map) is set by
 * img_build_disc(); the partition reference number used in long_ad / ICB
 * fields defaults to 0 (the map index, ECMA-167 3/8.8) and can be changed
 * with img_set_ad_part_ref().
 */

#ifndef UDF_IMAGE_H_
#define UDF_IMAGE_H_

#include <stdint.h>

#include "blockinput.h"

#define IMG_BLOCK_SIZE 2048

struct img_dir {
    char     path[1024];  /* full path, "/" for root */
    uint32_t fe_lba;      /* directory File Entry block (absolute) */
    uint32_t data_lba;    /* directory data block (absolute) */
    uint32_t data_len;    /* used bytes in data block */
    uint32_t data_blocks; /* allocated data blocks */
    uint16_t fe_tag;      /* File Entry (261) or Extended File Entry (266) */
    uint32_t data_off;    /* 0 for extent dirs, 176/216 for inline dirs */
};

/* allocation descriptor for img_add_file_ads */
struct img_ad {
    uint32_t len;         /* extent length (bytes) */
    uint32_t lba;         /* partition-relative block address */
    uint8_t  type;        /* 0 normal, 1 not recorded, 2 not allocated */
};

struct udf_image {
    udfread_block_input bi;   /* first member: img_read_cb casts input -> image */
    uint8_t *data;
    uint32_t nblocks;         /* allocated blocks */
    uint32_t used;            /* highest used block + 1 */
    uint32_t part_lba;        /* partition start (absolute lba) */
    uint16_t part_number;     /* partition number (PD + type-1 map) */
    uint16_t ad_part_ref;     /* partition reference in long_ad/ICB fields */
    uint32_t vds_seq;         /* Volume Descriptor Sequence Number (tag @16) */
    uint32_t pd_lba[2];       /* Partition Descriptor blocks from img_build_disc();
                               * num_blocks patched to image size in img_write_file() */
    uint32_t num_pd;
    uint32_t read_calls;
    uint32_t last_lba;
    int      record_reads;
    struct img_dir *dirs;
    uint32_t num_dirs;
    uint32_t max_dirs;
};

/* core */
struct udf_image *img_new(void);
void img_free(struct udf_image *img);
uint8_t *img_block(struct udf_image *img, uint32_t lba);   /* grow if needed */
void img_set_u8(struct udf_image *img, uint32_t lba, uint32_t off, uint8_t v);
void img_set_u16(struct udf_image *img, uint32_t lba, uint32_t off, uint32_t v);
void img_set_u32(struct udf_image *img, uint32_t lba, uint32_t off, uint32_t v);
void img_set_u64(struct udf_image *img, uint32_t lba, uint32_t off, uint64_t v);
void img_write_tag_at_serial(struct udf_image *img, uint32_t lba, uint32_t off,
                             uint16_t tag_id, uint16_t serial);
void img_write_tag_at(struct udf_image *img, uint32_t lba, uint32_t off, uint16_t tag_id);
void img_write_tag(struct udf_image *img, uint32_t lba, uint16_t tag_id);
void img_corrupt_checksum(struct udf_image *img, uint32_t lba);

/* descriptor writers (descriptor at the start of the block) */
void img_set_vds_seq(struct udf_image *img, uint32_t seq);
void img_write_pvd(struct udf_image *img, uint32_t lba);
void img_write_pd(struct udf_image *img, uint32_t lba, uint16_t part_number);
void img_write_lvd(struct udf_image *img, uint32_t lba, uint16_t part_ref, uint16_t serial);
void img_write_td(struct udf_image *img, uint32_t lba);
void img_write_vdp(struct udf_image *img, uint32_t lba, uint32_t next_lba, uint32_t next_len);
void img_write_avdp(struct udf_image *img, uint32_t lba, uint32_t mvds_lba, uint32_t mvds_len,
                    uint32_t rvds_lba, uint32_t rvds_len);
void img_write_fsd(struct udf_image *img, uint32_t lba);
void img_write_fe(struct udf_image *img, uint32_t lba, uint16_t tag, uint8_t file_type,
                  uint16_t icb_flags, uint32_t l_ad, uint64_t length,
                  uint32_t ad_len, uint32_t ad_lba, uint16_t serial);
void img_write_fid_at(struct udf_image *img, uint32_t lba, uint32_t off, uint8_t characteristic,
                      uint8_t encoding, const uint8_t *name, uint32_t name_len,
                      uint32_t icb_lba, uint16_t serial);

/* block input (pass &img->bi to udfread_open_input) */
int img_read_cb(udfread_block_input *input, uint32_t lba, void *buf, uint32_t nblocks, int flags);
uint32_t img_size_cb(udfread_block_input *input);
void img_init_input(struct udf_image *img);
void img_record_reads(struct udf_image *img, int on);

/* disc layout */
void img_build_disc(struct udf_image *img, uint16_t part_number, uint32_t part_lba);
void img_set_ad_part_ref(struct udf_image *img, uint16_t ref);
void img_add_file(struct udf_image *img, const char *path, const uint8_t *content, uint32_t len);
void img_add_dir(struct udf_image *img, const char *path);
void img_add_file_serial(struct udf_image *img, const char *path,
                         const uint8_t *content, uint32_t len, uint16_t serial);
void img_add_dir_serial(struct udf_image *img, const char *path, uint16_t serial);
void img_add_file_efe(struct udf_image *img, const char *path,
                      const uint8_t *content, uint32_t len);
void img_add_dir_efe(struct udf_image *img, const char *path);
void img_add_inline_dir(struct udf_image *img, const char *path);
void img_add_file_raw(struct udf_image *img, const char *parent_path, uint8_t encoding,
                      const uint8_t *name, uint32_t name_len,
                      const uint8_t *content, uint32_t len);
void img_add_dir_raw(struct udf_image *img, const char *parent_path, uint8_t encoding,
                     const uint8_t *name, uint32_t name_len);
void img_add_file_ads(struct udf_image *img, const char *path, uint64_t length,
                      const struct img_ad *ads, uint32_t num_ads);

/* file output (for independent verification with other implementations) */
int img_write_file(struct udf_image *img, const char *path);

#endif /* UDF_IMAGE_H_ */

