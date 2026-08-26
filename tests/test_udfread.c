/*
 * Unit tests for udfread.c (cache layer).
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Includes the library source to reach its static functions.
 * Must NOT be linked against libudfread (duplicate decode_* symbols).
 *
 * Uses the shared test_udf_fs.h for the test image helpers.
 * The udf_fs layer itself is tested in test_udf_fs.c.
 *
 * Note: udfread.c logs to stderr (udf_error unconditionally, udf_log /
 * udf_trace gated by static enable_log/enable_trace which default to 0
 * here), so assertions are on return codes and state, not log output.
 */

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Winline"
#endif

#include "test_ecma.h"
#include "test_blockinput.h"

#include "../src/udfread.c"

#include "test_udf_fs.h"

/* u_init needs udfread struct definition from udfread.c */
static void u_init(struct udfread *u, struct fs_test_env *e)
{
    memset(u, 0, sizeof(*u));
    u->fs.input = &e->m.bi;
    u->fs.ecma = e->fs.ecma;
    u->fs.part = e->fs.part;
}

/* ---- tests ---- */

static void test_scan_dir(void)
{
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_fs fs;
    struct udf_partitions part;
    struct udf_dir dir;
    struct udf_dir_entries none;
    struct long_ad icb;
    uint32_t index = 0xffffffff, used;
    static const uint32_t trap110111[] = { 110, 111 };

    part_init(&part, 0);
    icb.length = UDF_BLOCK_SIZE;
    icb.lba = 10;
    icb.partition = 0;

    memset(image, 0, sizeof(image));
    /* directory data: FILE1.TXT (48) + SUBDIR (48) + HIDDEN.TXT (52) = 148
     * (FID size is 4*((38 + 1 + strlen + 3) / 4): L_FI includes the
     * cs0 encoding byte) */
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 148, 148, 11);
    used = make_fid(img_block(111), 0, "FILE1.TXT", 0, 0);
    used += make_fid(img_block(111) + used, CHAR_FLAG_DIR, "SUBDIR", 0, 0);
    make_fid(img_block(111) + used, CHAR_FLAG_HIDDEN, "HIDDEN.TXT", 0, 0);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110111, 2);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&fs, &icb, &dir.entries));

    CHECK_EQ_INT(0, _scan_dir(&dir.entries, "FILE1.TXT", &index));
    CHECK_EQ_UINT(0, index);
    CHECK_EQ_INT(0, _scan_dir(&dir.entries, "SUBDIR", &index));
    CHECK_EQ_UINT(1, index);

    /* hidden entry is scanned like any other
     * (UDF 3.3.1.1.2: hidden files are processed as normal) */
    CHECK_EQ_UINT(3, dir.entries.num_entries);
    CHECK_EQ_INT(0, _scan_dir(&dir.entries, "HIDDEN.TXT", &index));
    CHECK_EQ_UINT(2, index);

    /* not found: index is not modified */
    index = 123;
    CHECK_EQ_INT(-1, _scan_dir(&dir.entries, "NOPE", &index));
    CHECK_EQ_UINT(123, index);

    /* case sensitive lookup */
    index = 123;
    CHECK_EQ_INT(-1, _scan_dir(&dir.entries, "file1.txt", &index));
    CHECK_EQ_UINT(123, index);

    /* empty entry list */
    memset(&none, 0, sizeof(none));
    CHECK_EQ_INT(-1, _scan_dir(&none, "FILE1.TXT", &index));
    CHECK_EQ_UINT(123, index);

    udf_clean_dir_entries(&dir.entries);
}

/* free the subdirs of an embedded root tree. The root itself is not
 * heap-allocated, so _free_tree() cannot be used on it. */
static void free_root_tree(struct udf_dir *tree)
{
    _clean_tree(tree);
}

/* root dir at 110 (data 111): SUBDIR (dir, icb 113) + FILE1.TXT (file);
 * subdir at 113 (data 114): CHILD.TXT */
