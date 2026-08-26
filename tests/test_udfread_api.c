/*
 * API tests for libudfread (public udfread.h API)
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Drives the public API (udfread_open_input, udfread_opendir,
 * udfread_file_open, ...) against crafted in-memory images built with
 * tests/udf_image.c. Links the real library (libudfread_dep).
 */

#include "test_util.h"
#include "udf_image.h"
#include "test_api.h"

#include "udfread.h"
#include <udfread-version.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static void test_init(void)
{
    udfread *udf = udfread_init();

    CHECK_NOT_NULL(udf);
    udfread_close(udf);
    udfread_close(NULL);
}

static void test_version(void)
{
    int major = -1, minor = -1, micro = -1;
    int major2 = -2, minor2 = -2, micro2 = -2;

    /* runtime version must match the build-time defines */
    udfread_get_version(&major, &minor, &micro);
    CHECK(major >= 0);
    CHECK(minor >= 0);
    CHECK(micro >= 0);
    CHECK_EQ_INT(UDFREAD_VERSION_MAJOR, major);
    CHECK_EQ_INT(UDFREAD_VERSION_MINOR, minor);
    CHECK_EQ_INT(UDFREAD_VERSION_MICRO, micro);

    /* stable across calls */
    udfread_get_version(&major2, &minor2, &micro2);
    CHECK_EQ_INT(major, major2);
    CHECK_EQ_INT(minor, minor2);
    CHECK_EQ_INT(micro, micro2);

    /* NULL arguments are allowed and ignored */
    udfread_get_version(NULL, NULL, NULL);

    major2 = -2;
    udfread_get_version(&major2, NULL, NULL);
    CHECK_EQ_INT(UDFREAD_VERSION_MAJOR, major2);

    minor2 = -2;
    micro2 = -2;
    udfread_get_version(NULL, &minor2, &micro2);
    CHECK_EQ_INT(UDFREAD_VERSION_MINOR, minor2);
    CHECK_EQ_INT(UDFREAD_VERSION_MICRO, micro2);
}

static void test_open(void)
{
    struct udf_image *img;
    udfread *udf;
    UDFFILE *f;
    char tmp_path[256];

    /* happy path: write image to temp file, open with udfread_open */
    img = img_new();
    img_build_disc(img, 0, 100);
    img_add_file(img, "/FILE1.TXT", (const uint8_t *)"hello", 5);
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/udfread_test_XXXXXX");
    int fd = mkstemp(tmp_path);
    CHECK(fd >= 0);
    close(fd);
    CHECK_EQ_INT(0, img_write_file(img, tmp_path));

    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(0, udfread_open(udf, tmp_path));

    f = udfread_file_open(udf, "/FILE1.TXT");
    CHECK_NOT_NULL(f);
    udfread_file_close(f);
    udfread_close(udf);

    /* empty path rejected */
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_open(udf, ""));
    udfread_close(udf);

    /* NULL path rejected */
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_open(udf, NULL));
    udfread_close(udf);

    /* non-existent file */
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_open(udf, "/nonexistent/path"));
    udfread_close(udf);

    /* non-UDF regular file */
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/udfread_test_XXXXXX");
    fd = mkstemp(tmp_path);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK((ssize_t)write(fd, "this is not a disc\n", 19) == 19);
        close(fd);
    }
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_open(udf, tmp_path));
    udfread_close(udf);
    unlink(tmp_path);

    /* directory path */
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_open(udf, "/tmp"));
    udfread_close(udf);

    unlink(tmp_path);
    img_free(img);
}

