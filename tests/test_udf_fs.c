/*
 * Unit tests for the udf_fs layer (src/udf_fs.c).
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
 * Note: udf_fs.c logs to stderr (udf_error unconditionally, udf_log /
 * udf_trace gated by static enable_log/enable_trace which default to 0
 * here), so assertions are on return codes and state, not log output.
 */

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Winline"
#endif

#include "test_ecma.h"
#include "test_blockinput.h"

#include "../src/udf_fs.c"

#include "test_udf_fs.h"

/* ---- tests ---- */

static void test_find_root_dir(void)
{
    struct fs_test_env e;
    struct file_set_descriptor fsd;
    struct long_ad fsd_loc;
    static const uint32_t trap105[] = { 105 };
    static const uint32_t trap105106107[] = { 105, 106, 107 };
    static const uint32_t trap1051005[] = { 105, 1005 };

    fs_test_init(&e, 0);
    fsd_loc.length = UDF_BLOCK_SIZE;
    fsd_loc.lba = 5;
    fsd_loc.partition = 0;

    /* FSD found on the first block */
    SET_SUBCASE("first block");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    make_fsd_block(img_block(105), 7, 0);
    CHECK_EQ_INT(0, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));
    CHECK_EQ_UINT(7, fsd.root_icb.lba);
    CHECK_EQ_UINT(0, fsd.root_icb.partition);

    /* FSD found after scanning multiple blocks */
    SET_SUBCASE("multiple blocks");
    fs_test_case(&e, trap105106107, 3);
    memset(image, 0, sizeof(image));
    make_fsd_block(img_block(107), 7, 0);
    fsd_loc.length = 3 * UDF_BLOCK_SIZE;
    CHECK_EQ_INT(0, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));
    CHECK_EQ_UINT(7, fsd.root_icb.lba);
    fsd_loc.length = UDF_BLOCK_SIZE;

    /* terminating descriptor: no FSD found */
    SET_SUBCASE("terminating descriptor");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    make_vds_block(img_block(105), ECMA_TerminatingDescriptor);
    CHECK_EQ_INT(-1, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));

    /* unhandled tag in the FSD area */
    SET_SUBCASE("unhandled tag");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    make_vds_block(img_block(105), ECMA_PartitionDescriptor);
    CHECK_EQ_INT(-1, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));

    /* zeroed block: tag 0 is unhandled, loop ends, not found */
    SET_SUBCASE("zeroed block");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    CHECK_EQ_INT(-1, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));

    /* mirror: primary block fails tag validation, mirror has the FSD */
    SET_SUBCASE("mirror");
    fs_test_case(&e, trap1051005, 2);
    memset(image, 0, sizeof(image));
    make_fsd_block(img_block(1005), 7, 0);
    set_tag(img_block(105), ECMA_FileSetDescriptor);
    img_block(105)[4] ^= 0xff;   /* corrupt checksum */
    part_init(&e.part, 1000);
    fs_init(&e.fs, &e.m, &e.cap, &e.log, &e.ctx, &e.part);
    CHECK_EQ_INT(0, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));
    CHECK_EQ_UINT(7, fsd.root_icb.lba);
    part_init(&e.part, 0);
    fs_init(&e.fs, &e.m, &e.cap, &e.log, &e.ctx, &e.part);

    /* zero-length FSD extent: pointer targets the FSD itself, probe one block */
    SET_SUBCASE("zero-length extent");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    make_fsd_block(img_block(105), 7, 0);
    fsd_loc.length = 0;
    CHECK_EQ_INT(0, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));
    CHECK_EQ_UINT(7, fsd.root_icb.lba);
    fsd_loc.length = UDF_BLOCK_SIZE;

    /* zero-length FSD extent with no FSD on disc: single failed probe */
    SET_SUBCASE("zero-length no FSD");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    fsd_loc.length = 0;
    CHECK_EQ_INT(-1, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));
    fsd_loc.length = UDF_BLOCK_SIZE;

    /* descriptor version number 2 (ECMA 167, 3/7.2: readers shall
     * accept both versions 2 and 3) */
    SET_SUBCASE("version 2");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    make_fsd_block(img_block(105), 7, 0);
    set_tag_version(img_block(105), 2);
    CHECK_EQ_INT(0, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));
    CHECK_EQ_UINT(7, fsd.root_icb.lba);

    /* wrong tag location: best effort, accept the descriptor */
    SET_SUBCASE("wrong location");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    make_fsd_block(img_block(105), 7, 0);
    set_tag_location(img_block(105), 999);
    CHECK_EQ_INT(0, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));
    CHECK_EQ_UINT(7, fsd.root_icb.lba);

    /* valid DescriptorCRC accepted */
    SET_SUBCASE("valid CRC");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    make_fsd_block(img_block(105), 7, 0);
    set_tag_crc(img_block(105), 512);
    CHECK_EQ_INT(0, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));
    CHECK_EQ_UINT(7, fsd.root_icb.lba);

    /* broken DescriptorCRC: descriptor rejected (UDF 2.60, 2.2.1.2:
     * Descriptor CRC should be validated when reading) */
    SET_SUBCASE("broken CRC");
    fs_test_case(&e, trap105, 1);
    memset(image, 0, sizeof(image));
    make_fsd_block(img_block(105), 7, 0);
    set_tag_crc(img_block(105), 512);
    img_block(105)[8] ^= 0xff;
    CHECK_EQ_INT(-1, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));

    /* FSD extent runs past end of the image: scan stops at the last
     * readable block, not found */
    SET_SUBCASE("past end");
    fs_test_case(&e, NULL, 0);
    memset(image, 0, sizeof(image));
    fsd_loc.lba = IMG_BLOCKS - 3;
    fsd_loc.length = 10 * UDF_BLOCK_SIZE;
    CHECK_EQ_INT(-1, udf_find_root_dir(&e.fs, &fsd_loc, &fsd));
    fsd_loc.lba = 5;
    fsd_loc.length = UDF_BLOCK_SIZE;
}

