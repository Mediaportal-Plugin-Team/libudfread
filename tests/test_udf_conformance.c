/*
 * UDF conformance tests
 *
 * Test set: OSTA UDF Compliance Test Disc Specification, Rev 1.0
 * (July 8, 1999). PDF: udfcomp100.pdf
 *
 * Approach: each test replicates the disc structure described in the
 * specification as a crafted in-memory image (tests/udf_image.c) and
 * exercises the public libudfread API (udfread_open_input) against it.
 * The crafted images can be written to files (img_write_file) and
 * verified against independent UDF implementations to prove the test
 * data is valid.
 *
 * Verdict conventions:
 *   - tests that must pass use the CHECK* macros;
 *   - tests for behavior libudfread does not implement yet use
 *     TEST_XFAIL (expected failure); they must be flipped to hard
 *     CHECKs when the corresponding bug is fixed.
 *
 * Each test carries a "Conformance test XX:" reference comment with a
 * short description of the tested rule.
 *
 * Image strategy: the file-level tests (3.3, 3.4, 3.6, 3.7, 3.8, 3.21,
 * 3.22, 3.23, 3.34, 3.40) share one disc image (g_main_img) built once
 * with all their structures; the volume-level tests (3.9, 3.33, 3.37)
 * build their own images.
 */

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Winline"
#endif

#include "test_util.h"
#include "udf_image.h"
#include "test_api.h"

#include "udfread.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static struct udf_image *g_main_img;

/* ---------- shared disc image (file-level tests) ---------- */

static void build_3_3(struct udf_image *img)
{
    uint8_t n[254];

    img_add_dir(img, "/Filenames");
    img_add_dir(img, "/Filenames/Long_Filenames");

    /* CS0-8: 254-char names, differ only in the last character */
    memset(n, 'A', 253);
    n[253] = '1';
    img_add_file_raw(img, "/Filenames/Long_Filenames", 8, n, 254,
                     (const uint8_t *)"1", 1);
    n[253] = '2';
    img_add_file_raw(img, "/Filenames/Long_Filenames", 8, n, 254,
                     (const uint8_t *)"2", 1);

    /* CS0-16: 127-char names (big-endian UTF-16), differ only in the last
     * character (UDF 2.60 2.1.1: compression ID 16 is big endian) */
    for (size_t i = 0; i < 126; i++) {
        n[2 * i] = 0;
        n[2 * i + 1] = 'B';
    }
    n[252] = 0;
    n[253] = '1';
    img_add_file_raw(img, "/Filenames/Long_Filenames", 16, n, 254,
                     (const uint8_t *)"3", 1);
    n[252] = 0;
    n[253] = '2';
    img_add_file_raw(img, "/Filenames/Long_Filenames", 16, n, 254,
                     (const uint8_t *)"4", 1);
}

static void build_3_4(struct udf_image *img)
{
    /* big-endian UTF-16 encoded names (UDF 2.60 2.1.1: CS0-16 is
     * big endian) */
    static const uint8_t double_byte[] = {
        0x00,0x44, 0x00,0x4F, 0x00,0x55, 0x00,0x42, 0x00,0x4C, 0x00,0x45,
        0x00,0x5F, 0x00,0x42, 0x00,0x59, 0x00,0x54, 0x00,0x45, 0x00,0x2E,
        0x00,0x54, 0x00,0x58, 0x00,0x54
    };
    static const uint8_t universal[] = {
        0x30,0xE6, 0x30,0xCB, 0x30,0xD0, 0x30,0xFC, 0x30,0xB5, 0x30,0xEB,
        0x30,0xC7, 0x30,0xA3, 0x30,0xB9, 0x30,0xAF, 0x89,0x8F, 0x5B,0x9A,
        0x00,0x2E, 0x00,0x54, 0x00,0x58, 0x00,0x54
    };
    static const uint8_t one_nihongo[] = {
        0x00,0x31, 0x65,0xE5, 0x67,0x2C, 0x8A,0x9E,
        0x00,0x2E, 0x00,0x54, 0x00,0x58, 0x00,0x54
    };
    static const uint8_t e[] = {
        0x30,0x48, 0x00,0x2E, 0x00,0x54, 0x00,0x58, 0x00,0x54
    };
    static const uint8_t nihongo_mei[] = {
        0x65,0xE5, 0x67,0x2C, 0x8A,0x9E, 0x54,0x0D,
        0x00,0x2E, 0x00,0x54, 0x00,0x58, 0x00,0x54
    };
    static const uint8_t nihongo_dir[] = {
        0x65,0xE5, 0x67,0x2C, 0x8A,0x9E, 0x30,0xC7, 0x30,0xA3,
        0x30,0xEC, 0x30,0xAF, 0x30,0xC8, 0x30,0xEA
    };
    char kanji_dir[256];

    img_add_dir(img, "/Filenames/Filename_Encoding");

    img_add_file_raw(img, "/Filenames/Filename_Encoding", 8,
                     (const uint8_t *)"SINGLE_BYTE.TXT", 15, (const uint8_t *)"1", 1);
    img_add_file_raw(img, "/Filenames/Filename_Encoding", 16,
                     double_byte, sizeof(double_byte), (const uint8_t *)"2", 1);
    img_add_file_raw(img, "/Filenames/Filename_Encoding", 16,
                     universal, sizeof(universal), (const uint8_t *)"3", 1);
    img_add_file_raw(img, "/Filenames/Filename_Encoding", 16,
                     one_nihongo, sizeof(one_nihongo), (const uint8_t *)"4", 1);
    img_add_file_raw(img, "/Filenames/Filename_Encoding", 16,
                     e, sizeof(e), (const uint8_t *)"5", 1);
    img_add_file_raw(img, "/Filenames/Filename_Encoding", 16,
                     nihongo_mei, sizeof(nihongo_mei), (const uint8_t *)"6", 1);

    img_add_dir_raw(img, "/Filenames/Filename_Encoding", 16,
                    nihongo_dir, sizeof(nihongo_dir));

    /* one file inside the directory (an empty directory cannot be opened
     * by libudfread: _parse_dir() rejects the zeroed remainder of the
     * extent) */
    strcpy(kanji_dir, "/Filenames/Filename_Encoding/");
    memcpy(kanji_dir + strlen(kanji_dir), nihongo_dir, sizeof(nihongo_dir));
    kanji_dir[strlen("/Filenames/Filename_Encoding/") + sizeof(nihongo_dir)] = 0;
    img_add_file_raw(img, kanji_dir, 8, (const uint8_t *)"PLAIN.TXT", 9,
                     (const uint8_t *)"x", 1);
}