static void test_open_input(void)
{
    struct udf_image *img;
    udfread *udf;
    UDFFILE *f;
    uint32_t i;

    /* happy path */
    img = new_std_disc();
    udf = open_image(img);
    /* double open: rejected */
    CHECK_EQ_INT(-1, udfread_open_input(udf, &img->bi));
    udfread_close(udf);
    img_free(img);

    /* NULL arguments */
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_open_input(NULL, NULL));
    CHECK_EQ_INT(-1, udfread_open_input(udf, NULL));

    /* input without read callback */
    img = new_std_disc();
    img->bi.read = NULL;
    CHECK_EQ_INT(-1, udfread_open_input(udf, &img->bi));
    udfread_close(udf);
    img_free(img);

    /* zeroed image */
    img = img_new();
    img_init_input(img);
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_open_input(udf, &img->bi));
    udfread_close(udf);
    img_free(img);

    /* garbage (0xff-filled) image */
    img = img_new();
    img_block(img, 300);
    for (i = 0; i < img->used * IMG_BLOCK_SIZE; i++) {
        img->data[i] = 0xff;
    }
    img_init_input(img);
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_open_input(udf, &img->bi));
    udfread_close(udf);
    img_free(img);

    /* size callback is optional when the anchor is found at block 256
     * (UDF 2.60, 2.2.3 fallback positions need it) */
    img = new_std_disc();
    img_init_input(img);
    img->bi.size = NULL;
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(0, udfread_open_input(udf, &img->bi));
    f = udfread_file_open(udf, "/FILE1.TXT");
    CHECK_NOT_NULL(f);
    udfread_file_close(f);
    udfread_close(udf);
    img_free(img);

    /* no anchor at block 256 and no size callback: clean error */
    img = img_new();
    img_init_input(img);
    img->bi.size = NULL;
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_open_input(udf, &img->bi));
    udfread_close(udf);
    img_free(img);
}

static void test_get_volume_id(void)
{
    struct udf_image *img = new_std_disc();
    udfread *udf = open_image(img);
    CHECK_EQ_STR("CONFORMANCE", udfread_get_volume_id(udf));
    CHECK(udfread_get_volume_id(NULL) == NULL);
    udfread_close(udf);
    img_free(img);

    /* CS0-8 Volume Identifier with latin-1 character is returned as
     * MUTF-8 (ECMA-167 3/10.1 BP 24: dstring[32], UDF 2.1.1 cs0).
     * ECMA-167 1/7.2.12: last byte of the field holds the number of
     * bytes used to record the characters (encoding byte included). */
    img = img_new();
    img_build_disc(img, 0, 100);
    memset(img_block(img, 257) + 24, 0, 32);   /* Volume Identifier */
    img->data[257 * IMG_BLOCK_SIZE + 24] = 8;  /* CS0 encoding */
    memcpy(img->data + 257 * IMG_BLOCK_SIZE + 25, "VOL\xc5", 4);
    img->data[257 * IMG_BLOCK_SIZE + 55] = 5;
    udf = open_image(img);
    CHECK_EQ_STR("VOL\xc3\x85", udfread_get_volume_id(udf));
    udfread_close(udf);
    img_free(img);

    /* all-zero dstring field is invalid (ECMA-167 1/7.2.12), decoded
     * best-effort to an empty identifier */
    img = img_new();
    img_build_disc(img, 0, 100);
    memset(img_block(img, 257) + 24, 0, 32);
    udf = open_image(img);
    CHECK_EQ_STR("", udfread_get_volume_id(udf));
    udfread_close(udf);
    img_free(img);
}

static void test_get_volume_set_id(void)
{
    struct udf_image *img = new_std_disc();
    udfread *udf = open_image(img);
    uint8_t buf[128];

    CHECK_EQ_UINT(0, udfread_get_volume_set_id(NULL, buf, sizeof(buf)));
    CHECK_EQ_UINT(0, udfread_get_volume_set_id(udf, NULL, 10));
    CHECK_EQ_UINT(128, udfread_get_volume_set_id(udf, buf, sizeof(buf)));
    CHECK(all_zero(buf, sizeof(buf)));
    CHECK_EQ_UINT(128, udfread_get_volume_set_id(udf, buf, 10));
    CHECK_EQ_UINT(128, udfread_get_volume_set_id(udf, buf, 0));

    udfread_close(udf);
    img_free(img);

    /* content: raw Volume Set Identifier bytes are returned as-is
     * (ECMA-167 3/10.1 BP 72: dstring[128] in the PVD) */
    img = img_new();
    img_build_disc(img, 0, 100);
    memcpy(img_block(img, 257) + 72, "\x08SETID", 6);
    udf = open_image(img);
    memset(buf, 0xaa, sizeof(buf));
    CHECK_EQ_UINT(128, udfread_get_volume_set_id(udf, buf, sizeof(buf)));
    CHECK_EQ_UINT(8, buf[0]);
    CHECK(memcmp(buf + 1, "SETID", 5) == 0);
    CHECK(all_zero(buf + 6, sizeof(buf) - 6));

    udfread_close(udf);
    img_free(img);
}