static void test_read_dir(void)
{
    struct fs_test_env e;
    struct udf_dir_entries dir;
    struct long_ad icb;
    uint32_t used;
    uint32_t i;
    static const uint32_t trap110[] = { 110 };
    static const uint32_t trap110111[] = { 110, 111 };
    static const uint32_t trap110111112[] = { 110, 111, 112 };
    static const uint32_t trap3100[] = { 3100 };
    static const uint32_t trap1103100[] = { 110, 3100 };

    fs_test_init(&e, 0);
    icb.length = UDF_BLOCK_SIZE;
    icb.lba = 10;
    icb.partition = 0;

    /* valid directory: two entries in one data block */
    SET_SUBCASE("valid dir");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 11);
    used = make_fid(img_block(111), 0, "FILE1.TXT", 0, 0);
    make_fid(img_block(111) + used, CHAR_FLAG_DIR, "SUBDIR", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(2, dir.num_entries);
    CHECK_EQ_STR("FILE1.TXT", dir.files[0].filename);
    CHECK_EQ_STR("SUBDIR", dir.files[1].filename);
    CHECK(dir.files[1].characteristic & CHAR_FLAG_DIR);
    udf_clean_dir_entries(&dir);

    /* inline directory */
    SET_SUBCASE("inline dir");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 3, 52, 52, 0, 0);
    make_fid_at(img_block(110) + 176, 0, "INLINE.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(1, dir.num_entries);
    CHECK_EQ_STR("INLINE.TXT", dir.files[0].filename);
    udf_clean_dir_entries(&dir);

    /* file entry is not a directory */
    SET_SUBCASE("not a dir");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 96, 96, 11);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(-1, udf_read_dir(&e.fs, &icb, &dir));

    /* missing file entry */
    SET_SUBCASE("missing FE");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(-1, udf_read_dir(&e.fs, &icb, &dir));

    /* empty directory (no allocation descriptors): ECMA-167 4/8.6 allows
     * zero entries, so this is a valid empty directory */
    SET_SUBCASE("empty dir no AD");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 0, 0, 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(0, dir.num_entries);

    /* empty directory: allocated extent is entirely zeroed (damaged or
     * interrupted-write disc) -> empty directory, not a parse error */
    SET_SUBCASE("empty dir zeroed");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 2048, 2048, 11);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(0, dir.num_entries);

    /* fragmented directory: best effort on the first extent */
    SET_SUBCASE("fragmented dir");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 16, 96, 96, 11);
    set_u32(img_block(110) + 184, 2048);   /* second extent */
    set_u32(img_block(110) + 188, 12);
    used = make_fid(img_block(111), 0, "FILE1.TXT", 0, 0);
    make_fid(img_block(111) + used, CHAR_FLAG_DIR, "SUBDIR", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(2, dir.num_entries);
    udf_clean_dir_entries(&dir);

    /* parse failure: data block is not a file identifier */
    SET_SUBCASE("parse failure");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 11);
    make_vds_block(img_block(111), ECMA_PartitionDescriptor);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(-1, udf_read_dir(&e.fs, &icb, &dir));

    /* parent entry skipped (spec: parent entry has empty filename) */
    SET_SUBCASE("parent skipped");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 92, 92, 11);
    used = make_fid(img_block(111), CHAR_FLAG_PARENT, "", 0, 0);
    make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(1, dir.num_entries);
    CHECK_EQ_STR("FILE1.TXT", dir.files[0].filename);
    udf_clean_dir_entries(&dir);

    /* empty filename skipped */
    SET_SUBCASE("empty filename");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 88, 88, 11);
    used = make_fid(img_block(111), 0, "", 0, 0);
    make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(1, dir.num_entries);
    CHECK_EQ_STR("FILE1.TXT", dir.files[0].filename);
    udf_clean_dir_entries(&dir);

    /* unknown partition */
    SET_SUBCASE("unknown partition");
    fs_test_case(&e, NULL, 0);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 11);
    icb.partition = 5;
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(-1, udf_read_dir(&e.fs, &icb, &dir));
    icb.partition = 0;

    /* file entry read failure (no mirror) */
    SET_SUBCASE("FE read fail");
    fs_test_case(&e, trap3100, 1);
    memset(image, 0, sizeof(image));
    icb.lba = 3000;
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(-1, udf_read_dir(&e.fs, &icb, &dir));
    icb.lba = 10;

    /* directory data read failure */
    SET_SUBCASE("dir data read fail");
    fs_test_case(&e, trap1103100, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 3000);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(-1, udf_read_dir(&e.fs, &icb, &dir));

    /* inline directory with unparsable content */
    SET_SUBCASE("inline unparsable");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 3, 52, 52, 0, 0);
    make_vds_block(img_block(110) + 176, ECMA_PartitionDescriptor);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(-1, udf_read_dir(&e.fs, &icb, &dir));

    /* file identifier with zero-length filename skipped */
    SET_SUBCASE("zero len filename");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 88, 88, 11);
    used = make_fid(img_block(111), 0, "X", 0, 0);
    img_block(111)[19] = 0;   /* filename_len = 0 */
    make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(1, dir.num_entries);
    CHECK_EQ_STR("FILE1.TXT", dir.files[0].filename);
    udf_clean_dir_entries(&dir);

    /* file identifier with unsupported cs0 encoding skipped */
    SET_SUBCASE("bad cs0 encoding");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 88, 88, 11);
    used = make_fid(img_block(111), 0, "X", 0, 0);
    img_block(111)[38] = 7;   /* bad cs0 encoding byte */
    make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(1, dir.num_entries);
    CHECK_EQ_STR("FILE1.TXT", dir.files[0].filename);
    udf_clean_dir_entries(&dir);

    /* directory spanning two blocks: FIDs fill both blocks exactly,
     * one FID ends at the block boundary, the next starts in block 2 */
    SET_SUBCASE("two blocks");
    fs_test_case(&e, trap110111112, 3);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 4096, 4096, 11);
    for (i = 0; i < 64; i++) {
        char name[24];
        memset(name, 'A', 23);   /* 23 chars -> FID size 64 */
        name[23] = 0;
        make_fid_at(img_block(111) + 64 * i, 0, name, 0, 0);
    }
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(64, dir.num_entries);
    if (dir.num_entries == 64) {
        CHECK_EQ_STR("AAAAAAAAAAAAAAAAAAAAAAA", dir.files[0].filename);
        CHECK_EQ_STR("AAAAAAAAAAAAAAAAAAAAAAA", dir.files[31].filename);
        CHECK_EQ_STR("AAAAAAAAAAAAAAAAAAAAAAA", dir.files[32].filename);
        CHECK_EQ_STR("AAAAAAAAAAAAAAAAAAAAAAA", dir.files[63].filename);
    }
    udf_clean_dir_entries(&dir);

    /* deleted entry is skipped (UDF 2.3.4.2.1) */
    SET_SUBCASE("deleted skipped");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 11);
    used = make_fid(img_block(111), CHAR_FLAG_DELETED, "DEAD.TXT", 0, 0);
    make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(1, dir.num_entries);
    if (dir.num_entries == 1) {
        CHECK_EQ_STR("FILE1.TXT", dir.files[0].filename);
        CHECK((dir.files[0].characteristic & CHAR_FLAG_DELETED) == 0);
    }
    udf_clean_dir_entries(&dir);

    /* deleted entry with erased compression id skipped
     * (UDF 2.3.4.2.1: compression id 8 is rewritten to 254) */
    SET_SUBCASE("deleted comp id");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 11);
    used = make_fid(img_block(111), CHAR_FLAG_DELETED, "DEAD.TXT", 0, 0);
    img_block(111)[38] = 254;
    make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(1, dir.num_entries);
    CHECK_EQ_STR("FILE1.TXT", dir.files[0].filename);
    udf_clean_dir_entries(&dir);

    /* hidden entry returned with characteristics preserved */
    SET_SUBCASE("hidden entry");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 104, 104, 11);
    used = make_fid(img_block(111), CHAR_FLAG_HIDDEN, "HIDDEN.TXT", 0, 0);
    make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(2, dir.num_entries);
    CHECK(dir.files[0].characteristic & CHAR_FLAG_HIDDEN);
    CHECK_EQ_STR("HIDDEN.TXT", dir.files[0].filename);
    udf_clean_dir_entries(&dir);

    /* implementation use area: filename starts at 38 + l_iu
     * (UDF 2.3.4.4; l_iu >= 32 when used) */
    SET_SUBCASE("impl use area");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 132, 132, 11);
    {
        uint8_t iu[32];
        memset(iu, 0xaa, sizeof(iu));   /* ADImpUse: flags + UDF UniqueID */
        used = make_fid_iu_at(img_block(111), 0, "WITHIU.TXT", 0, 0, iu, 32);
        make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    }
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(2, dir.num_entries);
    CHECK_EQ_STR("WITHIU.TXT", dir.files[0].filename);
    udf_clean_dir_entries(&dir);

    /* truncated FID: filename length overruns the directory data.
     * Parsing stops after the entries decoded so far. */
    SET_SUBCASE("truncated FID");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 11);
    make_fid(img_block(111), 0, "TRUNCATED.TXT", 0, 0);
    img_block(111)[19] = 200;   /* filename_len > remaining data */
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(0, dir.num_entries);
    udf_clean_dir_entries(&dir);

    /* duplicate names are both returned */
    SET_SUBCASE("duplicate names");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 11);
    used = make_fid(img_block(111), 0, "DUP.TXT", 0, 0);
    make_fid(img_block(111) + used, CHAR_FLAG_DIR, "DUP.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(2, dir.num_entries);
    CHECK_EQ_STR("DUP.TXT", dir.files[0].filename);
    CHECK_EQ_STR("DUP.TXT", dir.files[1].filename);
    udf_clean_dir_entries(&dir);

    /* spec-conformant parent FID: no filename at all (UDF 2.3.4.6),
     * next FID follows immediately at offset 40 */
    SET_SUBCASE("parent FID");
    fs_test_case(&e, trap110111, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 88, 88, 11);
    used = make_parent_fid_at(img_block(111), 5);
    make_fid(img_block(111) + used, 0, "FILE1.TXT", 0, 0);
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(0, udf_read_dir(&e.fs, &icb, &dir));
    CHECK_EQ_UINT(1, dir.num_entries);
    CHECK_EQ_STR("FILE1.TXT", dir.files[0].filename);
    udf_clean_dir_entries(&dir);

    /* zero-length ICB extent: no file entry can be read */
    SET_SUBCASE("zero ICB");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_DIR, 0, 8, 96, 96, 11);
    icb.length = 0;
    memset(&dir, 0, sizeof(dir));
    CHECK_EQ_INT(-1, udf_read_dir(&e.fs, &icb, &dir));
    icb.length = UDF_BLOCK_SIZE;
}

