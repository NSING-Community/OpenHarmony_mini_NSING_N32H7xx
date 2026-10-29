/*
 * Copyright (c) 2025 NSING Technologies Inc.
 * Licensed under the Apache License, Version 2.0 (the "License").
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file board_print.c
 * @brief Self-contained printf / snprintf / vprintf / PRINT_RELEASE /
 *        __libc_init_array for the early boot path, for LiteOS-M's own
 *        PRINTK/PRINT_ERR macros, and for the XTS (hctest) test framework.
 *
 * WHY THIS FILE EXISTS:
 *   LiteOS-M defines
 *       #define PRINTK(fmt, ...)  LOS_Printf(..., fmt, ...)
 *       #define LOS_Printf(...)   do { if (!OsLogLevelCheck(lv)) printf(fmt, ...); } while (0)
 *   i.e. the KERNEL's own logging goes through printf(). The musl libc built
 *   into this tree does provide vsnprintf/printf_core, but it is not usable in
 *   this -nostdlib bare-metal link: calling it hangs immediately (verified on
 *   hardware - the very first PRINTK in LOS_KernelInit never returns).
 *
 *   So the formatter is implemented here instead. Supported conversions:
 *       c s d i u x X p %
 *   with the '-' and '0' flags, decimal field width, a '.' precision that
 *   truncates %s, and the 'l' / 'll' / 'z' length modifiers.
 *
 *   The hctest framework used by XTS prints its progress with "%10s" and the
 *   result lines with "%s:%d:%s:PASS", so field width on %s matters.
 *
 *   All exported symbols are weak, so a real C library may still override them.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "uart_console.h"

#define BOARD_PRINT_BUF_SIZE 256

typedef struct {
    char   *buf;
    size_t  cap;   /* total buffer size including the NUL */
    size_t  len;   /* characters written so far (excl. NUL) */
} FmtOut;

static void FmtPutc(FmtOut *o, char c)
{
    if (o->len + 1 < o->cap) {
        o->buf[o->len] = c;
    }
    o->len++;
}

/* Emit a string honouring field width, left alignment and precision.
   `precision` < 0 means "no precision given". */
static void FmtPutStr(FmtOut *o, const char *s, int width, int leftAlign, int precision)
{
    int len = 0;
    int pad;
    int i;

    if (s == NULL) {
        s = "(null)";
    }
    while (s[len] != '\0') {
        len++;
    }
    if (precision >= 0 && len > precision) {
        len = precision;                    /* truncate, like "%.3s" */
    }

    pad = (width > len) ? (width - len) : 0;
    if (!leftAlign) {
        for (i = 0; i < pad; i++) {
            FmtPutc(o, ' ');
        }
    }
    for (i = 0; i < len; i++) {
        FmtPutc(o, s[i]);
    }
    if (leftAlign) {
        for (i = 0; i < pad; i++) {
            FmtPutc(o, ' ');
        }
    }
}

/* Emit `val` in `base` (10 or 16). `upper` selects A-F vs a-f.
   `width`/`zeroPad`/`leftAlign` implement %08x and %-6u style output. */
static void FmtPutNum(FmtOut *o, unsigned long long val, unsigned int base,
                      int upper, int width, int zeroPad, int leftAlign)
{
    char tmp[32];
    int  n = 0;
    int  pad;
    int  i;
    unsigned int digit;

    if (val == 0) {
        tmp[n++] = '0';
    } else {
        while (val != 0 && n < (int)sizeof(tmp)) {
            digit = (unsigned int)(val % base);
            tmp[n++] = (char)(digit < 10 ? ('0' + digit)
                                         : ((upper ? 'A' : 'a') + (digit - 10)));
            val /= base;
        }
    }

    pad = (width > n) ? (width - n) : 0;
    if (!leftAlign) {
        for (i = 0; i < pad; i++) {
            FmtPutc(o, zeroPad ? '0' : ' ');
        }
    }
    while (n > 0) {
        FmtPutc(o, tmp[--n]);
    }
    if (leftAlign) {
        for (i = 0; i < pad; i++) {
            FmtPutc(o, ' ');
        }
    }
}