static void test_select_partition(void)
{
    struct udf_image *img;
    udfread *udf = udfread_init();
    UDFFILE *f;

    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_select_partition(NULL, 0));
    CHECK_EQ_INT(0, udfread_select_partition(udf, 0));
    CHECK_EQ_INT(0, udfread_select_partition(udf, UDFREAD_PARTITION_FIRST));
    CHECK_EQ_INT(0, udfread_select_partition(udf, UDFREAD_PARTITION_LAST));
    CHECK_EQ_INT(-1, udfread_select_partition(udf, -3));

    /* after open: error */
    img = new_std_disc();
    img_init_input(img);
    CHECK_EQ_INT(0, udfread_open_input(udf, &img->bi));
    CHECK_EQ_INT(-1, udfread_select_partition(udf, 0));

    udfread_close(udf);
    img_free(img);

    /* selection is honoured: image with partition number 7 only */
    img = img_new();
    img_build_disc(img, 7, 100);
    img_add_file(img, "/FILE1.TXT", (const uint8_t *)"hello", 5);

    /* default (FIRST) opens partition 7 */
    img_init_input(img);
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(0, udfread_open_input(udf, &img->bi));
    f = udfread_file_open(udf, "/FILE1.TXT");
    CHECK_NOT_NULL(f);
    udfread_file_close(f);
    udfread_close(udf);

    /* requesting the wrong partition number fails */
    img_init_input(img);
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(0, udfread_select_partition(udf, 0));
    CHECK_EQ_INT(-1, udfread_open_input(udf, &img->bi));
    udfread_close(udf);

    /* requesting the existing partition number works */
    img_init_input(img);
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(0, udfread_select_partition(udf, 7));
    CHECK_EQ_INT(0, udfread_open_input(udf, &img->bi));
    udfread_close(udf);

    /* LAST with a single partition */
    img_init_input(img);
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(0, udfread_select_partition(udf, UDFREAD_PARTITION_LAST));
    CHECK_EQ_INT(0, udfread_open_input(udf, &img->bi));
    udfread_close(udf);

    img_free(img);
}

static void test_set_log(void)
{
    struct udf_image *img = img_new();
    struct capture_log cap;
    udfread *udf = udfread_init();

    CHECK_NOT_NULL(udf);
    CHECK_EQ_INT(-1, udfread_set_log(NULL, NULL, NULL));
    CHECK_EQ_INT(-1, udfread_set_log_level(NULL, UDFREAD_LOG_ERROR));

    /* capture logger before open */
    memset(&cap, 0, sizeof(cap));
    CHECK_EQ_INT(0, udfread_set_log(udf, &cap, capture_logger));
    CHECK_EQ_INT(0, udfread_set_log_level(udf, UDFREAD_LOG_ERROR));

    /* invalid levels */
    CHECK_EQ_INT(-1, udfread_set_log_level(udf, -1));
    CHECK_EQ_INT(-1, udfread_set_log_level(udf, 4));

    /* NULL logger resets to the default handler */
    CHECK_EQ_INT(0, udfread_set_log(udf, NULL, NULL));

    /* set_log after open: error; level change after open: ok */
    img_free(img);
    img = new_std_disc();
    img_init_input(img);
    CHECK_EQ_INT(0, udfread_open_input(udf, &img->bi));
    CHECK_EQ_INT(-1, udfread_set_log(udf, &cap, capture_logger));
    CHECK_EQ_INT(0, udfread_set_log_level(udf, UDFREAD_LOG_NONE));
    CHECK_EQ_INT(0, udfread_set_log_level(udf, UDFREAD_LOG_TRACE));

    udfread_close(udf);
    img_free(img);
}