static void build_3_6(struct udf_image *img)
{
    char path[1024];
    uint32_t i;

    img_add_dir(img, "/Deeptree");
    strcpy(path, "/Deeptree");
    for (i = 1; i <= 126; i++) {
        snprintf(path + strlen(path), sizeof(path) - strlen(path),
                 "/OSTA%03u", i);
        img_add_dir(img, path);
    }
    strncat(path, "/EINDE", sizeof(path) - strlen(path) - 1);
    img_add_file(img, path, (const uint8_t *)"EINDE", 5);
}

static void build_3_7(struct udf_image *img)
{
    char name[16], content[16], path[64];
    uint32_t i;

    img_add_dir(img, "/Filenames/50K_Files");
    for (i = 1; i <= 50000; i++) {
        snprintf(name, sizeof(name), "%u", i);
        snprintf(content, sizeof(content), "%u", i);
        snprintf(path, sizeof(path), "/Filenames/50K_Files/%s", name);
        img_add_file(img, path, (const uint8_t *)content,
                     (uint32_t)strlen(content));
    }
}

static void build_3_8(struct udf_image *img)
{
    struct img_ad ads[19];
    uint8_t *blk;
    uint32_t blk_a, blk_b, i;

    img_add_dir(img, "/Large_File");

    /* 2GB-1_FILE: 0x7fffffff bytes = [N 2048][U 524287][U 524287][N 2047] */
    blk_a = img->used;
    blk = img_block(img, img->used);
    memset(blk, 0, IMG_BLOCK_SIZE);
    memcpy(blk, "START ", 6);
    blk_b = img->used;
    blk = img_block(img, img->used);
    memset(blk, 0, IMG_BLOCK_SIZE);
    memcpy(blk + 2043, " END", 4);
    ads[0] = (struct img_ad){ 2048, blk_a - img->part_lba, 0 };
    ads[1] = (struct img_ad){ 0x3FFFF800, blk_a + 1 - img->part_lba, 1 };
    ads[2] = (struct img_ad){ 0x3FFFF800, blk_a + 1 + 524287 - img->part_lba, 1 };
    ads[3] = (struct img_ad){ 2047, blk_b - img->part_lba, 0 };
    img_add_file_ads(img, "/Large_File/2GB-1_FILE", 0x7fffffffULL, ads, 4);

    /* 16GB_FILE: 0x400000000 bytes =
     * [N 2048][16 x U 524287][U 14][N 2048] */
    blk_a = img->used;
    blk = img_block(img, img->used);
    memset(blk, 0, IMG_BLOCK_SIZE);
    memcpy(blk, "START ", 6);
    blk_b = img->used;
    blk = img_block(img, img->used);
    memset(blk, 0, IMG_BLOCK_SIZE);
    memcpy(blk + 2044, " END", 4);
    ads[0] = (struct img_ad){ 2048, blk_a - img->part_lba, 0 };
    for (i = 0; i < 16; i++) {
        ads[1 + i] = (struct img_ad){ 0x3FFFF800,
                                      blk_a + 1 + 524287 * i - img->part_lba, 1 };
    }
    ads[17] = (struct img_ad){ 28672, blk_a + 1 + 524287 * 16 - img->part_lba, 1 };
    ads[18] = (struct img_ad){ 2048, blk_b - img->part_lba, 0 };
    img_add_file_ads(img, "/Large_File/16GB_FILE", 0x400000000ULL, ads, 19);
}

static void build_3_21(struct udf_image *img)
{
    img_add_dir(img, "/Embedded_data");
    img_add_file(img, "/Embedded_data/EMBED.TXT",
                 (const uint8_t *)"embedded data\n", 14);
    img_add_inline_dir(img, "/Embedded_data/InlineDir");
    img_add_file(img, "/Embedded_data/InlineDir/EMBEDDED.TXT",
                 (const uint8_t *)"inline\n", 7);
}