static int BoardVsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    FmtOut o;

    o.buf = buf;
    o.cap = cap;
    o.len = 0;

    if (cap == 0) {
        return 0;
    }

    while (*fmt != '\0') {
        int leftAlign = 0;
        int zeroPad = 0;
        int width = 0;
        int precision = -1;
        int isLong = 0;
        int isLongLong = 0;

        if (*fmt != '%') {
            FmtPutc(&o, *fmt++);
            continue;
        }

        fmt++;  /* skip '%' */

        /* flags: only '-' (left align) and '0' (zero pad) are honoured */
        for (;;) {
            if (*fmt == '-') {
                leftAlign = 1;
                fmt++;
            } else if (*fmt == '0') {
                zeroPad = 1;
                fmt++;
            } else {
                break;
            }
        }

        /* field width */
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* precision: only used to truncate %s */
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* length modifier */
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                isLongLong = 1;
                fmt++;
            } else {
                isLong = 1;
            }
        } else if (*fmt == 'z') {
            fmt++;      /* size_t is 32-bit here: same width as unsigned int */
        }

        switch (*fmt) {
            case 'c':
                FmtPutc(&o, (char)va_arg(ap, int));
                break;
            case 's':
                FmtPutStr(&o, va_arg(ap, const char *), width, leftAlign, precision);
                break;
            case 'd':
            case 'i': {
                long long sv;

                if (isLongLong) {
                    sv = va_arg(ap, long long);
                } else if (isLong) {
                    sv = (long long)va_arg(ap, long);
                } else {
                    sv = (long long)va_arg(ap, int);
                }
                if (sv < 0) {
                    FmtPutc(&o, '-');
                    /* the sign already used one column */
                    FmtPutNum(&o, (unsigned long long)(-(sv + 1)) + 1ULL, 10, 0,
                              (width > 0) ? (width - 1) : 0, zeroPad, leftAlign);
                } else {
                    FmtPutNum(&o, (unsigned long long)sv, 10, 0, width, zeroPad, leftAlign);
                }
                break;
            }
            case 'u':
            case 'x':
            case 'X': {
                unsigned long long uv;

                if (isLongLong) {
                    uv = va_arg(ap, unsigned long long);
                } else if (isLong) {
                    uv = (unsigned long long)va_arg(ap, unsigned long);
                } else {
                    uv = (unsigned long long)va_arg(ap, unsigned int);
                }
                if (*fmt == 'u') {
                    FmtPutNum(&o, uv, 10, 0, width, zeroPad, leftAlign);
                } else {
                    FmtPutNum(&o, uv, 16, (*fmt == 'X'), width, zeroPad, leftAlign);
                }
                break;
            }
            case 'p':
                FmtPutStr(&o, "0x", 0, 0, -1);
                FmtPutNum(&o, (unsigned long long)(uintptr_t)va_arg(ap, void *),
                          16, 0, 0, 0, 0);
                break;
            case '%':
                FmtPutc(&o, '%');
                break;
            case '\0':
                fmt--;      /* trailing '%': stop cleanly */
                break;
            default:
                FmtPutc(&o, '%');
                FmtPutc(&o, *fmt);
                break;
        }

        if (*fmt != '\0') {
            fmt++;
        }
    }

    /* always NUL-terminate (truncated output is fine) */
    if (o.len < o.cap) {
        o.buf[o.len] = '\0';
    } else {
        o.buf[o.cap - 1] = '\0';
    }

    return (int)o.len;
}

/* Serve stdio's vsnprintf/snprintf/printf from the same self-contained core:
   these are the symbols musl would otherwise provide (and that hang). */
__attribute__((weak)) int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    return BoardVsnprintf(buf, cap, fmt, ap);
}

__attribute__((weak)) int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = BoardVsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

static int BoardVprint(const char *fmt, va_list ap)
{
    char buf[BOARD_PRINT_BUF_SIZE];
    int len = BoardVsnprintf(buf, sizeof(buf), fmt, ap);
    if (len < 0) {
        return len;
    }
    if (len > (int)sizeof(buf)) {
        len = (int)sizeof(buf);
    }
    uart_write(buf, (size_t)len);
    return len;
}

__attribute__((weak)) int printf(const char *fmt, ...)
{
    va_list ap;
    int len;
    va_start(ap, fmt);
    len = BoardVprint(fmt, ap);
    va_end(ap);
    return len;
}

__attribute__((weak)) int vprintf(const char *fmt, va_list ap)
{
    return BoardVprint(fmt, ap);
}

__attribute__((weak)) void PRINT_RELEASE(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)BoardVprint(fmt, ap);
    va_end(ap);
}

__attribute__((weak)) void __libc_init_array(void)
{
}