static void test_read_file_entry(void)
{
    struct fs_test_env e;
    struct file_entry *fe;
    struct long_ad icb;
    static const uint32_t trap110[] = { 110 };
    static const uint32_t trap110112[] = { 110, 112 };
    static const uint32_t trap1103100[] = { 110, 3100 };
    static const uint32_t trap204719982048[] = { 2047, 1998, 2048 };
    static const uint32_t trap20472048[] = { 2047, 2048 };
    static const uint32_t trap210[]      = { 210 };

    fs_test_init(&e, 0);
    icb.length = UDF_BLOCK_SIZE;
    icb.lba = 10;
    icb.partition = 0;

    /* valid file entry */
    SET_SUBCASE("valid FE");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(ECMA_FT_BYTESTREAM, fe->file_type);
        CHECK_EQ_UINT(2048, fe->length);
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(2048, fe->u.ads.ad[0].length);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* unknown tag */
    SET_SUBCASE("unknown tag");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_vds_block(img_block(110), ECMA_PartitionDescriptor);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK(fe == NULL);

    /* allocation extent chain: last ad is an AD pointer */
    SET_SUBCASE("AD chain");
    fs_test_case(&e, trap110112, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 16, 2048, 2048, 11);
    set_u32(img_block(110) + 184, 0xC0000000 | 2048);  /* AD pointer */
    set_u32(img_block(110) + 188, 12);
    make_vds_block(img_block(112), ECMA_AllocationExtentDescriptor);
    set_u32(img_block(112) + 20, 8);   /* l_ad */
    set_u32(img_block(112) + 24, 2048);
    set_u32(img_block(112) + 28, 13);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(2, fe->u.ads.num_ad);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        CHECK_EQ_UINT(13, fe->u.ads.ad[1].lba);
        free_file_entry(&fe);
    }

    /* extended file entry */
    SET_SUBCASE("extended FE");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_vds_block(img_block(110), ECMA_ExtendedFileEntry);
    set_u16(img_block(110) + 20, 4);            /* strategy type */
    img_block(110)[27] = ECMA_FT_BYTESTREAM;
    set_u16(img_block(110) + 34, 0);            /* icb flags: short ads */
    set_u64(img_block(110) + 56, 2048);         /* length */
    set_u32(img_block(110) + 208, 0);           /* l_ea */
    set_u32(img_block(110) + 212, 8);           /* l_ad */
    set_u32(img_block(110) + 216, 2048);        /* extent length */
    set_u32(img_block(110) + 220, 11);          /* extent lba */
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(ECMA_FT_BYTESTREAM, fe->file_type);
        CHECK_EQ_UINT(2048, fe->length);
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* allocation extent chain: read failure */
    SET_SUBCASE("AD read fail");
    fs_test_case(&e, trap1103100, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 16, 2048, 2048, 11);
    set_u32(img_block(110) + 184, 0xC0000000 | 2048);  /* AD pointer */
    set_u32(img_block(110) + 188, 3000);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* allocation extent chain: unexpected tag */
    SET_SUBCASE("AD unexpected tag");
    fs_test_case(&e, trap110112, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 16, 2048, 2048, 11);
    set_u32(img_block(110) + 184, 0xC0000000 | 2048);  /* AD pointer */
    set_u32(img_block(110) + 188, 12);
    make_vds_block(img_block(112), ECMA_PartitionDescriptor);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* allocation extent chain: invalid allocation extent */
    SET_SUBCASE("AD invalid");
    fs_test_case(&e, trap110112, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 16, 2048, 2048, 11);
    set_u32(img_block(110) + 184, 0xC0000000 | 2048);  /* AD pointer */
    set_u32(img_block(110) + 188, 12);
    make_vds_block(img_block(112), ECMA_AllocationExtentDescriptor);
    set_u32(img_block(112) + 20, 0xFFFFFFFF);   /* l_ad too large */
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* multi-block file entry: second block read fails on primary,
     * mirror has it */
    SET_SUBCASE("multi-block mirror");
    fs_test_case(&e, trap204719982048, 3);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(2047), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    part_init(&e.part, 50);
    e.part.p[0].length++;   /* let the extent reach the last block: the read
                           * attempt past media end fails (mock bounds) */
    fs_init(&e.fs, &e.m, &e.cap, &e.log, &e.ctx, &e.part);
    icb.lba = 1947;
    icb.length = 2 * UDF_BLOCK_SIZE;
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }
    icb.lba = 10;
    icb.length = UDF_BLOCK_SIZE;
    part_init(&e.part, 0);

    /* multi-block file entry: second block outside partition */
    SET_SUBCASE("multi-block OOB");
    fs_test_case(&e, trap20472048, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(2047), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    part_init(&e.part, 100);
    fs_init(&e.fs, &e.m, &e.cap, &e.log, &e.ctx, &e.part);
    icb.lba = 1947;
    icb.length = 2 * UDF_BLOCK_SIZE;
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK(fe == NULL);
    icb.lba = 10;
    icb.length = UDF_BLOCK_SIZE;
    part_init(&e.part, 0);

    /* file entry beyond declared partition length */
    SET_SUBCASE("FE beyond part");
    fs_test_case(&e, NULL, 0);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    part_init(&e.part, 0);
    e.part.p[0].length = 100;
    fs_init(&e.fs, &e.m, &e.cap, &e.log, &e.ctx, &e.part);
    icb.lba = 110;
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK(fe == NULL);
    icb.lba = 10;
    part_init(&e.part, 0);

    /* unknown partition length (0): bounds checks disabled */
    SET_SUBCASE("unknown part len");
    fs_test_case(&e, trap210, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(210), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    part_init(&e.part, 0);
    e.part.p[0].length = 0;
    fs_init(&e.fs, &e.m, &e.cap, &e.log, &e.ctx, &e.part);
    icb.lba = 110;
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(ECMA_FT_BYTESTREAM, fe->file_type);
        free_file_entry(&fe);
    }
    icb.lba = 10;
    part_init(&e.part, 0);

    /* extended file entry with inline content */
    SET_SUBCASE("inline content");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_ext_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 3, 4, 4, 0, 0);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1, fe->content_inline);
        CHECK_EQ_UINT(4, fe->u.data.information_length);
        CHECK(fe->u.data.content[0] == 'a' && fe->u.data.content[3] == 'd');
        free_file_entry(&fe);
    }

    /* file entry with long_ad allocation descriptors (icb flags 1) */
    SET_SUBCASE("long_ad");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 1, 16, 2048, 0, 0);
    set_u32(img_block(110) + 176, 2048);   /* long_ad length */
    set_u32(img_block(110) + 180, 11);     /* long_ad lba */
    set_u16(img_block(110) + 184, 0);      /* long_ad partition */
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(2048, fe->u.ads.ad[0].length);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* file entry with ext_ad allocation descriptors (icb flags 2) */
    SET_SUBCASE("ext_ad");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 2, 20, 2048, 0, 0);
    set_u32(img_block(110) + 176, 2048);   /* ext_ad extent length */
    set_u32(img_block(110) + 180, 0);      /* recorded length */
    set_u32(img_block(110) + 184, 0);      /* information length */
    set_u32(img_block(110) + 188, 11);     /* extent lba */
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* file entry with extended attribute area: ads start at 176 + l_ea */
    SET_SUBCASE("EA area FE");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry_ea(img_block(110), ECMA_FT_BYTESTREAM, 0, 64, 8, 2048, 2048, 11);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(2048, fe->u.ads.ad[0].length);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* extended file entry with extended attribute area:
     * ads start at 216 + l_ea */
    SET_SUBCASE("EA area EFE");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_ext_file_entry_ea(img_block(110), ECMA_FT_BYTESTREAM, 0, 64, 8, 2048, 2048, 11);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(2048, fe->u.ads.ad[0].length);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* indirect entry (ECMA 167, 4/7.2.1 tag 259) is not a file entry */
    SET_SUBCASE("indirect entry");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_vds_block(img_block(110), 259);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK(fe == NULL);

    /* terminal entry (ECMA 167, 4/7.2.1 tag 260) is not a file entry */
    SET_SUBCASE("terminal entry");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_vds_block(img_block(110), 260);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK(fe == NULL);

    /* allocation extent: maximum l_ad (block size - AED header) parses.
     * long ads (icb flags 1): FE has data ad + AD extent pointer,
     * AED appends (2048-24)/16 more ads. */
    SET_SUBCASE("max l_ad");
    fs_test_case(&e, trap110112, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 1, 32, 2048, 2048, 11);
    set_u32(img_block(110) + 192, 0xC0000000 | 2048);  /* AD pointer */
    set_u32(img_block(110) + 196, 12);
    make_vds_block(img_block(112), ECMA_AllocationExtentDescriptor);
    set_u32(img_block(112) + 20, UDF_BLOCK_SIZE - 24);   /* max l_ad */
    {
        uint32_t i;
        for (i = 0; i < (UDF_BLOCK_SIZE - 24) / 16; i++) {
            set_u32(img_block(112) + 24 + i * 16, 2048);
            set_u32(img_block(112) + 28 + i * 16, 13);
        }
    }
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1 + (UDF_BLOCK_SIZE - 24) / 16, fe->u.ads.num_ad);
        CHECK_EQ_UINT(13, fe->u.ads.ad[(UDF_BLOCK_SIZE - 24) / 16].lba);
        free_file_entry(&fe);
    }

    /* allocation extent: l_ad larger than the block is rejected */
    SET_SUBCASE("l_ad too large");
    fs_test_case(&e, trap110112, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 1, 32, 2048, 2048, 11);
    set_u32(img_block(110) + 192, 0xC0000000 | 2048);
    set_u32(img_block(110) + 196, 12);
    make_vds_block(img_block(112), ECMA_AllocationExtentDescriptor);
    set_u32(img_block(112) + 20, UDF_BLOCK_SIZE - 20);
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(1, fe->u.ads.num_ad);
        CHECK_EQ_UINT(11, fe->u.ads.ad[0].lba);
        free_file_entry(&fe);
    }

    /* allocation extent chain that loops back to itself is rejected */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 16, 2048, 2048, 11);
    set_u32(img_block(110) + 184, 0xC0000000 | 2048);  /* AD pointer */
    set_u32(img_block(110) + 188, 12);
    make_vds_block(img_block(112), ECMA_AllocationExtentDescriptor);
    set_u32(img_block(112) + 20, 8);                   /* one short ad */
    set_u32(img_block(112) + 24, 0xC0000000 | 2048);   /* ...pointing back */
    set_u32(img_block(112) + 28, 12);                  /* to this same extent */
    SET_SUBCASE("AD loop");
    fs_test_case(&e, trap110112, 2);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 16, 2048, 2048, 11);
    set_u32(img_block(110) + 184, 0xC0000000 | 2048);  /* AD pointer */
    set_u32(img_block(110) + 188, 12);
    make_vds_block(img_block(112), ECMA_AllocationExtentDescriptor);
    set_u32(img_block(112) + 20, 8);                   /* one short ad */
    set_u32(img_block(112) + 24, 0xC0000000 | 2048);   /* ...pointing back */
    set_u32(img_block(112) + 28, 12);                  /* to this same extent */
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK(fe == NULL);

    /* zero-length ICB extent: nothing to read */
    SET_SUBCASE("zero ICB");
    fs_test_case(&e, trap110, 1);
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    icb.length = 0;
    fe = udf_read_file_entry(&e.fs, &icb);
    CHECK(fe == NULL);
    icb.length = UDF_BLOCK_SIZE;
}