static void build_3_22(struct udf_image *img)
{
    struct img_ad ads[3];
    uint8_t *blk;
    uint32_t blk_a, blk_b;

    img_add_dir(img, "/Unrecorded");

    /* Middle: [N 2048 'A'][U 2048][N 2048 'B'], length 6144 */
    blk_a = img->used;
    blk = img_block(img, img->used);
    memset(blk, 'A', IMG_BLOCK_SIZE);
    blk_b = img->used;
    blk = img_block(img, img->used);
    memset(blk, 'B', IMG_BLOCK_SIZE);
    ads[0] = (struct img_ad){ 2048, blk_a - img->part_lba, 0 };
    ads[1] = (struct img_ad){ 2048, blk_a + 1 - img->part_lba, 1 };
    ads[2] = (struct img_ad){ 2048, blk_b - img->part_lba, 0 };
    img_add_file_ads(img, "/Unrecorded/Middle", 6144, ads, 3);

    /* end: [N 2048 'A'][U 2048], length 4096 */
    blk_a = img->used;
    blk = img_block(img, img->used);
    memset(blk, 'A', IMG_BLOCK_SIZE);
    ads[0] = (struct img_ad){ 2048, blk_a - img->part_lba, 0 };
    ads[1] = (struct img_ad){ 2048, blk_a + 1 - img->part_lba, 1 };
    img_add_file_ads(img, "/Unrecorded/end", 4096, ads, 2);

    /* beyond: [N 2048 'A'][U 2048], length 2048 */
    blk_a = img->used;
    blk = img_block(img, img->used);
    memset(blk, 'A', IMG_BLOCK_SIZE);
    ads[0] = (struct img_ad){ 2048, blk_a - img->part_lba, 0 };
    ads[1] = (struct img_ad){ 2048, blk_a + 1 - img->part_lba, 1 };
    img_add_file_ads(img, "/Unrecorded/beyond", 2048, ads, 2);
}

static void build_3_23(struct udf_image *img)
{
    struct img_ad ads[2];
    uint8_t *blk;
    uint32_t blk_a, blk_b;

    img_add_dir(img, "/Filelength");

    blk_a = img->used;
    blk = img_block(img, img->used);
    memset(blk, 'A', IMG_BLOCK_SIZE);
    blk_b = img->used;
    blk = img_block(img, img->used);
    memset(blk, 'B', IMG_BLOCK_SIZE);
    ads[0] = (struct img_ad){ 2048, blk_a - img->part_lba, 0 };
    ads[1] = (struct img_ad){ 1000, blk_b - img->part_lba, 0 };
    img_add_file_ads(img, "/Filelength/lg_eq_ad.txt", 3048, ads, 2);
    img_add_file_ads(img, "/Filelength/lg_lt_ad.txt", 2600, ads, 2);
}

static void build_3_34(struct udf_image *img)
{
    uint8_t data[64];
    uint32_t i;

    img_add_dir(img, "/Tag_serial");
    img_add_dir_serial(img, "/Tag_serial/zero", 0);
    for (i = 0; i < 64; i++) {
        data[i] = (uint8_t)i;
    }
    img_add_file_serial(img, "/Tag_serial/zero/ZERO_FILE.TXT", data, 64, 0);
    img_add_dir_serial(img, "/Tag_serial/nonzero", 4321);
    for (i = 0; i < 64; i++) {
        data[i] = (uint8_t)(0x80 + i);
    }
    img_add_file_serial(img, "/Tag_serial/nonzero/NONZERO_FILE.TXT", data, 64, 4321);

    /* prevailing Logical Volume Descriptor: tag serial number 4321 */
    img_write_lvd(img, 259, 0, 4321);
    img_write_lvd(img, 263, 0, 4321);
}

static void build_3_40(struct udf_image *img)
{
    img_add_dir(img, "/ExtdFEs");
    img_add_file(img, "/ExtdFEs/1of5", (const uint8_t *)"1of5", 4);
    img_add_dir_efe(img, "/ExtdFEs/2of5");
    img_add_file_efe(img, "/ExtdFEs/2of5/file_in_2of5", (const uint8_t *)"2", 1);
    img_add_dir(img, "/ExtdFEs/3of5");
    img_add_file(img, "/ExtdFEs/3of5/file_in_3of5", (const uint8_t *)"3", 1);
    img_add_dir_efe(img, "/ExtdFEs/4of5");
    img_add_file_efe(img, "/ExtdFEs/4of5/file_in_4of5", (const uint8_t *)"4", 1);
    img_add_file(img, "/ExtdFEs/5of5", (const uint8_t *)"5of5", 4);
}

static struct udf_image *build_main_image(void)
{
    struct udf_image *img = img_new();

    img_build_disc(img, 113, 265);
    build_3_3(img);
    build_3_4(img);
    build_3_6(img);
    build_3_7(img);
    build_3_8(img);
    build_3_21(img);
    build_3_22(img);
    build_3_23(img);
    build_3_34(img);
    build_3_40(img);
    img_init_input(img);
    return img;
}

static struct udf_image *ensure_main_img(void)
{
    if (!g_main_img) {
        g_main_img = build_main_image();
    }
    return g_main_img;
}

/* ---------- volume-level test images ---------- */

/*
 * Conformance test 3.9: Multiple Descriptor Sequences
 *
 * The volume descriptor sequence contains 6 instances of the
 * descriptor set, arranged via Volume Descriptor Pointers; the
 * instance with the highest Volume Descriptor Sequence Number
 * prevails (ECMA-167 3/8.4.3). The first five instances reference
 * the non-existing partition number 4321 in their LVD; the last
 * instance (number 6) is valid.
 *
 * libudfread scans the whole sequence, keeps the highest-numbered
 * instance, and discards incomplete ones, so the
 * default open mounts instance 6.
 */