static void test_opendir(void)
{
    struct udf_image *img = img_new();
    struct udfread_dirent entry;
    udfread *udf;
    UDFDIR *dir;
    int found_file = 0, found_subdir = 0, count = 0;

    img_build_disc(img, 0, 100);
    img_add_file(img, "/FILE1.TXT", (const uint8_t *)"hello", 5);
    img_add_dir(img, "/SUBDIR");
    img_add_file(img, "/SUBDIR/CHILD.TXT", (const uint8_t *)"child", 5);
    img_add_dir(img, "/SUBDIR/GRANDKID");
    udf = open_image(img);

    dir = udfread_opendir(udf, "/");
    CHECK_NOT_NULL(dir);
    if (dir) {
        while (udfread_readdir(dir, &entry)) {
            count++;
            if (!strcmp(entry.d_name, "FILE1.TXT")) {
                found_file = 1;
                CHECK_EQ_UINT(UDF_DT_REG, entry.d_type);
            } else if (!strcmp(entry.d_name, "SUBDIR")) {
                found_subdir = 1;
                CHECK_EQ_UINT(UDF_DT_DIR, entry.d_type);
            }
        }
        CHECK_EQ_UINT(2, count);

        /* past end */
        CHECK(udfread_readdir(dir, &entry) == NULL);

        /* rewind re-lists */
        udfread_rewinddir(dir);
        CHECK(udfread_readdir(dir, &entry) != NULL);

        /* NULL arguments */
        CHECK(udfread_readdir(NULL, &entry) == NULL);
        CHECK(udfread_readdir(dir, NULL) == NULL);

        udfread_closedir(dir);
    }
    CHECK(found_file);
    CHECK(found_subdir);

    /* subdir listing */
    dir = udfread_opendir(udf, "/SUBDIR");
    CHECK_NOT_NULL(dir);
    if (dir) {
        CHECK(udfread_readdir(dir, &entry) != NULL);
        CHECK_EQ_STR("CHILD.TXT", entry.d_name);
        udfread_closedir(dir);
    }

    /* errors */
    CHECK(udfread_opendir(udf, "/NOPE") == NULL);
    CHECK(udfread_opendir(udf, "/FILE1.TXT") == NULL);
    CHECK(udfread_opendir(NULL, "/") == NULL);
    CHECK(udfread_opendir(udf, NULL) == NULL);
    CHECK(udfread_opendir(udf, "") == NULL);

    /* dot components are not allowed (udfread.h) */
    CHECK(udfread_opendir(udf, "/./SUBDIR") == NULL);
    CHECK(udfread_opendir(udf, "/SUBDIR/../SUBDIR") == NULL);

    /* extra separators / trailing separator: pin current behaviour */
    dir = udfread_opendir(udf, "//SUBDIR");
    CHECK_NOT_NULL(dir);
    udfread_closedir(dir);
    dir = udfread_opendir(udf, "/SUBDIR/");
    CHECK_NOT_NULL(dir);
    if (dir) {
        CHECK(udfread_readdir(dir, &entry) != NULL);
        CHECK_EQ_STR("CHILD.TXT", entry.d_name);
        udfread_closedir(dir);
    }

    /* opendir_at chain into an empty subdirectory */
    dir = udfread_opendir(udf, "/SUBDIR");
    CHECK_NOT_NULL(dir);
    if (dir) {
        UDFDIR *grand = udfread_opendir_at(dir, "GRANDKID");

        CHECK_NOT_NULL(grand);
        if (grand) {
            /* empty directory: immediate EOF */
            CHECK(udfread_readdir(grand, &entry) == NULL);
            udfread_closedir(grand);
        }
        udfread_closedir(dir);
    }
    udfread_closedir(NULL);

    udfread_close(udf);
    img_free(img);
}