static void test_read_file_blocks(void)
{
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_fs fs;
    struct udf_partitions part;
    struct file_entry *fe;
    struct long_ad icb;
    uint8_t buf[UDF_BLOCK_SIZE];
    uint8_t buf2[2 * UDF_BLOCK_SIZE];
    static const uint32_t trap110[] = { 110 };
    static const uint32_t trap110111[] = { 110, 111 };
    static const uint32_t trap110112[] = { 110, 112 };
    static const uint32_t trap110111112[] = { 110, 111, 112 };
    static const uint32_t trap1103100[] = { 110, 3100 };

    part_init(&part, 0);
    icb.length = UDF_BLOCK_SIZE;
    icb.lba = 10;
    icb.partition = 0;

    /* single extent */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    memset(img_block(111), 0x5a, UDF_BLOCK_SIZE);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110111, 2);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        memset(buf, 0, sizeof(buf));
        CHECK_EQ_UINT(1, udf_read_file_blocks(&fs, fe, buf, 0, 1, 0));
        CHECK(buf[0] == 0x5a && buf[UDF_BLOCK_SIZE - 1] == 0x5a);
        free_file_entry(&fe);
    }

    /* multi-extent: second extent */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 16, 4096, 2048, 11);
    set_u32(img_block(110) + 184, 2048);
    set_u32(img_block(110) + 188, 12);
    memset(img_block(112), 0xa5, UDF_BLOCK_SIZE);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110112, 2);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        memset(buf, 0, sizeof(buf));
        CHECK_EQ_UINT(1, udf_read_file_blocks(&fs, fe, buf, 1, 1, 0));
        CHECK(buf[0] == 0xa5);
        free_file_entry(&fe);
    }

    /* inline file: no blocks to read */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 3, 4, 4, 0, 0);
    memcpy(img_block(110) + 176, "abcd", 4);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(0, udf_read_file_blocks(&fs, fe, buf, 0, 1, 0));
        free_file_entry(&fe);
    }

    /* out of range */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110111, 2);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(0, udf_read_file_blocks(&fs, fe, buf, 5, 1, 0));
        free_file_entry(&fe);
    }

    /* read failure: extent outside the image */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 3000);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap1103100, 2);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(0, udf_read_file_blocks(&fs, fe, buf, 0, 1, 0));
        free_file_entry(&fe);
    }

    /* unallocated extent (lba 0): blocks zero-filled */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 4096, 2048, 0);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        memset(buf2, 0xff, sizeof(buf2));
        CHECK_EQ_UINT(2, udf_read_file_blocks(&fs, fe, buf2, 0, 2, 0));
        CHECK(buf2[0] == 0 && buf2[2 * UDF_BLOCK_SIZE - 1] == 0);
        free_file_entry(&fe);
    }

    /* extent longer than requested read: clamped to request */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 4096, 4096, 11);
    memset(img_block(111), 0x5a, UDF_BLOCK_SIZE);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110111, 2);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        memset(buf, 0, sizeof(buf));
        CHECK_EQ_UINT(1, udf_read_file_blocks(&fs, fe, buf, 0, 1, 0));
        CHECK(buf[0] == 0x5a);
        free_file_entry(&fe);
    }

    /* one call spanning two extents */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 16, 4096, 2048, 11);
    set_u32(img_block(110) + 184, 2048);
    set_u32(img_block(110) + 188, 12);
    memset(img_block(111), 0x5a, UDF_BLOCK_SIZE);
    memset(img_block(112), 0xa5, UDF_BLOCK_SIZE);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110111112, 3);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        memset(buf2, 0, sizeof(buf2));
        CHECK_EQ_UINT(2, udf_read_file_blocks(&fs, fe, buf2, 0, 2, 0));
        CHECK(buf2[0] == 0x5a && buf2[UDF_BLOCK_SIZE - 1] == 0x5a);
        CHECK(buf2[UDF_BLOCK_SIZE] == 0xa5 && buf2[2 * UDF_BLOCK_SIZE - 1] == 0xa5);
        free_file_entry(&fe);
    }

    /* tail extent shorter than a block */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 100, 100, 11);
    memset(img_block(111), 0x5a, UDF_BLOCK_SIZE);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110111, 2);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        memset(buf, 0, sizeof(buf));
        CHECK_EQ_UINT(1, udf_read_file_blocks(&fs, fe, buf, 0, 1, 0));
        CHECK(buf[0] == 0x5a);
        free_file_entry(&fe);
    }

    /* flags argument is passed through to the block input */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    memset(img_block(111), 0x5a, UDF_BLOCK_SIZE);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110111, 2);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        memset(buf, 0, sizeof(buf));
        CHECK_EQ_UINT(1, udf_read_file_blocks(&fs, fe, buf, 0, 1, 1));
        CHECK(buf[0] == 0x5a);
        free_file_entry(&fe);
    }

    /* zero-length read: no blocks are read */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        uint32_t calls = m.read_calls;
        CHECK_EQ_UINT(0, udf_read_file_blocks(&fs, fe, buf, 0, 0, 0));
        CHECK_EQ_UINT(calls, m.read_calls);
        free_file_entry(&fe);
    }
}