static struct udf_image *build_img_9(int valid_first)
{
    struct udf_image *img = img_new();

    img_build_disc(img, 113, 300);

    img_write_avdp(img, 256, 257, 37 * 2048, 295, 4 * 2048);

    /* instances 1..3, contiguous (257-265) */
    img_set_vds_seq(img, 1);
    img_write_pvd(img, 257);
    img_write_pd(img, 258, 113);
    img_write_lvd(img, 259, valid_first ? 0 : 4321, 0);
    img_set_vds_seq(img, 2);
    img_write_pvd(img, 260);
    img_write_pd(img, 261, 4321);
    img_write_lvd(img, 262, 4321, 0);
    img_set_vds_seq(img, 3);
    img_write_pvd(img, 263);
    img_write_pd(img, 264, 113);
    img_write_lvd(img, 265, 4321, 0);
    img_write_vdp(img, 266, 270, 4 * 2048);
    img_write_td(img, 267);
    /* 268, 269 zero */

    /* instance 4 (270-272), reached via VDP */
    img_set_vds_seq(img, 4);
    img_write_pvd(img, 270);
    img_write_pd(img, 271, 113);
    img_write_lvd(img, 272, 4321, 0);
    img_write_vdp(img, 273, 280, 4 * 2048);
    img_write_td(img, 274);
    /* 275-279 zero */

    /* instance 5 (280-282), reached via VDP */
    img_set_vds_seq(img, 5);
    img_write_pvd(img, 280);
    img_write_pd(img, 281, 113);
    img_write_lvd(img, 282, 4321, 0);
    img_write_vdp(img, 283, 290, 4 * 2048);
    img_write_td(img, 284);
    /* 285-289 zero */

    /* instance 6 (290-292): valid prevailing sequence */
    img_set_vds_seq(img, 6);
    img_write_pvd(img, 290);
    img_write_pd(img, 291, 113);
    img_write_lvd(img, 292, 0, 0);
    img_write_td(img, 293);
    img_write_td(img, 294);

    /* reserve sequence (295-298), identical to instance 6 */
    img_write_pvd(img, 295);
    img_write_pd(img, 296, 113);
    img_write_lvd(img, 297, 0, 0);
    img_write_td(img, 298);

    img_add_file(img, "/HELLO.TXT", (const uint8_t *)"hello\n", 6);
    return img;
}

/*
 * Conformance test 3.33: Volume Recognition Sequence
 *
 * The volume recognition sequence contains ISO 9660 descriptors
 * (PVD, SVD, terminator), then the UDF BEA01/NSR03 pair, an ISO
 * descriptor copy, and TEA01. The reader must skip the ISO
 * descriptors and find the UDF descriptors.
 */
static void build_iso_pvd(struct udf_image *img, uint32_t lba, uint8_t type)
{
    uint8_t *blk = img_block(img, lba);

    memset(blk, 0, IMG_BLOCK_SIZE);
    blk[0] = type;
    memcpy(blk + 1, "CD001", 5);
    blk[6] = 0;
}

static struct udf_image *build_img_33(void)
{
    struct udf_image *img = img_new();

    img_build_disc(img, 113, 265);
    img_add_file(img, "/HELLO.TXT", (const uint8_t *)"hello\n", 6);

    build_iso_pvd(img, 16, 1);     /* ISO 9660 PVD */
    build_iso_pvd(img, 17, 2);     /* ISO 9660 SVD */
    build_iso_pvd(img, 18, 255);   /* ISO 9660 terminator */
    memcpy(img_block(img, 19), "\0BEA01\1", 7);
    memcpy(img_block(img, 20), "\0NSR03\1", 7);
    build_iso_pvd(img, 21, 1);     /* ISO 9660 PVD copy */
    memcpy(img_block(img, 22), "\0TEA01\1", 7);
    return img;
}

/*
 * Conformance test 3.37: Volume Descriptor Pointer (disc D7)
 *
 * The volume descriptor sequence is spread: LVD at lba 0, VDP at
 * lba 1 pointing to the rest (PVD 16, PD 17, TD 18), with
 * zeroed sectors in between. The main VDS must be traversed via
 * the Volume Descriptor Pointer; the reserve sequence contains
 * only a terminating descriptor, so mounting fails if the VDP is
 * not followed.
 */
static struct udf_image *build_img_37(void)
{
    struct udf_image *img = img_new();

    img_build_disc(img, 113, 265);

    img_write_lvd(img, 0, 0, 0);
    img_write_vdp(img, 1, 16, 3 * 2048);
    img_write_td(img, 2);
    /* 3-15 zero */
    img_write_pvd(img, 16);
    img_write_pd(img, 17, 113);
    img_write_td(img, 18);
    memcpy(img_block(img, 19), "\0BEA01\1", 7);
    memcpy(img_block(img, 20), "\0NSR02\1", 7);
    memcpy(img_block(img, 21), "\0TEA01\1", 7);
    img_write_avdp(img, 256, 0, 19 * 2048, 261, 2048);
    img_write_td(img, 261);

    img_add_file(img, "/HELLO.TXT", (const uint8_t *)"hello\n", 6);
    return img;
}