static void test_opendir_at(void)
{
    struct udf_image *img = img_new();
    struct udfread_dirent entry;
    udfread *udf;
    UDFDIR *dir, *sub;

    img_build_disc(img, 0, 100);
    img_add_file(img, "/FILE1.TXT", (const uint8_t *)"hello", 5);
    img_add_dir(img, "/SUBDIR");
    img_add_file(img, "/SUBDIR/CHILD.TXT", (const uint8_t *)"child", 5);
    udf = open_image(img);

    dir = udfread_opendir(udf, "/");
    CHECK_NOT_NULL(dir);
    sub = udfread_opendir_at(dir, "SUBDIR");
    CHECK_NOT_NULL(sub);
    if (sub) {
        CHECK(udfread_readdir(sub, &entry) != NULL);
        CHECK_EQ_STR("CHILD.TXT", entry.d_name);
        udfread_closedir(sub);
    }
    CHECK(udfread_opendir_at(dir, "FILE1.TXT") == NULL);
    CHECK(udfread_opendir_at(dir, "NOPE") == NULL);
    CHECK(udfread_opendir_at(NULL, "SUBDIR") == NULL);
    CHECK(udfread_opendir_at(dir, NULL) == NULL);
    udfread_closedir(dir);

    udfread_close(udf);
    img_free(img);
}

static void test_file_open(void)
{
    struct udf_image *img = img_new();
    udfread *udf;
    UDFDIR *dir;
    UDFFILE *f;

    img_build_disc(img, 0, 100);
    img_add_file(img, "/FILE1.TXT", (const uint8_t *)"hello", 5);
    img_add_dir(img, "/SUBDIR");
    img_add_file(img, "/SUBDIR/CHILD.TXT", (const uint8_t *)"child", 5);
    udf = open_image(img);

    f = udfread_file_open(udf, "/FILE1.TXT");
    CHECK_NOT_NULL(f);
    udfread_file_close(f);

    /* leading separator optional */
    f = udfread_file_open(udf, "FILE1.TXT");
    CHECK_NOT_NULL(f);
    udfread_file_close(f);

    /* backslash separators */
    f = udfread_file_open(udf, "\\SUBDIR\\CHILD.TXT");
    CHECK_NOT_NULL(f);
    udfread_file_close(f);

    /* directory / missing / NULL arguments */
    CHECK(udfread_file_open(udf, "/SUBDIR") == NULL);
    CHECK(udfread_file_open(udf, "/NOPE") == NULL);
    CHECK(udfread_file_open(NULL, "/FILE1.TXT") == NULL);
    CHECK(udfread_file_open(udf, NULL) == NULL);
    CHECK(udfread_file_open(udf, "") == NULL);

    /* dot components are not allowed (udfread.h) */
    CHECK(udfread_file_open(udf, "./FILE1.TXT") == NULL);
    CHECK(udfread_file_open(udf, "/SUBDIR/../FILE1.TXT") == NULL);

    /* extra separators / mixed separators: pin current behaviour */
    f = udfread_file_open(udf, "//FILE1.TXT");
    CHECK_NOT_NULL(f);
    udfread_file_close(f);
    f = udfread_file_open(udf, "\\SUBDIR/CHILD.TXT");
    CHECK_NOT_NULL(f);
    udfread_file_close(f);

    /* file_openat */
    dir = udfread_opendir(udf, "/SUBDIR");
    CHECK_NOT_NULL(dir);
    f = udfread_file_openat(dir, "CHILD.TXT");
    CHECK_NOT_NULL(f);
    udfread_file_close(f);
    CHECK(udfread_file_openat(dir, "NOPE") == NULL);
    CHECK(udfread_file_openat(NULL, "CHILD.TXT") == NULL);
    CHECK(udfread_file_openat(dir, NULL) == NULL);
    udfread_closedir(dir);

    udfread_file_close(NULL);

    udfread_close(udf);
    img_free(img);
}

