/*
 * Shared helpers for API-level tests (test_udfread_api.c, test_udf_conformance.c)
 * Static inline to avoid multiple-definition when included in multiple TUs.
 */

#ifndef TEST_API_H
#define TEST_API_H

#include "test_util.h"
#include "udf_image.h"
#include "udfread.h"

#include <stdlib.h>
#include <string.h>

static inline int all_zero(const uint8_t *buf, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (buf[i]) {
            return 0;
        }
    }
    return 1;
}

static inline udfread *open_image(struct udf_image *img)
{
    udfread *udf;

    img_init_input(img);
    udf = udfread_init();
    CHECK_NOT_NULL(udf);
    if (!udf) {
        return NULL;
    }
    CHECK_EQ_INT(0, udfread_open_input(udf, &img->bi));
    return udf;
}

static inline void check_file_bin(udfread *udf, const char *path,
                                  const uint8_t *data, uint32_t len)
{
    UDFFILE *f = udfread_file_open(udf, path);
    uint8_t buf[128];

    CHECK_NOT_NULL(f);
    if (!f) {
        return;
    }
    CHECK_EQ_UINT(len, (uint32_t)udfread_file_size(f));
    CHECK_EQ_UINT(len, (uint32_t)udfread_file_read(f, buf, sizeof(buf)));
    CHECK(memcmp(buf, data, len) == 0);
    udfread_file_close(f);
}

static inline void check_file(udfread *udf, const char *path, const char *content)
{
    check_file_bin(udf, path, (const uint8_t *)content, (uint32_t)strlen(content));
}

static inline uint32_t dir_count(udfread *udf, const char *path)
{
    UDFDIR *dir = udfread_opendir(udf, path);
    struct udfread_dirent entry;
    uint32_t count = 0;

    CHECK_NOT_NULL(dir);
    if (!dir) {
        return 0;
    }
    while (udfread_readdir(dir, &entry)) {
        count++;
    }
    udfread_closedir(dir);
    return count;
}

static inline int dir_has_entry(udfread *udf, const char *path, const char *name)
{
    UDFDIR *dir = udfread_opendir(udf, path);
    struct udfread_dirent entry;
    int found = 0;

    CHECK_NOT_NULL(dir);
    if (!dir) {
        return 0;
    }
    while (udfread_readdir(dir, &entry)) {
        if (!strcmp(entry.d_name, name)) {
            found = 1;
        }
    }
    udfread_closedir(dir);
    return found;
}

/* Build a standard test disc: partition 0 at LBA 100, with one file /FILE1.TXT */
static inline struct udf_image *new_std_disc(void)
{
    struct udf_image *img = img_new();
    img_build_disc(img, 0, 100);
    img_add_file(img, "/FILE1.TXT", (const uint8_t *)"hello", 5);
    return img;
}

/* Build a standard test disc with a custom file */
static inline struct udf_image *new_std_disc_with_file(const char *path,
                                                        const uint8_t *data, uint32_t len)
{
    struct udf_image *img = img_new();
    img_build_disc(img, 0, 100);
    img_add_file(img, path, data, len);
    return img;
}

#endif /* TEST_API_H */