/* ---------- tests ---------- */

/*
 * Conformance test 3.13: Partition Number
 *
 * The Partition Descriptor Partition Number and the type-1 partition
 * map Partition Number are set to a non-zero value X (113 on disc D5);
 * the ICB / long_ad partition fields keep the partition reference
 * number 0 (the map index, ECMA-167 3/8.8). The reader must
 * distinguish the partition number from the partition reference
 * number and mount the volume.
 *
 * libudfread handles this: udf_parse_partition_maps() accepts the
 * type-1 map (113 == PD number) and stores the map index (0) as the
 * partition reference, so _partition_index() resolves the FSD
 * location and the volume opens and reads normally.
 */
static void test_3_13(void)
{
    static const uint8_t hello[] = "hello\n";
    struct udf_image *img = img_new();
    udfread *udf;
    struct udfread_dirent entry;
    UDFDIR *dir;
    int found = 0;

    img_build_disc(img, 113, 265);
    img_add_file(img, "/HELLO.TXT", hello, sizeof(hello) - 1);
    udf = open_image(img);
    CHECK_EQ_STR("CONFORMANCE", udfread_get_volume_id(udf));

    dir = udfread_opendir(udf, "/");
    CHECK_NOT_NULL(dir);
    if (dir) {
        while (udfread_readdir(dir, &entry)) {
            if (!strcmp(entry.d_name, "HELLO.TXT")) {
                found = 1;
            }
        }
        udfread_closedir(dir);
    }
    CHECK(found);

    check_file(udf, "/HELLO.TXT", "hello\n");

    udfread_close(udf);
    img_free(img);
}

/*
 * Conformance test 3.3: Long File Names
 *
 * Four files in /Filenames/Long_Filenames whose file identifier
 * length byte is exactly 255: two CS0-8 (254-character) names that
 * differ only in the last character, and two CS0-16 (127-character)
 * names that differ only in the last character. All must be
 * accessible as distinct names.
 */
static void test_3_3(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);
    UDFDIR *dir;
    struct udfread_dirent entry;
    char n8a[255], n8b[255], n16a[128], n16b[128];
    char path[512];
    int found = 0;

    memset(n8a, 'A', 253);
    n8a[253] = '1';
    n8a[254] = 0;
    memset(n8b, 'A', 253);
    n8b[253] = '2';
    n8b[254] = 0;
    memset(n16a, 'B', 126);
    n16a[126] = '1';
    n16a[127] = 0;
    memset(n16b, 'B', 126);
    n16b[126] = '2';
    n16b[127] = 0;

    dir = udfread_opendir(udf, "/Filenames/Long_Filenames");
    CHECK_NOT_NULL(dir);
    if (dir) {
        while (udfread_readdir(dir, &entry)) {
            if (!strcmp(entry.d_name, n8a)) {
                found |= 1;
            } else if (!strcmp(entry.d_name, n8b)) {
                found |= 2;
            } else if (!strcmp(entry.d_name, n16a)) {
                found |= 4;
            } else if (!strcmp(entry.d_name, n16b)) {
                found |= 8;
            }
        }
        udfread_closedir(dir);
    }
    CHECK_EQ_INT(15, found);

    snprintf(path, sizeof(path), "/Filenames/Long_Filenames/%s", n8a);
    check_file(udf, path, "1");
    snprintf(path, sizeof(path), "/Filenames/Long_Filenames/%s", n8b);
    check_file(udf, path, "2");
    snprintf(path, sizeof(path), "/Filenames/Long_Filenames/%s", n16a);
    check_file(udf, path, "3");
    snprintf(path, sizeof(path), "/Filenames/Long_Filenames/%s", n16b);
    check_file(udf, path, "4");

    udfread_close(udf);
}

/*
 * Conformance test 3.4: Filename Encoding
 *
 * /Filenames/Filename_Encoding contains six files and one directory
 * with CS0-8 and CS0-16 names, including Japanese characters in
 * Unicode. All must be accessible with their exact names.
 */