static void test_file_size(void)
{
    struct udf_image *img = img_new();
    udfread *udf;
    UDFFILE *f;

    img_build_disc(img, 0, 100);
    img_add_file(img, "/FILE1.TXT", (const uint8_t *)"hello", 5);
    img_add_file(img, "/EMPTY", (const uint8_t *)"", 0);
    udf = open_image(img);

    f = udfread_file_open(udf, "/FILE1.TXT");
    CHECK_NOT_NULL(f);
    CHECK_EQ_INT(5, (int)udfread_file_size(f));
    udfread_file_close(f);

    /* empty file */
    f = udfread_file_open(udf, "/EMPTY");
    CHECK_NOT_NULL(f);
    CHECK_EQ_INT(0, (int)udfread_file_size(f));
    udfread_file_close(f);

    CHECK_EQ_INT(-1, (int)udfread_file_size(NULL));

    udfread_close(udf);
    img_free(img);
}

static void test_file_read(void)
{
    struct udf_image *img = img_new();
    uint8_t content[4096];
    uint8_t buf[4096];
    udfread *udf;
    UDFFILE *f;
    uint32_t i;

    for (i = 0; i < sizeof(content); i++) {
        content[i] = (uint8_t)i;
    }
    img_build_disc(img, 0, 100);
    img_add_file(img, "/BIG.BIN", content, sizeof(content));      /* multi-extent */
    img_add_file(img, "/SMALL.TXT", (const uint8_t *)"abcd", 4);  /* inline */
    img_add_file(img, "/EMPTY", (const uint8_t *)"", 0);          /* empty */
    udf = open_image(img);

    /* full read, multi-extent */
    f = udfread_file_open(udf, "/BIG.BIN");
    CHECK_NOT_NULL(f);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ_INT(4096, (int)udfread_file_read(f, buf, sizeof(buf)));
    CHECK(memcmp(buf, content, sizeof(content)) == 0);
    /* EOF */
    CHECK_EQ_INT(0, (int)udfread_file_read(f, buf, 1));
    udfread_file_close(f);

    /* chunked reads (non-block-aligned) */
    f = udfread_file_open(udf, "/BIG.BIN");
    CHECK_NOT_NULL(f);
    for (i = 0; i < 16; i++) {
        CHECK_EQ_INT(256, (int)udfread_file_read(f, buf + 256 * i, 256));
    }
    CHECK(memcmp(buf, content, sizeof(content)) == 0);
    udfread_file_close(f);

    /* inline file */
    f = udfread_file_open(udf, "/SMALL.TXT");
    CHECK_NOT_NULL(f);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ_INT(4, (int)udfread_file_read(f, buf, 4));
    CHECK_EQ_UINT('a', buf[0]);
    CHECK_EQ_UINT('d', buf[3]);
    udfread_file_close(f);

    /* short read at EOF returns the remaining bytes */
    f = udfread_file_open(udf, "/SMALL.TXT");
    CHECK_NOT_NULL(f);
    CHECK_EQ_INT(2, (int)udfread_file_seek(f, -2, UDF_SEEK_END));
    CHECK_EQ_INT(2, (int)udfread_file_read(f, buf, 10));
    CHECK_EQ_UINT('c', buf[0]);
    CHECK_EQ_UINT('d', buf[1]);
    /* and the next read is EOF */
    CHECK_EQ_INT(0, (int)udfread_file_read(f, buf, 1));

    /* zero-length read */
    CHECK_EQ_INT(0, (int)udfread_file_read(f, buf, 0));
    udfread_file_close(f);

    /* empty file */
    f = udfread_file_open(udf, "/EMPTY");
    CHECK_NOT_NULL(f);
    CHECK_EQ_INT(0, (int)udfread_file_read(f, buf, 1));
    udfread_file_close(f);

    /* NULL arguments */
    CHECK_EQ_INT(-1, (int)udfread_file_read(NULL, buf, 1));
    f = udfread_file_open(udf, "/SMALL.TXT");
    CHECK_NOT_NULL(f);
    CHECK_EQ_INT(-1, (int)udfread_file_read(f, NULL, 1));
    udfread_file_close(f);

    udfread_close(udf);
    img_free(img);
}