static void setup_tree(struct udf_fs *fs, struct mock_input *m, struct udfread *u,
                       struct capture_log *cap, udf_log *log, ecma_ctx *ctx,
                       const struct udf_partitions *part)
{
    struct long_ad root_icb;
    uint32_t used;
    static const uint32_t trap110111113114[] = { 110, 111, 113, 114 };

    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 11);
    used = make_fid(img_block(111), CHAR_FLAG_DIR, "SUBDIR", 13, UDF_BLOCK_SIZE);
    make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    make_file_entry(img_block(113), ECMA_FT_DIR, 0, 8, 48, 48, 14);
    make_fid(img_block(114), 0, "CHILD.TXT", 0, 0);
    mock_init(m, image, IMG_BLOCKS);
    mock_expect(m, trap110111113114, 4);
    fs_init(fs, m, cap, log, ctx, part);

    memset(u, 0, sizeof(*u));
    u->fs.input = &m->bi;
    u->fs.ecma = fs->ecma;
    u->fs.part = fs->part;

    root_icb.length = UDF_BLOCK_SIZE;
    root_icb.lba = 10;
    root_icb.partition = 0;
    CHECK_EQ_INT(0, udf_read_dir(&u->fs, &root_icb, &u->root_dir.entries));
}

static void test_find_file(void)
{
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_fs fs;
    struct udf_partitions part;
    struct udfread u;
    struct udf_dir *tree;
    struct udf_dir dummy;
    const struct udf_file_identifier *fid;

    part_init(&part, 0);
    setup_tree(&fs, &m, &u, &cap, &log, &ctx, &part);

    /* root directory */
    tree = NULL;
    CHECK_EQ_INT(0, _find_file(&u, "/", &tree, NULL));
    CHECK(tree == &u.root_dir);

    /* root with p_fid: no file identifier for "/" */
    fid = NULL;
    CHECK_EQ_INT(-1, _find_file(&u, "/", &tree, &fid));

    /* subdirectory */
    tree = NULL;
    fid = NULL;
    CHECK_EQ_INT(0, _find_file(&u, "/SUBDIR", &tree, &fid));
    CHECK_NOT_NULL(tree);
    CHECK_NOT_NULL(fid);
    if (tree) {
        CHECK_EQ_UINT(1, tree->entries.num_entries);
        CHECK_EQ_STR("CHILD.TXT", tree->entries.files[0].filename);
    }
    if (fid) {
        CHECK_EQ_STR("SUBDIR", fid->filename);
    }

    /* file: tree is set to NULL */
    tree = &dummy;
    fid = NULL;
    CHECK_EQ_INT(0, _find_file(&u, "/FILE1.TXT", &tree, &fid));
    CHECK(tree == NULL);
    CHECK_NOT_NULL(fid);
    if (fid) {
        CHECK_EQ_STR("FILE1.TXT", fid->filename);
    }

    /* not found */
    CHECK_EQ_INT(-1, _find_file(&u, "/NOPE", &tree, &fid));

    /* parent is a file, not a directory */
    CHECK_EQ_INT(-1, _find_file(&u, "/FILE1.TXT/CHILD", &tree, &fid));

    /* depth-2 file lookup */
    tree = &dummy;
    fid = NULL;
    CHECK_EQ_INT(0, _find_file(&u, "/SUBDIR/CHILD.TXT", &tree, &fid));
    CHECK(tree == NULL);
    CHECK_NOT_NULL(fid);
    if (fid) {
        CHECK_EQ_STR("CHILD.TXT", fid->filename);
    }

    /* missing deep component */
    CHECK_EQ_INT(-1, _find_file(&u, "/SUBDIR/NOSUCH", &tree, &fid));

    /* second lookup of the subdirectory hits the cache */
    m.read_calls = 0;
    tree = NULL;
    fid = NULL;
    CHECK_EQ_INT(0, _find_file(&u, "/SUBDIR", &tree, &fid));
    CHECK_EQ_UINT(0, m.read_calls);

    /* path grammar: empty path */
    CHECK_EQ_INT(-1, _find_file(&u, "", &tree, &fid));

    /* path grammar: missing leading separator is accepted */
    tree = &dummy;
    fid = NULL;
    CHECK_EQ_INT(0, _find_file(&u, "SUBDIR", &tree, &fid));
    CHECK_NOT_NULL(tree);

    /* path grammar: trailing separator is accepted */
    tree = &dummy;
    CHECK_EQ_INT(0, _find_file(&u, "/SUBDIR/", &tree, &fid));
    CHECK_NOT_NULL(tree);

    /* path grammar: doubled separator is accepted */
    tree = &dummy;
    CHECK_EQ_INT(0, _find_file(&u, "//SUBDIR", &tree, &fid));
    CHECK_NOT_NULL(tree);

    /* path grammar: backslash separator (allowed per udfread.h) */
    tree = NULL;
    fid = NULL;
    CHECK_EQ_INT(0, _find_file(&u, "\\SUBDIR", &tree, &fid));
    CHECK_NOT_NULL(tree);

    /* path grammar: "." component not allowed */
    CHECK_EQ_INT(-1, _find_file(&u, "/.", &tree, &fid));

    /* path grammar: ".." component not allowed */
    CHECK_EQ_INT(-1, _find_file(&u, "/..", &tree, &fid));

    free_root_tree(&u.root_dir);
}