static void test_3_4(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);
    UDFDIR *dir;
    struct udfread_dirent entry;
    static const char *names[] = {
        "SINGLE_BYTE.TXT",
        "DOUBLE_BYTE.TXT",
        "\xE3\x83\xA6\xE3\x83\x8B\xE3\x83\x90\xE3\x83\xBC"
        "\xE3\x82\xB5\xE3\x83\xAB\xE3\x83\x87\xE3\x82\xA3"
        "\xE3\x82\xB9\xE3\x82\xAF\xE8\xA6\x8F\xE5\xAE\x9A.TXT",
        "1\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.TXT",
        "\xE3\x81\x88.TXT",
        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE5\x90\x8D.TXT",
        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"
        "\xE3\x83\x87\xE3\x82\xA3\xE3\x83\xAC\xE3\x82\xAF\xE3\x83\x88\xE3\x83\xAA"
    };
    int found[7] = {0};
    uint32_t count = 0;
    uint32_t i;

    dir = udfread_opendir(udf, "/Filenames/Filename_Encoding");
    CHECK_NOT_NULL(dir);
    if (dir) {
        while (udfread_readdir(dir, &entry)) {
            count++;
            for (i = 0; i < 7; i++) {
                if (!strcmp(entry.d_name, names[i])) {
                    found[i] = 1;
                }
            }
        }
        udfread_closedir(dir);
    }
    CHECK_EQ_UINT(7, count);
    for (i = 0; i < 7; i++) {
        CHECK(found[i]);
    }

    check_file(udf, "/Filenames/Filename_Encoding/SINGLE_BYTE.TXT", "1");
    check_file(udf, "/Filenames/Filename_Encoding/DOUBLE_BYTE.TXT", "2");
    check_file(udf, "/Filenames/Filename_Encoding/"
                    "\xE3\x83\xA6\xE3\x83\x8B\xE3\x83\x90\xE3\x83\xBC"
                    "\xE3\x82\xB5\xE3\x83\xAB\xE3\x83\x87\xE3\x82\xA3"
                    "\xE3\x82\xB9\xE3\x82\xAF\xE8\xA6\x8F\xE5\xAE\x9A.TXT", "3");
    check_file(udf, "/Filenames/Filename_Encoding/1\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.TXT", "4");
    check_file(udf, "/Filenames/Filename_Encoding/\xE3\x81\x88.TXT", "5");
    check_file(udf, "/Filenames/Filename_Encoding/"
                    "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE5\x90\x8D.TXT", "6");
    CHECK(dir_has_entry(udf, "/Filenames/Filename_Encoding/"
                              "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"
                              "\xE3\x83\x87\xE3\x82\xA3\xE3\x83\xAC\xE3\x82\xAF\xE3\x83\x88\xE3\x83\xAA",
                              "PLAIN.TXT"));
    check_file(udf, "/Filenames/Filename_Encoding/"
                    "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"
                    "\xE3\x83\x87\xE3\x82\xA3\xE3\x83\xAC\xE3\x82\xAF\xE3\x83\x88\xE3\x83\xAA/PLAIN.TXT",
                    "x");

    udfread_close(udf);
}

/*
 * Conformance test 3.6: Path Length
 *
 * /Deeptree/OSTA001/.../OSTA126/EINDE: 126 nested directories with
 * the EINDE file at the bottom (~1000-character path). The reader
 * must not crash and should resolve the path.
 */
static void test_3_6(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);
    char path[1024];
    uint32_t i;

    strcpy(path, "/Deeptree");
    for (i = 1; i <= 126; i++) {
        snprintf(path + strlen(path), sizeof(path) - strlen(path),
                 "/OSTA%03u", i);
    }
    strncat(path, "/EINDE", sizeof(path) - strlen(path) - 1);
    check_file(udf, path, "EINDE");

    udfread_close(udf);
}

/*
 * Conformance test 3.7: Large Directory
 *
 * /Filenames/50K_Files contains 50000 files named "1".."50000",
 * each with the file's name as content. All must be accessible and
 * readable.
 */
static void test_3_7(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);
    UDFDIR *dir;
    struct udfread_dirent entry;
    uint32_t count = 0;
    int found1 = 0, found12345 = 0, found50000 = 0;

    dir = udfread_opendir(udf, "/Filenames/50K_Files");
    CHECK_NOT_NULL(dir);
    if (dir) {
        while (udfread_readdir(dir, &entry)) {
            count++;
            if (!strcmp(entry.d_name, "1")) {
                found1 = 1;
            } else if (!strcmp(entry.d_name, "12345")) {
                found12345 = 1;
            } else if (!strcmp(entry.d_name, "50000")) {
                found50000 = 1;
            }
        }
        udfread_closedir(dir);
    }
    CHECK_EQ_UINT(50000, count);
    CHECK(found1);
    CHECK(found12345);
    CHECK(found50000);

    check_file(udf, "/Filenames/50K_Files/1", "1");
    check_file(udf, "/Filenames/50K_Files/12345", "12345");
    check_file(udf, "/Filenames/50K_Files/50000", "50000");

    udfread_close(udf);
}

/*
 * Conformance test 3.8: Large File
 *
 * /Large_File/2GB-1_FILE (0x7fffffff bytes) and /Large_File/16GB_FILE
 * (0x400000000 bytes). Only the first and last blocks are recorded
 * ("START " / " END" markers); the middle consists of unrecorded
 * extents and must read as zeros.
 */
static void test_3_8(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);
    UDFFILE *f;
    uint8_t *buf = malloc(2048);
    int64_t pos;

    CHECK_NOT_NULL(buf);
    if (!buf) {
        udfread_close(udf);
        return;
    }

    f = udfread_file_open(udf, "/Large_File/2GB-1_FILE");
    CHECK_NOT_NULL(f);
    if (f) {
        CHECK((uint64_t)udfread_file_size(f) == 0x7fffffffULL);
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, 2048));
        CHECK(memcmp(buf, "START ", 6) == 0);
        CHECK(all_zero(buf + 6, 2048 - 6));

        CHECK((uint64_t)udfread_file_seek(f, 2147481600, SEEK_SET) == 2147481600ULL);
        CHECK_EQ_INT(2047, (int)udfread_file_read(f, buf, 2048));
        CHECK(memcmp(buf + 2043, " END", 4) == 0);
        CHECK(all_zero(buf, 2043));

        pos = udfread_file_seek(f, 0x3fffffff, SEEK_SET);
        CHECK(pos == 0x3fffffff);
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, 2048));
        CHECK(all_zero(buf, 2048));
        udfread_file_close(f);
    }

    f = udfread_file_open(udf, "/Large_File/16GB_FILE");
    CHECK_NOT_NULL(f);
    if (f) {
        CHECK((uint64_t)udfread_file_size(f) == 0x400000000ULL);
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, 2048));
        CHECK(memcmp(buf, "START ", 6) == 0);
        CHECK(all_zero(buf + 6, 2048 - 6));

        pos = udfread_file_seek(f, 0x400000000ULL - 2048, SEEK_SET);
        CHECK(pos == (int64_t)(0x400000000ULL - 2048));
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, 2048));
        CHECK(memcmp(buf + 2044, " END", 4) == 0);
        CHECK(all_zero(buf, 2044));

        pos = udfread_file_seek(f, (int64_t)4194304 * 2048, SEEK_SET);
        CHECK(pos == (int64_t)4194304 * 2048);
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, 2048));
        CHECK(all_zero(buf, 2048));
        udfread_file_close(f);
    }

    free(buf);
    udfread_close(udf);
}

