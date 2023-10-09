/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include <stdarg.h>
#include <stddef.h>

#include "pg/ffa.h"

/* clang-format off */

#define FLAG_SPACE 0x01
#define FLAG_ZERO  0x02
#define FLAG_MINUS 0x04
#define FLAG_PLUS  0x08
#define FLAG_ALT   0x10
#define FLAG_UPPER 0x20
#define FLAG_NEG   0x40

#define DLOG_MAX_STRING_LENGTH 64

/* clang-format on */

#define DLOG_BUFFER_SIZE 8192

#define LOG_LEVEL_NONE UINT32_C(0)
#define LOG_LEVEL_ERROR UINT32_C(1)
#define LOG_LEVEL_NOTICE UINT32_C(2)
#define LOG_LEVEL_WARNING UINT32_C(3)
#define LOG_LEVEL_INFO UINT32_C(4)
#define LOG_LEVEL_DEBUG UINT32_C(5)
#define LOG_LEVEL_VERBOSE UINT32_C(6)

#if defined HOST_TESTING_MODE && HOST_TESTING_MODE != 0
extern size_t dlog_buffer_offset;
extern char dlog_buffer[];
#endif

/* dlog locking API */
void dlog_enable_lock(void);
void dlog_lock(void);
void dlog_unlock(void);

/* Helper functions */
const char *parse_flags(const char *p, int *flags);

/* dlog printing API */
void dlog(const char *fmt, ...);
void vdlog(const char *fmt, va_list args);

/* warpper macros for dlog levels */
#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define dlog_error(...) dlog("ERROR: " __VA_ARGS__)
#else
#define dlog_error(...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_NOTICE
#define dlog_notice(...) dlog("NOTICE: " __VA_ARGS__)
#else
#define dlog_notice(...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARNING
#define dlog_warning(...) dlog("WARNING: " __VA_ARGS__)
#else
#define dlog_warning(...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define dlog_info(...) dlog("INFO: " __VA_ARGS__)
#else
#define dlog_info(...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define dlog_debug(...) dlog("DEBUG: " __VA_ARGS__)
#else
#define dlog_debug(...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
#define dlog_verbose(...) dlog("VERBOSE: " __VA_ARGS__)
#else
#define dlog_verbose(...)
#endif

/* utility macros */
#define RET(assertion, code, msg...) \
    do {                             \
        if (assertion) {             \
            dlog_error(msg);         \
            return code;             \
        }                            \
    } while (0)

#define GOTO(assertion, label, msg...) \
    do {                               \
        if (assertion) {               \
            dlog_error(msg);           \
            goto label;                \
        }                              \
    } while (0)

#define ALERT(assertion, msg...) \
    do {                         \
        if (assertion) {         \
            dlog_warning(msg);   \
        }                        \
    } while (0)

/* public API */
void dlog_flush_vm_buffer(uint16_t id, char buffer[], size_t length);