static void test_read_subdir(void)
{
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_fs fs;
    struct udf_partitions part;
    struct udfread u;
    struct udf_dir *subtree;

    part_init(&part, 0);
    setup_tree(&fs, &m, &u, &cap, &log, &ctx, &part);

    /* non-directory entry */
    subtree = _cache_subdir(&u, &u.root_dir, 1);
    CHECK(subtree == NULL);

    /* directory entry */
    subtree = _cache_subdir(&u, &u.root_dir, 0);
    CHECK_NOT_NULL(subtree);
    if (subtree) {
        CHECK_EQ_UINT(1, subtree->entries.num_entries);
        CHECK_EQ_STR("CHILD.TXT", subtree->entries.files[0].filename);
    }

    /* cached: second call does not re-read */
    m.read_calls = 0;
    subtree = _cache_subdir(&u, &u.root_dir, 0);
    CHECK_NOT_NULL(subtree);
    CHECK_EQ_UINT(0, m.read_calls);

    /* defensive: entry index out of range */
    CHECK(_cache_subdir(&u, &u.root_dir, 2) == NULL);

    /* defensive: NULL tree */
    _clean_tree(NULL);

    free_root_tree(&u.root_dir);
}

static void test_cache_subdir_error(void)
{
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_fs fs;
    struct udf_partitions part;
    struct udfread u;
    struct udf_dir *tree;
    const struct udf_file_identifier *fid;

    part_init(&part, 0);
    setup_tree(&fs, &m, &u, &cap, &log, &ctx, &part);

    /* subdir read failure: point SUBDIR icb at the FID data block */
    u.root_dir.entries.files[0].icb.lba = 11;
    CHECK(_cache_subdir(&u, &u.root_dir, 0) == NULL);

    /* _find_file: subdir cache failure */
    tree = NULL;
    fid = NULL;
    CHECK_EQ_INT(-1, _find_file(&u, "/SUBDIR", &tree, &fid));

    free_root_tree(&u.root_dir);
}

static void test_new_udfdir(void)
{
    struct udfread u;
    struct udf_dir dir;
    UDFDIR *d;

    memset(&u, 0, sizeof(u));
    memset(&dir, 0, sizeof(dir));

    CHECK(_new_udfdir(&u, NULL) == NULL);

    d = _new_udfdir(&u, &dir);
    CHECK_NOT_NULL(d);
    if (d) {
        CHECK(d->dir == &dir);
        CHECK(d->udf == &u);
        free(d);
    }
}