static void test_file_seek_tell(void)
{
    struct udf_image *img = new_std_disc_with_file("/FILE1.TXT",
                                                    (const uint8_t *)"hello world", 11);
    udfread *udf = open_image(img);
    UDFFILE *f;
    uint8_t buf[4];

    f = udfread_file_open(udf, "/FILE1.TXT");
    CHECK_NOT_NULL(f);

    CHECK_EQ_INT(0, (int)udfread_file_tell(f));
    CHECK_EQ_INT(5, (int)udfread_file_seek(f, 5, UDF_SEEK_SET));
    CHECK_EQ_INT(5, (int)udfread_file_tell(f));
    CHECK_EQ_INT(8, (int)udfread_file_seek(f, 3, UDF_SEEK_CUR));
    CHECK_EQ_INT(11, (int)udfread_file_seek(f, 0, UDF_SEEK_END));
    CHECK_EQ_INT(1, (int)udfread_file_seek(f, -10, UDF_SEEK_END));
    CHECK_EQ_INT(0, (int)udfread_file_seek(f, 0, UDF_SEEK_SET));

    /* errors */
    CHECK_EQ_INT(-1, (int)udfread_file_seek(f, -1, UDF_SEEK_SET));
    CHECK_EQ_INT(-1, (int)udfread_file_seek(f, 0, 3));
    /* invalid whence */
    CHECK_EQ_INT(-1, (int)udfread_file_seek(f, 0, -1));
    /* negative CUR below start of file */
    CHECK_EQ_INT(-1, (int)udfread_file_seek(f, -1, UDF_SEEK_CUR));
    /* seeking past EOF is allowed; reads there return EOF */
    CHECK_EQ_INT(12, (int)udfread_file_seek(f, 12, UDF_SEEK_SET));
    CHECK_EQ_INT(12, (int)udfread_file_tell(f));
    CHECK_EQ_INT(112, (int)udfread_file_seek(f, 100, UDF_SEEK_CUR));
    CHECK_EQ_INT(0, (int)udfread_file_read(f, buf, 4));
    CHECK_EQ_INT(-1, (int)udfread_file_seek(NULL, 0, UDF_SEEK_SET));
    CHECK_EQ_INT(-1, (int)udfread_file_tell(NULL));

    udfread_file_close(f);
    udfread_close(udf);
    img_free(img);
}