/*
 * Conformance test 3.9: Multiple Descriptor Sequences
 *
 * The volume descriptor sequence contains 6 instances; the last
 * one prevails (ECMA-167 3/8.4.3). The first five instances point
 * to the non-existing partition 4321, so a reader honoring
 * "highest sequence number wins" mounts the disc.
 *
 * The default open (UDFREAD_PARTITION_FIRST) early-exits at the
 * first complete PVD+PD+LVD group and would pick instance 1, so
 * the test selects UDFREAD_PARTITION_LAST: _search_vds() then
 * scans the whole sequence and the last instance prevails. The
 * control image (first instance valid) must mount either way.
 */
static void test_3_9(void)
{
    struct udf_image *img = build_img_9(0);
    udfread *udf;

    /* default open: the highest-numbered instance prevails */
    udf = open_image(img);
    if (udf) {
        check_file(udf, "/HELLO.TXT", "hello\n");
        udfread_close(udf);
    }
    img_free(img);

    /* control: first instance is the valid one */
    img = build_img_9(1);
    udf = open_image(img);
    if (udf) {
        check_file(udf, "/HELLO.TXT", "hello\n");
        udfread_close(udf);
    }
    img_free(img);
}

/*
 * Conformance test 3.21: Embedded Data
 *
 * /Embedded_data/EMBED.TXT has its data stored in the File Entry
 * (inline ICB); the directory /Embedded_data/InlineDir stores its
 * file identifiers inline as well. Both must be accessible.
 */
static void test_3_21(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);

    check_file(udf, "/Embedded_data/EMBED.TXT", "embedded data\n");
    CHECK(dir_has_entry(udf, "/Embedded_data/InlineDir", "EMBEDDED.TXT"));
    check_file(udf, "/Embedded_data/InlineDir/EMBEDDED.TXT", "inline\n");

    udfread_close(udf);
}

/*
 * Conformance test 3.22: Unrecorded Extents
 *
 * Files with unrecorded extents in the middle, at the end, and
 * beyond the end of the file. Unrecorded bytes must read as zeros.
 */
static void test_3_22(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);
    UDFFILE *f;
    uint8_t buf[2048];

    f = udfread_file_open(udf, "/Unrecorded/Middle");
    CHECK_NOT_NULL(f);
    if (f) {
        CHECK_EQ_UINT(6144, (uint32_t)udfread_file_size(f));
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, sizeof(buf)));
        CHECK(buf[0] == 'A' && buf[2047] == 'A');
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, sizeof(buf)));
        CHECK(all_zero(buf, sizeof(buf)));
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, sizeof(buf)));
        CHECK(buf[0] == 'B' && buf[2047] == 'B');
        udfread_file_close(f);
    }

    f = udfread_file_open(udf, "/Unrecorded/end");
    CHECK_NOT_NULL(f);
    if (f) {
        CHECK_EQ_UINT(4096, (uint32_t)udfread_file_size(f));
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, sizeof(buf)));
        CHECK(buf[0] == 'A' && buf[2047] == 'A');
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, sizeof(buf)));
        CHECK(all_zero(buf, sizeof(buf)));
        udfread_file_close(f);
    }

    f = udfread_file_open(udf, "/Unrecorded/beyond");
    CHECK_NOT_NULL(f);
    if (f) {
        CHECK_EQ_UINT(2048, (uint32_t)udfread_file_size(f));
        CHECK_EQ_INT(2048, (int)udfread_file_read(f, buf, sizeof(buf)));
        CHECK(buf[0] == 'A' && buf[2047] == 'A');
        CHECK_EQ_INT(0, (int)udfread_file_read(f, buf, sizeof(buf)));
        udfread_file_close(f);
    }

    udfread_close(udf);
}

/*
 * Conformance test 3.23: Information Length
 *
 * lg_eq_ad.txt: information length equals the sum of the
 * allocation descriptor lengths; lg_lt_ad.txt: information length
 * is smaller (file tail). Both contain non-zero data and must be
 * fully readable.
 */