static void test_file_open(void)
{
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_fs fs;
    struct udf_partitions part;
    struct udfread u;
    struct udf_file_identifier fid;
    UDFFILE *f;
    static const uint32_t trap110[] = { 110 };
    static const uint32_t trap3100[] = { 3100 };

    part_init(&part, 0);
    memset(&fid, 0, sizeof(fid));
    fid.icb.length = UDF_BLOCK_SIZE;
    fid.icb.partition = 0;

    /* directory: refused */
    fid.characteristic = CHAR_FLAG_DIR;
    memset(image, 0, sizeof(image));
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, NULL, 0);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    memset(&u, 0, sizeof(u));
    u.fs.input = &m.bi;
    u.fs.ecma = fs.ecma;
    u.fs.part = fs.part;
    CHECK(_file_open(&u, "D", &fid) == NULL);

    /* file entry read failure */
    fid.characteristic = 0;
    fid.icb.lba = 3000;
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap3100, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    memset(&u, 0, sizeof(u));
    u.fs.input = &m.bi;
    u.fs.ecma = fs.ecma;
    u.fs.part = fs.part;
    CHECK(_file_open(&u, "F", &fid) == NULL);

    /* wrong partition reference: refused without reads */
    fid.icb.lba = 10;
    fid.icb.partition = 1;
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, NULL, 0);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    memset(&u, 0, sizeof(u));
    u.fs.input = &m.bi;
    u.fs.ecma = fs.ecma;
    u.fs.part = fs.part;
    CHECK(_file_open(&u, "F", &fid) == NULL);

    /* parent file identifier: not checked here; open fails because the
     * file entry at the ICB is invalid (zeroed block) */
    fid.characteristic = CHAR_FLAG_PARENT;
    fid.icb.partition = 0;
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    memset(&u, 0, sizeof(u));
    u.fs.input = &m.bi;
    u.fs.ecma = fs.ecma;
    u.fs.part = fs.part;
    CHECK(_file_open(&u, "P", &fid) == NULL);

    /* success */
    fid.characteristic = 0;
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    fid.icb.lba = 10;
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    memset(&u, 0, sizeof(u));
    u.fs.input = &m.bi;
    u.fs.ecma = fs.ecma;
    u.fs.part = fs.part;
    f = _file_open(&u, "F", &fid);
    CHECK_NOT_NULL(f);
    if (f) {
        CHECK(f->udf == &u);
        CHECK_NOT_NULL(f->fe);
        free_file_entry(&f->fe);
        free(f);
    }

    /* symlink file type: rejected (no resolution; UDF 2.60 4.2.1.1) */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_SYMLINK, 0, 8, 96, 96, 11);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    memset(&u, 0, sizeof(u));
    u.fs.input = &m.bi;
    u.fs.ecma = fs.ecma;
    u.fs.part = fs.part;
    f = _file_open(&u, "L", &fid);
    CHECK(f == NULL);
}

