/*
 * Crafted UDF image builder for tests
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * See udf_image.h for the disc layout and usage.
 */

#include "udf_image.h"

#include "ecma167.h"  /* descriptor tags, file types, CHAR_FLAG_* */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * core
 */

struct udf_image *img_new(void)
{
    return (struct udf_image *)calloc(1, sizeof(struct udf_image));
}

void img_free(struct udf_image *img)
{
    if (img) {
        free(img->data);
        free(img->dirs);
        free(img);
    }
}

/* keep Partition Descriptor num_blocks in sync with image growth.
 * Writes bytes directly: img_set_u32() would recurse via img_block(). */
static void _img_patch_pd(struct udf_image *img)
{
    uint32_t i;
    uint32_t v = img->used - img->part_lba;

    for (i = 0; i < img->num_pd; i++) {
        uint8_t *p = img->data + (size_t)img->pd_lba[i] * IMG_BLOCK_SIZE + 192;
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
    }
}

uint8_t *img_block(struct udf_image *img, uint32_t lba)
{
    if (lba >= img->nblocks) {
        uint32_t n = img->nblocks ? img->nblocks : 64;
        while (n <= lba) {
            n *= 2;
        }
        img->data = (uint8_t *)realloc(img->data, (size_t)n * IMG_BLOCK_SIZE);
        memset(img->data + (size_t)img->nblocks * IMG_BLOCK_SIZE, 0,
               (size_t)(n - img->nblocks) * IMG_BLOCK_SIZE);
        img->nblocks = n;
    }
    if (lba >= img->used) {
        img->used = lba + 1;
        if (img->num_pd) {
            _img_patch_pd(img);
        }
    }
    return img->data + (size_t)lba * IMG_BLOCK_SIZE;
}

void img_set_u8(struct udf_image *img, uint32_t lba, uint32_t off, uint8_t v)
{
    img_block(img, lba)[off] = v;
}