static void test_file_lba(void)
{
    struct udf_image *img = img_new();
    uint8_t content[4096];
    udfread *udf;
    UDFFILE *f;
    uint32_t data_lba;

    memset(content, 0x5a, sizeof(content));
    img_build_disc(img, 0, 100);
    img_add_file(img, "/BIG.BIN", content, sizeof(content));
    data_lba = img->used - 2;  /* fe at used-2, data blocks at used-1.. */
    img_add_file(img, "/SMALL.TXT", (const uint8_t *)"abcd", 4);
    udf = open_image(img);

    f = udfread_file_open(udf, "/BIG.BIN");
    CHECK_NOT_NULL(f);
    CHECK_EQ_UINT(data_lba, udfread_file_lba(f, 0));
    CHECK_EQ_UINT(data_lba + 1, udfread_file_lba(f, 1));
    CHECK_EQ_UINT(0, udfread_file_lba(f, 2));  /* beyond file */
    udfread_file_close(f);

    /* inline file: no block address */
    f = udfread_file_open(udf, "/SMALL.TXT");
    CHECK_NOT_NULL(f);
    CHECK_EQ_UINT(0, udfread_file_lba(f, 0));
    udfread_file_close(f);

    CHECK_EQ_UINT(0, udfread_file_lba(NULL, 0));

    udfread_close(udf);
    img_free(img);

    /* fragmented file: file blocks map to the right extent, offsets are
     * absolute (partition start + partition-relative ad lba) */
    img = img_new();
    img_build_disc(img, 0, 100);
    {
        struct img_ad ads[2];
        uint32_t ext0, ext1;

        img_block(img, 130);   /* reserve space past the built-in layout */
        ext0 = img->used + 4;
        ext1 = ext0 + 1;
        ads[0].len = IMG_BLOCK_SIZE;  ads[0].lba = ext0;  ads[0].type = 0;
        ads[1].len = IMG_BLOCK_SIZE;  ads[1].lba = ext1;  ads[1].type = 0;
        img_add_file_ads(img, "/FRAG.BIN", 2 * IMG_BLOCK_SIZE, ads, 2);

        udf = open_image(img);
        f = udfread_file_open(udf, "/FRAG.BIN");
        CHECK_NOT_NULL(f);
        CHECK_EQ_UINT(100 + ext0, udfread_file_lba(f, 0));
        CHECK_EQ_UINT(100 + ext1, udfread_file_lba(f, 1));
        CHECK_EQ_UINT(0, udfread_file_lba(f, 2));
        udfread_file_close(f);
        udfread_close(udf);
    }
    img_free(img);
}

static void test_read_blocks(void)
{
    uint8_t content[4096];
    uint8_t buf[4096];
    udfread *udf;
    UDFFILE *f;
    uint32_t i;

    for (i = 0; i < sizeof(content); i++) {
        content[i] = (uint8_t)(0x5a + i);
    }
    struct udf_image *img = new_std_disc_with_file("/BIG.BIN", content, sizeof(content));
    udf = open_image(img);

    f = udfread_file_open(udf, "/BIG.BIN");
    CHECK_NOT_NULL(f);

    /* argument checks */
    CHECK_EQ_UINT(0, udfread_read_blocks(NULL, buf, 0, 1, 0));
    CHECK_EQ_UINT(0, udfread_read_blocks(f, NULL, 0, 1, 0));
    CHECK_EQ_UINT(0, udfread_read_blocks(f, buf, 0, 0, 0));

    /* real read */
    memset(buf, 0, sizeof(buf));
    CHECK_EQ_UINT(1, udfread_read_blocks(f, buf, 0, 1, 0));
    CHECK(memcmp(buf, content, IMG_BLOCK_SIZE) == 0);

    /* multi-block read spanning both blocks */
    memset(buf, 0, sizeof(buf));
    CHECK_EQ_UINT(2, udfread_read_blocks(f, buf, 0, 2, 0));
    CHECK(memcmp(buf, content, sizeof(content)) == 0);

    /* request clamped to the file size */
    CHECK_EQ_UINT(2, udfread_read_blocks(f, buf, 0, 3, 0));

    /* beyond EOF */
    CHECK_EQ_UINT(0, udfread_read_blocks(f, buf, 2, 1, 0));

    udfread_file_close(f);
    udfread_close(udf);
    img_free(img);
}

RUN_TESTS(TEST_ENTRY(test_init),
          TEST_ENTRY(test_version),
          TEST_ENTRY(test_open),
          TEST_ENTRY(test_open_input),
          TEST_ENTRY(test_get_volume_id),
          TEST_ENTRY(test_get_volume_set_id),
          TEST_ENTRY(test_select_partition),
          TEST_ENTRY(test_set_log),
          TEST_ENTRY(test_opendir),
          TEST_ENTRY(test_opendir_at),
          TEST_ENTRY(test_file_open),
          TEST_ENTRY(test_file_size),
          TEST_ENTRY(test_file_read),
          TEST_ENTRY(test_file_seek_tell),
          TEST_ENTRY(test_file_lba),
          TEST_ENTRY(test_read_blocks))