static void test_read(void)
{
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_fs fs;
    struct udf_partitions part;
    struct udfread u;
    struct file_entry *fe;
    struct long_ad icb;
    UDFFILE f;
    uint8_t buf[UDF_BLOCK_SIZE];
    uint8_t buf2[2 * UDF_BLOCK_SIZE];
    static const uint32_t trap110111112[] = { 110, 111, 112 };

    part_init(&part, 0);
    icb.length = UDF_BLOCK_SIZE;
    icb.lba = 10;
    icb.partition = 0;

    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 4096, 4096, 11);
    memset(img_block(111), 0x5a, UDF_BLOCK_SIZE);
    memset(img_block(112), 0xa5, UDF_BLOCK_SIZE);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110111112, 3);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (!fe) {
        return;
    }

    memset(&u, 0, sizeof(u));
    u.fs.input = &m.bi;
    u.fs.ecma = fs.ecma;
    u.fs.part = fs.part;
    memset(&f, 0, sizeof(f));
    f.udf = &u;
    f.fe = fe;
    f.block_mem = malloc(2 * UDF_BLOCK_SIZE);
    CHECK_NOT_NULL(f.block_mem);
    if (!f.block_mem) {
        free_file_entry(&fe);
        return;
    }
    f.block = ALIGN(f.block_mem, UDF_BLOCK_SIZE);

    /* mid-block read: loads block, copies chunk */
    m.read_calls = 0;
    f.pos = 100;
    CHECK_EQ_INT(50, _read(&f, buf, 50));
    CHECK_EQ_UINT(150, f.pos);
    CHECK_EQ_UINT(1, m.read_calls);
    CHECK_EQ_UINT(0x5a, buf[0]);
    CHECK_EQ_UINT(0x5a, buf[49]);

    /* block already valid: no re-read (cache buffer still holds block 0
     * loaded by the mid-block read above) */
    m.read_calls = 0;
    f.block_valid = 1;
    f.pos = 150;
    CHECK_EQ_INT(50, _read(&f, buf, 50));
    CHECK_EQ_UINT(200, f.pos);
    CHECK_EQ_UINT(0, m.read_calls);

    /* full block */
    f.pos = 0;
    f.block_valid = 0;
    CHECK_EQ_INT(UDF_BLOCK_SIZE, _read(&f, buf, UDF_BLOCK_SIZE));
    CHECK_EQ_UINT(UDF_BLOCK_SIZE, f.pos);
    CHECK_EQ_UINT(0x5a, buf[0]);
    CHECK_EQ_UINT(0x5a, buf[UDF_BLOCK_SIZE - 1]);

    /* multiple blocks */
    f.pos = 0;
    CHECK_EQ_INT(2 * UDF_BLOCK_SIZE, _read(&f, buf2, 2 * UDF_BLOCK_SIZE));
    CHECK_EQ_UINT(2 * UDF_BLOCK_SIZE, f.pos);
    CHECK_EQ_UINT(0x5a, buf2[0]);
    CHECK_EQ_UINT(0x5a, buf2[UDF_BLOCK_SIZE - 1]);
    CHECK_EQ_UINT(0xa5, buf2[UDF_BLOCK_SIZE]);
    CHECK_EQ_UINT(0xa5, buf2[2 * UDF_BLOCK_SIZE - 1]);

    /* partial block */
    f.pos = 0;
    CHECK_EQ_INT(100, _read(&f, buf, 100));
    CHECK_EQ_UINT(100, f.pos);

    /* unaligned read stops at the block boundary; the caller loops */
    f.pos = 1500;
    CHECK_EQ_INT(548, _read(&f, buf2, 2000));
    CHECK_EQ_UINT(2048, f.pos);
    CHECK_EQ_UINT(0x5a, buf2[0]);
    CHECK_EQ_UINT(0x5a, buf2[547]);

    /* continuation read from the next block */
    CHECK_EQ_INT(2000, _read(&f, buf2, 2000));
    CHECK_EQ_UINT(4048, f.pos);
    CHECK_EQ_UINT(0xa5, buf2[0]);
    CHECK_EQ_UINT(0xa5, buf2[1999]);

    /* short read at end of file */
    f.pos = 2 * UDF_BLOCK_SIZE - 50;
    CHECK_EQ_INT(50, _read(&f, buf, 100));
    CHECK_EQ_UINT(2 * UDF_BLOCK_SIZE, f.pos);
    CHECK_EQ_UINT(0xa5, buf[49]);

    /* seek backwards to an already-cached block range: served from cache */
    f.pos = 100;
    m.read_calls = 0;
    CHECK_EQ_INT(50, _read(&f, buf, 50));
    CHECK_EQ_UINT(0, m.read_calls);
    CHECK_EQ_UINT(0xa5, buf[49]);

    /* seek into a different block: cache must be reloaded */
    f.pos = 3000;
    CHECK_EQ_INT(48, _read(&f, buf, 48));
    CHECK_EQ_UINT(0xa5, buf[0]);
    CHECK_EQ_UINT(0xa5, buf[47]);

    /* zero-length read */
    f.pos = 100;
    CHECK_EQ_INT(0, _read(&f, buf, 0));
    CHECK_EQ_UINT(100, f.pos);

    /* read failure: beyond end of file */
    f.pos = 2 * UDF_BLOCK_SIZE;
    CHECK_EQ_INT(-1, _read(&f, buf, 100));

    /* mid-block read failure */
    f.block_valid = 0;
    f.pos = 2 * UDF_BLOCK_SIZE + 100;
    CHECK_EQ_INT(-1, _read(&f, buf, 100));

    /* multi-block read failure */
    f.pos = 2 * UDF_BLOCK_SIZE;
    CHECK_EQ_INT(-1, _read(&f, buf, UDF_BLOCK_SIZE));

    free(f.block_mem);
    free_file_entry(&fe);
}