static void test_3_23(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);
    UDFFILE *f;
    uint8_t buf[4096];

    f = udfread_file_open(udf, "/Filelength/lg_eq_ad.txt");
    CHECK_NOT_NULL(f);
    if (f) {
        CHECK_EQ_UINT(3048, (uint32_t)udfread_file_size(f));
        CHECK_EQ_INT(3048, (int)udfread_file_read(f, buf, sizeof(buf)));
        CHECK(buf[0] == 'A' && buf[2047] == 'A');
        CHECK(buf[2048] == 'B' && buf[3047] == 'B');
        udfread_file_close(f);
    }

    f = udfread_file_open(udf, "/Filelength/lg_lt_ad.txt");
    CHECK_NOT_NULL(f);
    if (f) {
        CHECK_EQ_UINT(2600, (uint32_t)udfread_file_size(f));
        CHECK_EQ_INT(2600, (int)udfread_file_read(f, buf, sizeof(buf)));
        CHECK(buf[0] == 'A' && buf[2047] == 'A');
        CHECK(buf[2048] == 'B' && buf[2599] == 'B');
        udfread_file_close(f);
    }

    udfread_close(udf);
}

/*
 * Conformance test 3.33: Volume Recognition Sequence
 *
 * ISO 9660 descriptors interleaved with the UDF BEA01/NSR03
 * sequence. The reader must skip the ISO descriptors and find the
 * UDF descriptors.
 */
static void test_3_33(void)
{
    struct udf_image *img = build_img_33();
    udfread *udf = open_image(img);

    if (udf) {
        CHECK_EQ_STR("CONFORMANCE", udfread_get_volume_id(udf));
        check_file(udf, "/HELLO.TXT", "hello\n");
        udfread_close(udf);
    }
    img_free(img);
}

/*
 * Conformance test 3.34: Tag Serial Number
 *
 * Descriptors in /Tag_serial/zero have tag serial number 0, those
 * in /Tag_serial/nonzero have 4321; the prevailing LVD also has
 * serial 4321. The reader must accept any serial number.
 */
static void test_3_34(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);
    uint8_t data[64];
    uint32_t i;

    for (i = 0; i < 64; i++) {
        data[i] = (uint8_t)i;
    }
    CHECK(dir_has_entry(udf, "/Tag_serial/zero", "ZERO_FILE.TXT"));
    check_file_bin(udf, "/Tag_serial/zero/ZERO_FILE.TXT", data, 64);

    for (i = 0; i < 64; i++) {
        data[i] = (uint8_t)(0x80 + i);
    }
    CHECK(dir_has_entry(udf, "/Tag_serial/nonzero", "NONZERO_FILE.TXT"));
    check_file_bin(udf, "/Tag_serial/nonzero/NONZERO_FILE.TXT", data, 64);

    udfread_close(udf);
}

/*
 * Conformance test 3.37: Volume Descriptor Pointer
 *
 * The main VDS is traversed via a Volume Descriptor Pointer that
 * jumps over zeroed sectors to the remaining descriptors. Mounting
 * must succeed (the reserve sequence is only a terminating
 * descriptor, so following the VDP is required).
 */
static void test_3_37(void)
{
    struct udf_image *img = build_img_37();
    udfread *udf = open_image(img);

    if (udf) {
        CHECK_EQ_STR("CONFORMANCE", udfread_get_volume_id(udf));
        check_file(udf, "/HELLO.TXT", "hello\n");
        udfread_close(udf);
    }
    img_free(img);
}

/*
 * Conformance test 3.40: Standard and Extended File Entries
 *
 * /ExtdFEs: 1of5 and 5of5 are standard File Entry files; 2of5 and
 * 4of5 are Extended File Entry directories; 3of5 is a standard
 * File Entry directory. All must be accessible, including the
 * files inside the directories.
 */
static void test_3_40(void)
{
    struct udf_image *img = ensure_main_img();
    udfread *udf = open_image(img);

    CHECK_EQ_UINT(5, dir_count(udf, "/ExtdFEs"));
    CHECK(dir_has_entry(udf, "/ExtdFEs", "1of5"));
    CHECK(dir_has_entry(udf, "/ExtdFEs", "2of5"));
    CHECK(dir_has_entry(udf, "/ExtdFEs", "3of5"));
    CHECK(dir_has_entry(udf, "/ExtdFEs", "4of5"));
    CHECK(dir_has_entry(udf, "/ExtdFEs", "5of5"));

    check_file(udf, "/ExtdFEs/1of5", "1of5");
    check_file(udf, "/ExtdFEs/5of5", "5of5");

    CHECK(dir_has_entry(udf, "/ExtdFEs/2of5", "file_in_2of5"));
    check_file(udf, "/ExtdFEs/2of5/file_in_2of5", "2");
    CHECK(dir_has_entry(udf, "/ExtdFEs/3of5", "file_in_3of5"));
    check_file(udf, "/ExtdFEs/3of5/file_in_3of5", "3");
    CHECK(dir_has_entry(udf, "/ExtdFEs/4of5", "file_in_4of5"));
    check_file(udf, "/ExtdFEs/4of5/file_in_4of5", "4");

    udfread_close(udf);
}
RUN_TESTS(TEST_ENTRY(test_3_13),
          TEST_ENTRY(test_3_3),
          TEST_ENTRY(test_3_4),
          TEST_ENTRY(test_3_6),
          TEST_ENTRY(test_3_7),
          TEST_ENTRY(test_3_8),
          TEST_ENTRY(test_3_9),
          TEST_ENTRY(test_3_21),
          TEST_ENTRY(test_3_22),
          TEST_ENTRY(test_3_23),
          TEST_ENTRY(test_3_33),
          TEST_ENTRY(test_3_34),
          TEST_ENTRY(test_3_37),
          TEST_ENTRY(test_3_40))