static void test_file_lba(void)
{
    struct capture_log cap;
    udf_log log;
    ecma_ctx ctx;
    struct mock_input m;
    struct udf_fs fs;
    struct udf_partitions part;
    struct file_entry *fe;
    struct long_ad icb;
    uint32_t extent_length;
    static const uint32_t trap110[] = { 110 };

    part_init(&part, 0);
    icb.length = UDF_BLOCK_SIZE;
    icb.lba = 10;
    icb.partition = 0;

    /* single extent */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        extent_length = 0;
        CHECK_EQ_UINT(111, udf_file_lba(&fs, fe, 0, &extent_length));
        CHECK_EQ_UINT(1, extent_length);
        CHECK_EQ_UINT(0, udf_file_lba(&fs, fe, 1, NULL));
        free_file_entry(&fe);
    }

    /* empty extent (lba 0) */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 0);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(0, udf_file_lba(&fs, fe, 0, NULL));
        free_file_entry(&fe);
    }

    /* partition mismatch: logs but still maps */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    part.num_partition = 2;
    part.p[1].number = 1;
    part.p[1].lba = PART_LBA;
    icb.partition = 1;
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(111, udf_file_lba(&fs, fe, 0, NULL));
        free_file_entry(&fe);
    }
    icb.partition = 0;
    part_init(&part, 0);

    /* allocation descriptor extent type (built manually) */
    fe = (struct file_entry *)calloc(1, sizeof(struct file_entry) + sizeof(struct long_ad));
    CHECK_NOT_NULL(fe);
    if (fe) {
        fe->file_type = ECMA_FT_BYTESTREAM;
        fe->u.ads.num_ad = 1;
        fe->u.ads.ad[0].length = UDF_BLOCK_SIZE;
        fe->u.ads.ad[0].lba = 11;
        fe->u.ads.ad[0].extent_type = ECMA_AD_EXTENT_AD;
        CHECK_EQ_UINT(0, udf_file_lba(&fs, fe, 0, NULL));
        free_file_entry(&fe);
    }

    /* inline file */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 3, 4, 4, 0, 0);
    memcpy(img_block(110) + 176, "abcd", 4);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        CHECK_EQ_UINT(0, udf_file_lba(&fs, fe, 0, NULL));
        free_file_entry(&fe);
    }

    /* extent type 1 (allocated but not recorded): treated like
     * unallocated space, no block is mapped */
    fe = (struct file_entry *)calloc(1, sizeof(struct file_entry) + sizeof(struct long_ad));
    CHECK_NOT_NULL(fe);
    if (fe) {
        fe->file_type = ECMA_FT_BYTESTREAM;
        fe->u.ads.num_ad = 1;
        fe->u.ads.ad[0].length = UDF_BLOCK_SIZE;
        fe->u.ads.ad[0].lba = 11;
        fe->u.ads.ad[0].extent_type = ECMA_AD_EXTENT_NOT_RECORDED;
        CHECK_EQ_UINT(0, udf_file_lba(&fs, fe, 0, NULL));
        free_file_entry(&fe);
    }

    /* extent type 2 (not allocated): no storage behind it */
    fe = (struct file_entry *)calloc(1, sizeof(struct file_entry) + sizeof(struct long_ad));
    CHECK_NOT_NULL(fe);
    if (fe) {
        fe->file_type = ECMA_FT_BYTESTREAM;
        fe->u.ads.num_ad = 1;
        fe->u.ads.ad[0].length = UDF_BLOCK_SIZE;
        fe->u.ads.ad[0].lba = 11;
        fe->u.ads.ad[0].extent_type = ECMA_AD_EXTENT_NOT_ALLOCATED;
        CHECK_EQ_UINT(0, udf_file_lba(&fs, fe, 0, NULL));
        free_file_entry(&fe);
    }

    /* failed lookup does not touch extent_length */
    memset(image, 0, sizeof(image));
    make_file_entry(img_block(110), ECMA_FT_BYTESTREAM, 0, 8, 2048, 2048, 11);
    mock_init(&m, image, IMG_BLOCKS);
    mock_expect(&m, trap110, 1);
    fs_init(&fs, &m, &cap, &log, &ctx, &part);
    fe = udf_read_file_entry(&fs, &icb);
    CHECK_NOT_NULL(fe);
    if (fe) {
        extent_length = 0xdeadbeef;
        CHECK_EQ_UINT(0, udf_file_lba(&fs, fe, 5, &extent_length));
        CHECK_EQ_UINT(0xdeadbeef, extent_length);
        free_file_entry(&fe);
    }
}