static void test_read_inline(void)
{
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_fs fs;
    struct udf_partitions part;
    struct udfread u;
    struct file_entry *fe;
    struct long_ad icb;
    UDFFILE f;
    uint8_t buf[4];
    static const uint32_t trap110[] = { 110 };

    part_init(&part, 0);
    icb.length = UDF_BLOCK_SIZE;
    icb.lba = 10;
    icb.partition = 0;

    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 3, 4, 4, 0, 0);
    memcpy(img_block(110) + 176, "abcd", 4);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (!fe) {
        return;
    }

    memset(&u, 0, sizeof(u));
    u.fs.ecma = fs.ecma;
    memset(&f, 0, sizeof(f));
    f.udf = &u;
    f.fe = fe;

    /* whole content */
    CHECK_EQ_INT(4, _read_inline(&f, buf, 4));
    CHECK_EQ_UINT(4, f.pos);
    CHECK_EQ_UINT('a', buf[0]);
    CHECK_EQ_UINT('b', buf[1]);
    CHECK_EQ_UINT('c', buf[2]);
    CHECK_EQ_UINT('d', buf[3]);

    /* read hits padding */
    f.pos = 2;
    CHECK_EQ_INT(4, _read_inline(&f, buf, 4));
    CHECK_EQ_UINT(6, f.pos);
    CHECK_EQ_UINT('c', buf[0]);
    CHECK_EQ_UINT('d', buf[1]);
    CHECK_EQ_UINT(0, buf[2]);
    CHECK_EQ_UINT(0, buf[3]);

    /* position beyond information length */
    f.pos = 5;
    CHECK_EQ_INT(2, _read_inline(&f, buf, 2));
    CHECK_EQ_UINT(7, f.pos);
    CHECK_EQ_UINT(0, buf[0]);
    CHECK_EQ_UINT(0, buf[1]);

    /* far beyond content: zero fill, no buffer overrun */
    f.pos = 4096;
    CHECK_EQ_INT(4, _read_inline(&f, buf, 4));
    CHECK_EQ_UINT(4100, f.pos);
    CHECK_EQ_UINT(0, buf[0]);
    CHECK_EQ_UINT(0, buf[1]);
    CHECK_EQ_UINT(0, buf[2]);
    CHECK_EQ_UINT(0, buf[3]);

    /* zero-length read */
    f.pos = 1;
    CHECK_EQ_INT(0, _read_inline(&f, buf, 0));
    CHECK_EQ_UINT(1, f.pos);

    /* byte-stream read on inline file: unsupported */
    f.pos = 0;
    CHECK_EQ_INT(-1, _read(&f, buf, 4));

    free_file_entry(&fe);
}

RUN_TESTS(
    TEST_ENTRY(test_scan_dir),
    TEST_ENTRY(test_find_file),
    TEST_ENTRY(test_read_subdir),
    TEST_ENTRY(test_cache_subdir_error),
    TEST_ENTRY(test_new_udfdir),
    TEST_ENTRY(test_file_open),
    TEST_ENTRY(test_read),
    TEST_ENTRY(test_read_inline),
)