void img_set_u16(struct udf_image *img, uint32_t lba, uint32_t off, uint32_t v)
{
    uint8_t *p = img_block(img, lba) + off;
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void img_set_u32(struct udf_image *img, uint32_t lba, uint32_t off, uint32_t v)
{
    uint8_t *p = img_block(img, lba) + off;
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void img_set_u64(struct udf_image *img, uint32_t lba, uint32_t off, uint64_t v)
{
    uint8_t *p = img_block(img, lba) + off;
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 32);
    p[5] = (uint8_t)(v >> 40);
    p[6] = (uint8_t)(v >> 48);
    p[7] = (uint8_t)(v >> 56);
}

void img_write_tag_at_serial(struct udf_image *img, uint32_t lba, uint32_t off,
                             uint16_t tag_id, uint16_t serial)
{
    uint8_t *p = img_block(img, lba) + off;
    uint8_t sum = 0;
    int i;

    memset(p, 0, 16);
    p[0] = (uint8_t)tag_id;
    p[1] = (uint8_t)(tag_id >> 8);
    p[2] = 3;   /* descriptor version number */
    p[6] = (uint8_t)serial;
    p[7] = (uint8_t)(serial >> 8);
    for (i = 0; i < 4; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    for (i = 5; i < 16; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    p[4] = sum;
}

void img_write_tag_at(struct udf_image *img, uint32_t lba, uint32_t off, uint16_t tag_id)
{
    img_write_tag_at_serial(img, lba, off, tag_id, 0);
}

void img_write_tag(struct udf_image *img, uint32_t lba, uint16_t tag_id)
{
    img_write_tag_at(img, lba, 0, tag_id);
}

void img_corrupt_checksum(struct udf_image *img, uint32_t lba)
{
    img_block(img, lba)[4] ^= 0xff;
}

/* copy len bytes starting at lba+off, crossing block boundaries */
static void _img_write_buf(struct udf_image *img, uint32_t lba, uint32_t off,
                           const uint8_t *buf, uint32_t len)
{
    while (len) {
        uint32_t chunk = IMG_BLOCK_SIZE - off;
        if (chunk > len) {
            chunk = len;
        }
        memcpy(img_block(img, lba) + off, buf, chunk);
        buf += chunk;
        len -= chunk;
        lba++;
        off = 0;
    }
}

/*
 * block input
 */

int img_read_cb(udfread_block_input *input, uint32_t lba, void *buf, uint32_t nblocks, int flags)
{
    struct udf_image *img = (struct udf_image *)input;

    (void)flags;

    if (img->record_reads) {
        img->read_calls++;
        img->last_lba = lba;
    }
    if (lba > img->used || nblocks > img->used - lba) {
        return 0;
    }
    memcpy(buf, img->data + (size_t)lba * IMG_BLOCK_SIZE, (size_t)nblocks * IMG_BLOCK_SIZE);
    return (int)nblocks;
}

uint32_t img_size_cb(udfread_block_input *input)
{
    struct udf_image *img = (struct udf_image *)input;
    return img->used;
}

void img_init_input(struct udf_image *img)
{
    img->bi.close = NULL;
    img->bi.read = img_read_cb;
    img->bi.size = img_size_cb;
}

void img_record_reads(struct udf_image *img, int on)
{
    img->record_reads = on;
    img->read_calls = 0;
    img->last_lba = 0;
}

/*
 * descriptor writers
 */

/* Volume Descriptor Sequence Number written into the descriptor tag
 * (ECMA 167 3/7.2.2, field @16) of the next VDS descriptors */
void img_set_vds_seq(struct udf_image *img, uint32_t seq)
{
    img->vds_seq = seq;
}

static void _img_write_pvd(struct udf_image *img, uint32_t lba, uint32_t off)
{
    static const char vol_id[] = "CONFORMANCE";
    uint8_t *p;

    img_write_tag_at(img, lba, off, ECMA_PrimaryVolumeDescriptor);
    img_set_u32(img, lba, off + 16, img->vds_seq);

    /* Volume Identifier: CS0-8 dstring (UDF 2.1.1), 32-byte field */
    p = img_block(img, lba) + off + 24;
    p[0] = 8;  /* CS0 encoding */
    memcpy(p + 1, vol_id, sizeof(vol_id) - 1);
    img_set_u8(img, lba, off + 24 + 31, (uint8_t)(1 + sizeof(vol_id) - 1));

    /* Volume Set Identifier at +72: zeroed (128 bytes) */
}

void img_write_pvd(struct udf_image *img, uint32_t lba)
{
    _img_write_pvd(img, lba, 0);
}

static void _img_write_pd(struct udf_image *img, uint32_t lba, uint32_t off,
                          uint16_t part_number)
{
    img_write_tag_at(img, lba, off, ECMA_PartitionDescriptor);
    img_set_u32(img, lba, off + 16, img->vds_seq);
    img_set_u16(img, lba, off + 22, part_number);
    img_set_u32(img, lba, off + 188, img->part_lba);
    img_set_u32(img, lba, off + 192, 100);  /* num_blocks */
}

void img_write_pd(struct udf_image *img, uint32_t lba, uint16_t part_number)
{
    _img_write_pd(img, lba, 0, part_number);
}

static void _img_write_lvd(struct udf_image *img, uint32_t lba, uint32_t off,
                           uint16_t part_ref, uint16_t serial)
{
    static const char domain_id[] = "*OSTA UDF Compliant";

    img_write_tag_at_serial(img, lba, off, ECMA_LogicalVolumeDescriptor, serial);
    img_set_u32(img, lba, off + 16, img->vds_seq);
    img_set_u32(img, lba, off + 212, 2048);  /* block size */

    /* Domain Identifier at +216: identifier at +217 */
    memcpy(img_block(img, lba) + off + 217, domain_id, sizeof(domain_id) - 1);

    /* Contents Use at +248: FSD location {length 2048, lba 0, partition X} */
    img_set_u32(img, lba, off + 248, 2048);
    img_set_u32(img, lba, off + 252, 0);
    img_set_u16(img, lba, off + 256, part_ref);

    /* Partition Map Table: one type-1 map */
    img_set_u32(img, lba, off + 264, 6);  /* partition_map_table_length */
    img_set_u32(img, lba, off + 268, 1);  /* num_partition_maps */
    img_set_u8(img, lba, off + 440, 1);   /* type 1 */
    img_set_u8(img, lba, off + 441, 6);   /* length */
    img_set_u16(img, lba, off + 444, img->part_number);
}

void img_write_lvd(struct udf_image *img, uint32_t lba, uint16_t part_ref, uint16_t serial)
{
    _img_write_lvd(img, lba, 0, part_ref, serial);
}

void img_write_td(struct udf_image *img, uint32_t lba)
{
    img_write_tag(img, lba, ECMA_TerminatingDescriptor);
}

void img_write_vdp(struct udf_image *img, uint32_t lba, uint32_t next_lba, uint32_t next_len)
{
    img_write_tag(img, lba, ECMA_VolumeDescriptorPointer);
    img_set_u32(img, lba, 16, img->vds_seq);   /* volume_descriptor_sequence_number */
    img_set_u32(img, lba, 20, next_len);
    img_set_u32(img, lba, 24, next_lba);
}

void img_write_avdp(struct udf_image *img, uint32_t lba, uint32_t mvds_lba, uint32_t mvds_len,
                    uint32_t rvds_lba, uint32_t rvds_len)
{
    img_write_tag(img, lba, ECMA_AnchorVolumeDescriptorPointer);
    img_set_u32(img, lba, 16, mvds_len);
    img_set_u32(img, lba, 20, mvds_lba);
    img_set_u32(img, lba, 24, rvds_len);
    img_set_u32(img, lba, 28, rvds_lba);
}

void img_write_fsd(struct udf_image *img, uint32_t lba)
{
    img_write_tag(img, lba, ECMA_FileSetDescriptor);

    /* root ICB at +400: {length 2048, lba 1, partition X} */
    img_set_u32(img, lba, 400, 2048);
    img_set_u32(img, lba, 404, 1);
    img_set_u16(img, lba, 408, img->ad_part_ref);
}

/* File Entry (tag 261) or Extended File Entry (tag 266); ad_lba is
 * partition-relative. Inline files (icb_flags & 7 == 3) have their content
 * written by the caller at +176 / +216. */
void img_write_fe(struct udf_image *img, uint32_t lba, uint16_t tag, uint8_t file_type,
                  uint16_t icb_flags, uint32_t l_ad, uint64_t length,
                  uint32_t ad_len, uint32_t ad_lba, uint16_t serial)
{
    uint32_t ea_off = (tag == ECMA_ExtendedFileEntry) ? 208 : 168;
    uint32_t ad_off = ea_off + 4;
    uint32_t data_off = ad_off + 4;

    img_write_tag_at_serial(img, lba, 0, tag, serial);
    img_set_u16(img, lba, 20, 4);            /* strategy type */
    img_set_u8(img, lba, 27, file_type);
    img_set_u16(img, lba, 34, icb_flags);
    img_set_u64(img, lba, 56, length);
    img_set_u32(img, lba, ea_off, 0);        /* l_ea */
    img_set_u32(img, lba, ad_off, l_ad);
    if ((icb_flags & 7) != 3 && l_ad) {
        img_set_u32(img, lba, data_off, ad_len);  /* plain length (extent type 0) */
        img_set_u32(img, lba, data_off + 4, ad_lba);
    }
}

/* File Identifier; icb_lba is partition-relative. The FID is composed in a
 * buffer first so it can be written across block boundaries. */
void img_write_fid_at(struct udf_image *img, uint32_t lba, uint32_t off, uint8_t characteristic,
                      uint8_t encoding, const uint8_t *name, uint32_t name_len,
                      uint32_t icb_lba, uint16_t serial)
{
    uint32_t used = 4 * ((39 + name_len + 3) / 4);
    uint8_t buf[512];

    memset(buf, 0, sizeof(buf));
    buf[0] = (uint8_t)ECMA_FileIdentifierDescriptor;
    buf[1] = (uint8_t)(ECMA_FileIdentifierDescriptor >> 8);
    buf[2] = 3;   /* descriptor version number */
    buf[6] = (uint8_t)serial;
    buf[7] = (uint8_t)(serial >> 8);
    buf[4] = (uint8_t)(buf[0] + buf[1] + buf[2] + buf[6] + buf[7]);
    buf[18] = characteristic;
    buf[19] = (uint8_t)(1 + name_len);
    buf[20] = 0;  /* icb length 2048 */
    buf[21] = 8;
    buf[22] = 0;
    buf[23] = 0;
    buf[24] = (uint8_t)icb_lba;
    buf[25] = (uint8_t)(icb_lba >> 8);
    buf[26] = (uint8_t)(icb_lba >> 16);
    buf[27] = (uint8_t)(icb_lba >> 24);
    buf[28] = (uint8_t)img->ad_part_ref;
    buf[29] = (uint8_t)(img->ad_part_ref >> 8);
    /* l_iu at +36: 0 */
    buf[38] = encoding;
    memcpy(buf + 39, name, name_len);

    _img_write_buf(img, lba, off, buf, used);
}

/*
 * directory registry
 */

static int _img_find_dir(struct udf_image *img, const char *path, struct img_dir **out)
{
    uint32_t i;

    for (i = 0; i < img->num_dirs; i++) {
        if (!strcmp(img->dirs[i].path, path)) {
            *out = &img->dirs[i];
            return 0;
        }
    }
    return -1;
}

static uint32_t _img_alloc(struct udf_image *img)
{
    uint32_t lba = img->used;
    img_block(img, lba);
    return lba;
}

/* File Identifier appended to a directory; grows / relocates the directory
 * data when it does not fit. */
static void _img_add_fid(struct udf_image *img, struct img_dir *parent,
                         uint8_t characteristic, uint8_t encoding,
                         const uint8_t *name, uint32_t name_len,
                         uint32_t icb_lba, uint16_t serial)
{
    uint32_t used = 4 * ((39 + name_len + 3) / 4);
    uint32_t off = parent->data_off + parent->data_len;
    uint32_t ad_off = parent->data_off ? parent->data_off - 4
                                       : (parent->fe_tag == ECMA_ExtendedFileEntry ? 216 : 176);

    if (off + used > parent->data_blocks * IMG_BLOCK_SIZE) {
        uint32_t new_blocks = parent->data_blocks * 2;
        if (new_blocks < parent->data_blocks + 16) {
            new_blocks = parent->data_blocks + 16;
        }
        if (parent->data_off) {
            abort();  /* inline directory cannot grow */
        }
        if (parent->data_lba + parent->data_blocks != img->used) {
            /* directory data is interleaved with other blocks: relocate */
            uint32_t new_lba = img->used;
            img_block(img, new_lba + new_blocks - 1);
            memmove(img->data + (size_t)new_lba * IMG_BLOCK_SIZE,
                    img->data + (size_t)parent->data_lba * IMG_BLOCK_SIZE,
                    (size_t)parent->data_blocks * IMG_BLOCK_SIZE);
            parent->data_lba = new_lba;
            img_set_u32(img, parent->fe_lba, ad_off + 4, new_lba - img->part_lba);
        } else {
            img_block(img, parent->data_lba + new_blocks - 1);
        }
        parent->data_blocks = new_blocks;
    }

    img_write_fid_at(img, parent->data_lba, off, characteristic, encoding,
                     name, name_len, icb_lba, serial);

    parent->data_len += used;

    /* keep the directory FE's allocation descriptor length in sync:
     * _parse_dir() walks the whole extent, so trailing zeroed bytes
     * would be rejected as "unexpected tag 0" */
    img_set_u32(img, parent->fe_lba, ad_off, parent->data_len);
}

static void _img_add_dir_comp(struct udf_image *img, struct img_dir *parent,
                              uint16_t tag, uint8_t encoding, const uint8_t *name,
                              uint32_t name_len, const char *path, uint16_t serial,
                              int inline_dir)
{
    struct img_dir *d;
    uint32_t fe_lba = _img_alloc(img);
    uint32_t data_lba = fe_lba;
    uint32_t data_off = (tag == ECMA_ExtendedFileEntry) ? 216 : 176;

    if (inline_dir) {
        img_write_fe(img, fe_lba, tag, ECMA_FT_DIR, 3, 0, 0, 0, 0, serial);
    } else {
        data_lba = _img_alloc(img);
        img_write_fe(img, fe_lba, tag, ECMA_FT_DIR, 0, 8, 2048, 2048,
                     data_lba - img->part_lba, serial);
        img_block(img, data_lba);
    }

    _img_add_fid(img, parent, CHAR_FLAG_DIR, encoding, name, name_len,
                 fe_lba - img->part_lba, serial);

    if (img->num_dirs == img->max_dirs) {
        img->max_dirs *= 2;
        img->dirs = (struct img_dir *)realloc(img->dirs, img->max_dirs * sizeof(struct img_dir));
    }
    d = &img->dirs[img->num_dirs++];
    strncpy(d->path, path, sizeof(d->path) - 1);
    d->path[sizeof(d->path) - 1] = 0;
    d->fe_lba = fe_lba;
    d->data_lba = data_lba;
    d->data_len = 0;
    d->data_blocks = 1;
    d->fe_tag = tag;
    d->data_off = inline_dir ? data_off : 0;
}

static void _img_add_file_comp(struct udf_image *img, struct img_dir *parent,
                               uint16_t tag, uint8_t encoding, const uint8_t *name,
                               uint32_t name_len, const uint8_t *content, uint32_t len,
                               uint16_t serial)
{
    uint32_t fe_lba = _img_alloc(img);
    uint32_t data_off = (tag == ECMA_ExtendedFileEntry) ? 216 : 176;
    uint32_t inline_max = IMG_BLOCK_SIZE - data_off;

    if (len <= inline_max) {
        /* inline file */
        img_write_fe(img, fe_lba, tag, ECMA_FT_BYTESTREAM, 3, len, len, 0, 0, serial);
        memcpy(img_block(img, fe_lba) + data_off, content, len);
    } else {
        /* extent file */
        uint32_t num_ads = (len + 2047) / 2048;
        uint32_t data_lba = _img_alloc(img);
        uint32_t i;

        img_write_fe(img, fe_lba, tag, ECMA_FT_BYTESTREAM, 0, 8 * num_ads, len, 0, 0, serial);
        for (i = 0; i < num_ads; i++) {
            uint32_t ad_len = len - 2048 * i;
            if (ad_len > 2048) {
                ad_len = 2048;
            }
            img_set_u32(img, fe_lba, data_off + 8 * i, ad_len);
            img_set_u32(img, fe_lba, data_off + 4 + 8 * i, data_lba - img->part_lba + i);
            memcpy(img_block(img, data_lba + i), content + 2048 * i, ad_len);
        }
    }

    _img_add_fid(img, parent, 0, encoding, name, name_len, fe_lba - img->part_lba, serial);
}

/*
 * disc layout
 */

void img_build_disc(struct udf_image *img, uint16_t part_number, uint32_t part_lba)
{
    uint32_t vds_lba = 257;
    uint32_t i;

    img->part_number = part_number;
    img->part_lba = part_lba;
    img->ad_part_ref = 0;  /* map index 0: ECMA-167 3/8.8 reference number */

    /* Volume Recognition Sequence */
    memcpy(img_block(img, 16), "\0BEA01\1", 7);
    memcpy(img_block(img, 17), "\0NSR02\1", 7);
    memcpy(img_block(img, 18), "\0TEA01\1", 7);

    /* Anchor Volume Descriptor Pointer */
    img_write_avdp(img, 256, vds_lba, 4 * 2048, vds_lba + 4, 4 * 2048);

    /* Volume Descriptor Sequences (main + backup).
     * One descriptor per block: _search_vds() decodes only the
     * descriptor at the start of each block. */
    for (i = 0; i < 2; i++) {
        uint32_t b = vds_lba + 4 * i;
        _img_write_pvd(img, b, 0);
        _img_write_pd(img, b + 1, 0, img->part_number);
        img->pd_lba[img->num_pd++] = b + 1;
        _img_write_lvd(img, b + 2, 0, img->ad_part_ref, 0);
        img_write_tag(img, b + 3, ECMA_TerminatingDescriptor);
    }

    /* partition: File Set Descriptor + root directory */
    img_write_fsd(img, part_lba);
    img_write_fe(img, part_lba + 1, ECMA_FileEntry, ECMA_FT_DIR, 0, 8, 2048, 2048, 2, 0);
    img_block(img, part_lba + 2);

    /* register root directory */
    img->dirs = (struct img_dir *)calloc(1, sizeof(struct img_dir));
    img->max_dirs = 1;
    img->num_dirs = 1;
    strcpy(img->dirs[0].path, "/");
    img->dirs[0].fe_lba = part_lba + 1;
    img->dirs[0].data_lba = part_lba + 2;
    img->dirs[0].data_len = 0;
    img->dirs[0].data_blocks = 1;
    img->dirs[0].fe_tag = ECMA_FileEntry;
    img->dirs[0].data_off = 0;
}

void img_set_ad_part_ref(struct udf_image *img, uint16_t ref)
{
    img->ad_part_ref = ref;
}

static void _img_add_file_path(struct udf_image *img, const char *path,
                               const uint8_t *content, uint32_t len,
                               uint16_t tag, uint16_t serial)
{
    char tmp[1024];
    char cur[1024] = "";
    char *save = NULL;
    char *tok;
    struct img_dir *parent = NULL;

    if (_img_find_dir(img, "/", &parent) < 0) {
        return;
    }

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;

    tok = strtok_r(tmp, "/", &save);
    while (tok) {
        char *next = strtok_r(NULL, "/", &save);

        strncat(cur, "/", sizeof(cur) - strlen(cur) - 1);
        strncat(cur, tok, sizeof(cur) - strlen(cur) - 1);

        if (next) {
            struct img_dir *d;
            if (_img_find_dir(img, cur, &d) < 0) {
                _img_add_dir_comp(img, parent, ECMA_FileEntry, 8,
                                  (const uint8_t *)tok, (uint32_t)strlen(tok),
                                  cur, 0, 0);
                _img_find_dir(img, cur, &d);
            }
            parent = d;
        } else {
            _img_add_file_comp(img, parent, tag, 8, (const uint8_t *)tok,
                               (uint32_t)strlen(tok), content, len, serial);
        }
        tok = next;
    }
}

static void _img_add_dir_path(struct udf_image *img, const char *path,
                              uint16_t tag, uint16_t serial, int inline_dir)
{
    char tmp[1024];
    char cur[1024] = "";
    char *save = NULL;
    char *tok;
    struct img_dir *parent = NULL;

    if (_img_find_dir(img, "/", &parent) < 0) {
        return;
    }

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;

    tok = strtok_r(tmp, "/", &save);
    while (tok) {
        struct img_dir *d;

        strncat(cur, "/", sizeof(cur) - strlen(cur) - 1);
        strncat(cur, tok, sizeof(cur) - strlen(cur) - 1);

        if (_img_find_dir(img, cur, &d) < 0) {
            _img_add_dir_comp(img, parent, tag, 8, (const uint8_t *)tok,
                              (uint32_t)strlen(tok), cur, serial, inline_dir);
            _img_find_dir(img, cur, &d);
        }
        parent = d;
        tok = strtok_r(NULL, "/", &save);
    }
}

void img_add_file(struct udf_image *img, const char *path, const uint8_t *content, uint32_t len)
{
    _img_add_file_path(img, path, content, len, ECMA_FileEntry, 0);
}

void img_add_dir(struct udf_image *img, const char *path)
{
    _img_add_dir_path(img, path, ECMA_FileEntry, 0, 0);
}

void img_add_file_serial(struct udf_image *img, const char *path,
                         const uint8_t *content, uint32_t len, uint16_t serial)
{
    _img_add_file_path(img, path, content, len, ECMA_FileEntry, serial);
}

void img_add_dir_serial(struct udf_image *img, const char *path, uint16_t serial)
{
    _img_add_dir_path(img, path, ECMA_FileEntry, serial, 0);
}

void img_add_file_efe(struct udf_image *img, const char *path,
                      const uint8_t *content, uint32_t len)
{
    _img_add_file_path(img, path, content, len, ECMA_ExtendedFileEntry, 0);
}

void img_add_dir_efe(struct udf_image *img, const char *path)
{
    _img_add_dir_path(img, path, ECMA_ExtendedFileEntry, 0, 0);
}

void img_add_inline_dir(struct udf_image *img, const char *path)
{
    _img_add_dir_path(img, path, ECMA_FileEntry, 0, 1);
}

void img_add_file_raw(struct udf_image *img, const char *parent_path, uint8_t encoding,
                      const uint8_t *name, uint32_t name_len,
                      const uint8_t *content, uint32_t len)
{
    struct img_dir *parent;

    if (_img_find_dir(img, parent_path, &parent) < 0) {
        return;
    }
    _img_add_file_comp(img, parent, ECMA_FileEntry, encoding, name, name_len,
                       content, len, 0);
}

void img_add_dir_raw(struct udf_image *img, const char *parent_path, uint8_t encoding,
                     const uint8_t *name, uint32_t name_len)
{
    struct img_dir *parent;
    char path[256];
    uint32_t n;

    if (_img_find_dir(img, parent_path, &parent) < 0) {
        return;
    }
    n = (uint32_t)strlen(parent_path);
    if (n + 1 + name_len >= sizeof(path)) {
        return;
    }
    memcpy(path, parent_path, n);
    path[n++] = '/';
    memcpy(path + n, name, name_len);
    path[n + name_len] = 0;
    _img_add_dir_comp(img, parent, ECMA_FileEntry, encoding, name, name_len,
                      path, 0, 0);
}

void img_add_file_ads(struct udf_image *img, const char *path, uint64_t length,
                      const struct img_ad *ads, uint32_t num_ads)
{
    char tmp[1024];
    char cur[1024] = "";
    char *save = NULL;
    char *tok;
    struct img_dir *parent = NULL;
    uint32_t fe_lba;
    uint32_t i;

    if (_img_find_dir(img, "/", &parent) < 0) {
        return;
    }

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;

    tok = strtok_r(tmp, "/", &save);
    while (tok) {
        char *next = strtok_r(NULL, "/", &save);

        strncat(cur, "/", sizeof(cur) - strlen(cur) - 1);
        strncat(cur, tok, sizeof(cur) - strlen(cur) - 1);

        if (next) {
            struct img_dir *d;
            if (_img_find_dir(img, cur, &d) < 0) {
                _img_add_dir_comp(img, parent, ECMA_FileEntry, 8,
                                  (const uint8_t *)tok, (uint32_t)strlen(tok),
                                  cur, 0, 0);
                _img_find_dir(img, cur, &d);
            }
            parent = d;
        } else {
            fe_lba = _img_alloc(img);
            img_write_fe(img, fe_lba, ECMA_FileEntry, ECMA_FT_BYTESTREAM, 0,
                         8 * num_ads, length, 0, 0, 0);
            for (i = 0; i < num_ads; i++) {
                img_set_u32(img, fe_lba, 176 + 8 * i,
                            (ads[i].len & 0x3fffffff) | ((uint32_t)ads[i].type << 30));
                img_set_u32(img, fe_lba, 180 + 8 * i, ads[i].lba);
            }
            _img_add_fid(img, parent, 0, 8, (const uint8_t *)tok,
                         (uint32_t)strlen(tok), fe_lba - img->part_lba, 0);
        }
        tok = next;
    }
}

/*
 * file output
 */

int img_write_file(struct udf_image *img, const char *path)
{
    FILE *f = fopen(path, "wb");

    if (!f) {
        return -1;
    }
    if (fwrite(img->data, IMG_BLOCK_SIZE, img->used, f) != img->used) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