static void test_cs0_to_mutf8(void)
{
    char *s;
    static const uint8_t cs0_8[] = { 8, 'A', 'B' };
    static const uint8_t cs0_16[] = { 16, 0x00, 0x41, 0x00, 0x42 };
    static const uint8_t cs0_bad[] = { 7, 'A' };

    s = udf_cs0_to_mutf8(cs0_8, sizeof(cs0_8));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_STR("AB", s);
        free(s);
    }

    s = udf_cs0_to_mutf8(cs0_16, sizeof(cs0_16));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_STR("AB", s);
        free(s);
    }

    s = udf_cs0_to_mutf8(NULL, 0);
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_STR("", s);
        free(s);
    }

    s = udf_cs0_to_mutf8(cs0_bad, sizeof(cs0_bad));
    CHECK(s == NULL);

    /* 8-bit mode: char >= 0x80 produces 2-byte UTF-8 */
    static const uint8_t cs0_8_mb[] = { 8, 0xE9 };  /* 'é' */
    s = udf_cs0_to_mutf8(cs0_8_mb, sizeof(cs0_8_mb));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_UINT(0xC3, (uint8_t)s[0]);
        CHECK_EQ_UINT(0xA9, (uint8_t)s[1]);
        CHECK_EQ_UINT(0, (uint8_t)s[2]);
        free(s);
    }

    /* 8-bit mode: char 0 produces modified UTF-8 (2-byte: 0xC0 0x80) */
    static const uint8_t cs0_8_null[] = { 8, 0 };
    s = udf_cs0_to_mutf8(cs0_8_null, sizeof(cs0_8_null));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_UINT(0xC0, (uint8_t)s[0]);
        CHECK_EQ_UINT(0x80, (uint8_t)s[1]);
        CHECK_EQ_UINT(0, (uint8_t)s[2]);
        free(s);
    }

    /* 16-bit mode: char >= 0x7FF produces 3-byte UTF-8 */
    static const uint8_t cs0_16_mb[] = { 16, 0x4E, 0x00 };  /* '一' U+4E00 */
    s = udf_cs0_to_mutf8(cs0_16_mb, sizeof(cs0_16_mb));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_UINT(0xE4, (uint8_t)s[0]);
        CHECK_EQ_UINT(0xB8, (uint8_t)s[1]);
        CHECK_EQ_UINT(0x80, (uint8_t)s[2]);
        CHECK_EQ_UINT(0, (uint8_t)s[3]);
        free(s);
    }

    /* 16-bit mode: char in 0x80..0x7FF produces 2-byte UTF-8 */
    static const uint8_t cs0_16_2b[] = { 16, 0x00, 0xE9 };  /* 'é' U+00E9 */
    s = udf_cs0_to_mutf8(cs0_16_2b, sizeof(cs0_16_2b));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_UINT(0xC3, (uint8_t)s[0]);
        CHECK_EQ_UINT(0xA9, (uint8_t)s[1]);
        CHECK_EQ_UINT(0, (uint8_t)s[2]);
        free(s);
    }

    /* 16-bit mode: UTF-16 surrogate pair U+10437.
     * Surrogates are not combined: each is encoded separately
     * (CESU-8 style), not as a single 4-byte UTF-8 sequence. */
    static const uint8_t cs0_16_sur[] = { 16, 0xD8, 0x01, 0xDF, 0x37 };
    s = udf_cs0_to_mutf8(cs0_16_sur, sizeof(cs0_16_sur));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_UINT(0xED, (uint8_t)s[0]);
        CHECK_EQ_UINT(0xA0, (uint8_t)s[1]);
        CHECK_EQ_UINT(0x81, (uint8_t)s[2]);
        CHECK_EQ_UINT(0xED, (uint8_t)s[3]);
        CHECK_EQ_UINT(0xBC, (uint8_t)s[4]);
        CHECK_EQ_UINT(0xB7, (uint8_t)s[5]);
        CHECK_EQ_UINT(0, (uint8_t)s[6]);
        free(s);
    }

    /* 16-bit mode: NUL character uses modified UTF-8 encoding C0 80 */
    static const uint8_t cs0_16_null[] = { 16, 0x00, 0x00 };
    s = udf_cs0_to_mutf8(cs0_16_null, sizeof(cs0_16_null));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_UINT(0xC0, (uint8_t)s[0]);
        CHECK_EQ_UINT(0x80, (uint8_t)s[1]);
        CHECK_EQ_UINT(0, (uint8_t)s[2]);
        free(s);
    }

    /* compression id 254 (deleted name) decodes to an empty name
     * (UDF 2.1.1: the CS0 expansion is empty and unique) */
    static const uint8_t cs0_254[] = { 254, 'A' };
    s = udf_cs0_to_mutf8(cs0_254, sizeof(cs0_254));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_UINT(0, (uint8_t)s[0]);
        free(s);
    }

    /* compression id 255 (deleted name) decodes to an empty name */
    static const uint8_t cs0_255[] = { 255, 'A' };
    s = udf_cs0_to_mutf8(cs0_255, sizeof(cs0_255));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_UINT(0, (uint8_t)s[0]);
        free(s);
    }

    /* reserved compression id 0 is rejected */
    static const uint8_t cs0_zero[] = { 0, 'A' };
    s = udf_cs0_to_mutf8(cs0_zero, sizeof(cs0_zero));
    CHECK(s == NULL);

    /* 8-bit mode with no characters at all */
    static const uint8_t cs0_8_empty[] = { 8 };
    s = udf_cs0_to_mutf8(cs0_8_empty, sizeof(cs0_8_empty));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_STR("", s);
        free(s);
    }

    /* odd-length 16-bit stream: trailing byte handled gracefully */
    static const uint8_t cs0_16_odd[] = { 16, 0x00, 0x41, 0x00 };
    s = udf_cs0_to_mutf8(cs0_16_odd, sizeof(cs0_16_odd));
    CHECK_NOT_NULL(s);
    if (s) {
        CHECK_EQ_STR("A", s);
        free(s);
    }
}

RUN_TESTS(
    TEST_ENTRY(test_find_root_dir),
    TEST_ENTRY(test_read_dir),
    TEST_ENTRY(test_read_file_entry),
    TEST_ENTRY(test_read_file_blocks),
    TEST_ENTRY(test_file_lba),
    TEST_ENTRY(test_cs0_to_mutf8),
)
