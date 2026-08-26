/*
 * Shared test environment for the udf_fs layer (tests/test_udf_fs.c)
 * and the udfread.c cache layer (tests/test_udfread.c).
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Provides the test image helpers. Include this header AFTER
 * ../src/udf_fs.c (test_udf_fs.c) or ../src/udfread.c (test_udfread.c).
 * The .c files must include test_ecma.h and test_blockinput.h BEFORE
 * this header.
 */

#ifndef TEST_UDF_FS_H_
#define TEST_UDF_FS_H_

/* ---- test image ---- */

#define IMG_BLOCKS 2048
#define PART_LBA   100
static uint8_t image[IMG_BLOCKS * UDF_BLOCK_SIZE];

static uint8_t *img_block(uint32_t lba)
{
    return image + (size_t)lba * UDF_BLOCK_SIZE;
}

static void part_init(struct udf_partitions *part, uint32_t mirror_lba)
{
    memset(part, 0, sizeof(*part));
    part->num_partition = 1;
    part->p[0].number = 0;
    part->p[0].lba = PART_LBA;
    part->p[0].mirror_lba = mirror_lba;
    part->p[0].length = IMG_BLOCKS - PART_LBA;
}

static void fs_init(struct udf_fs *fs, struct mock_input *m, struct capture_log *cap,
                    udf_log *log, ecma_ctx *ctx, const struct udf_partitions *part)
{
    log_init(cap, log, ctx);
    memset(fs, 0, sizeof(*fs));
    fs->input = &m->bi;
    fs->ecma.lc = log;
    fs->part = *part;
}

/* ---- test environment helpers ---- */

/* Test environment for udf_fs / udfread unit tests.
 * Bundles all the common state to avoid per-test-function boilerplate. */
struct fs_test_env {
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_partitions part;
    struct udf_fs fs;
    struct long_ad icb;
};

/* Initialize the test environment (call once per test function). */
static inline void fs_test_init(struct fs_test_env *e, uint32_t mirror_lba)
{
    part_init(&e->part, mirror_lba);
    e->icb.length = UDF_BLOCK_SIZE;
    e->icb.lba = 10;
    e->icb.partition = 0;
}

/* Set up a sub-case: init mock with traps, init fs.
 * Call this at the start of each sub-case BEFORE writing test blocks to image. */
static inline void fs_test_case(struct fs_test_env *e, const uint32_t *traps, uint32_t n)
{
    mock_init(&e->m, image, IMG_BLOCKS);
    if (n > 0) {
        mock_expect(&e->m, traps, n);
    } else {
        e->m.expect_enabled = 0;
    }
    fs_init(&e->fs, &e->m, &e->cap, &e->log, &e->ctx, &e->part);
}

/* Initialize udfread struct from an fs_test_env (for test_udfread.c).
 * Must be defined in test_udfread.c after including udfread.c. */

#endif /* TEST_UDF_FS_H_ */

