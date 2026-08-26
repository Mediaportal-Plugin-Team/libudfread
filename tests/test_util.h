/*
 * Minimal test framework for libudfread tests
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef TEST_UTIL_H_
#define TEST_UTIL_H_

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int test_failures;
static const char *test_subcase_name = NULL;

#define SET_SUBCASE(name) \
    do { test_subcase_name = (name); } while (0)

#define CLEAR_SUBCASE() \
    do { test_subcase_name = NULL; } while (0)

#define _FAIL_PREFIX \
    (test_subcase_name ? test_subcase_name : ""), \
    (test_subcase_name ? ": " : "")

#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s%s%s\n", __FILE__, __LINE__, _FAIL_PREFIX, #cond); test_failures++; } } while (0)

#define CHECK_NOT_NULL(p) CHECK((p) != NULL)

#define CHECK_EQ_UINT(exp, got) \
    do { uint32_t _e = (uint32_t)(exp), _g = (uint32_t)(got); \
         if (_e != _g) { printf("FAIL %s:%d: %s%s %s = %lu, expected %lu\n", __FILE__, __LINE__, _FAIL_PREFIX, #got, (unsigned long)_g, (unsigned long)_e); test_failures++; } } while (0)

#define CHECK_EQ_INT(exp, got) \
    do { int _e = (int)(exp), _g = (int)(got); \
         if (_e != _g) { printf("FAIL %s:%d: %s%s %s = %d, expected %d\n", __FILE__, __LINE__, _FAIL_PREFIX, #got, _g, _e); test_failures++; } } while (0)

#define CHECK_EQ_STR(exp, got) \
    do { const char *_e = (exp), *_g = (got); \
         if (!_e || !_g || strcmp(_e, _g) != 0) { printf("FAIL %s:%d: %s%s %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, _FAIL_PREFIX, #got, _g ? _g : "(null)", _e ? _e : "(null)"); test_failures++; } } while (0)

/* capture logger for ecma_ctx / udfread_set_log */
struct capture_log {
    char   buf[1024];
    size_t len;
    int    calls;
};

__attribute__((unused))
static int capture_logger(void *c, const char *fmt, ...)
{
    struct capture_log *cl = (struct capture_log *)c;
    va_list ap;
    int n;
    size_t room = sizeof(cl->buf) - cl->len;

    va_start(ap, fmt);
    n = vsnprintf(cl->buf + cl->len, room, fmt, ap);
    va_end(ap);
    if (n > 0) {
        cl->len += (size_t)n;
        if (cl->len > sizeof(cl->buf)) {
            cl->len = sizeof(cl->buf);
        }
    }
    cl->calls++;
    return n;
}

#define TEST_ENTRY(fn) { #fn, fn }

/* little-endian buffer writers (matches ecma167.h _get_u* byte order) */
static inline void set_u16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void set_u32(uint8_t *p, uint32_t v) { set_u16(p, v); set_u16(p + 2, v >> 16); }
static inline void set_u64(uint8_t *p, uint64_t v) { set_u32(p, (uint32_t)v); set_u32(p + 4, (uint32_t)(v >> 32)); }

/* write a 16-byte descriptor tag with valid checksum and version number */
static inline void set_tag(uint8_t *p, uint16_t id)
{
    int i;
    uint8_t sum = 0;

    memset(p, 0, 16);
    set_u16(p, id);
    set_u16(p + 2, 3);   /* descriptor version number */
    for (i = 0; i < 4; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    for (i = 5; i < 16; i++) {
        sum = (uint8_t)(sum + p[i]);
    }

    p[4] = sum;
}

/* recompute tag checksum after editing fields inside bytes 0..15 */
static inline void set_tag_checksum(uint8_t *p)
{
    int i;
    uint8_t sum = 0;

    for (i = 0; i < 4; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    for (i = 5; i < 16; i++) {
        sum = (uint8_t)(sum + p[i]);
    }

    p[4] = sum;
}

/* descriptor version number (ECMA 167, 3/7.2): 2 or 3 */
static inline void set_tag_version(uint8_t *p, uint16_t version)
{
    set_u16(p + 2, version);
    set_tag_checksum(p);
}

/* tag location: block address of the descriptor (ECMA 167, 3/7.2.5) */
static inline void set_tag_location(uint8_t *p, uint32_t lba)
{
    set_u32(p + 12, lba);
    set_tag_checksum(p);
}

/* CRC-16 over the descriptor (ECMA 167, 3/7.2.6):
 * polynomial x^16 + x^12 + x^5 + 1, initial value 0, MSB first.
 * Check vector from UDF 2.60: bytes #70 #6A #77 -> CRC #3299. */
static inline uint16_t tag_crc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;
    size_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* compute and store CRC over the first crc_len bytes following the tag */
static inline void set_tag_crc(uint8_t *p, uint16_t crc_len)
{
    set_u16(p + 8, tag_crc(p + 16, crc_len));
    set_u16(p + 10, crc_len);
    set_tag_checksum(p);
}

#define RUN_TESTS(...) \
    int main(void) \
    { \
        struct { const char *name; void (*fn)(void); } tests[] = { __VA_ARGS__ }; \
        size_t i; \
        int total = 0; \
        for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) { \
            test_failures = 0; \
            printf("test: %s\n", tests[i].name); \
            tests[i].fn(); \
            if (test_failures) { \
                printf("  %d failure(s)\n", test_failures); \
                total += test_failures; \
            } \
        } \
        return total ? 1 : 0; \
    }

#endif /* TEST_UTIL_H_ */

