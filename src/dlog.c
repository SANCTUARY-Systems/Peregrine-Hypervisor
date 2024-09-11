/*
 * Copyright (c) 2023 SANCTUARY Systems GmbH
 *
 * This file is free software: you may copy, redistribute and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * For a commercial license, please contact SANCTUARY Systems GmbH
 * directly at info@sanctuary.dev
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *     Copyright 2018 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/dlog.h"

#include <stdbool.h>
#include <stddef.h>

#include "pg/spinlock.h"
#include "pg/std.h"
#include "pg/stdout.h"

/* Keep macro alignment */

static bool dlog_lock_enabled = false;
static struct spinlock sl = SPINLOCK_INIT;

/*
 * These global variables for the log buffer are not static because a test needs
 * to access them directly.
 */
#if defined HOST_TESTING_MODE && HOST_TESTING_MODE != 0
size_t dlog_buffer_offset;
char dlog_buffer[DLOG_BUFFER_SIZE];
#endif

/* dlog_lock - public dlog lock acquisition
 *
 * NOTE: this is used when we want to bypass dlog using stdout_putchar()
 */
void dlog_lock(void)
{
    if (dlog_lock_enabled) {
        sl_lock(&sl);
    }
}

/* dlog_unlock - public dlog lock release
 *
 * NOTE: this is used when we want to bypass dlog using stdout_putchar()
 */
void dlog_unlock(void)
{
    if (dlog_lock_enabled) {
        sl_unlock(&sl);
    }
}

/* maintain compatibility with previous API, where dlog locking was static */
static void lock(void)   __attribute__((alias("dlog_lock")));
static void unlock(void) __attribute__((alias("dlog_unlock")));

/**
 * Enables the lock protecting the serial device.
 */
void dlog_enable_lock(void)
{
	dlog_lock_enabled = true;
}

static void dlog_putchar(char c)
{
#if defined HOST_TESTING_MODE && HOST_TESTING_MODE != 0
	dlog_buffer[dlog_buffer_offset] = c;
	dlog_buffer_offset = (dlog_buffer_offset + 1) % DLOG_BUFFER_SIZE;
#endif
	stdout_putchar(c);
}

/**
 * Prints a raw string to the debug log and returns its length.
 */
static size_t print_raw_string(const char *str)
{
	const char *c = str;

	while (*c != '\0') {
		dlog_putchar(*c++);
	}

	return c - str;
}

/**
 * Prints a formatted string to the debug log. The format includes a minimum
 * width, the fill character, and flags (whether to align to left or right).
 *
 * str is the full string, while suffix is a pointer within str that indicates
 * where the suffix begins. This is used when printing right-aligned numbers
 * with a zero fill; for example, -10 with width 4 should be padded to -010,
 * so suffix would point to index one of the "-10" string .
 */
static void print_string(const char *str, const char *suffix, size_t width,
			 int flags, char fill)
{
	size_t len = suffix - str;

	/* Print the string up to the beginning of the suffix. */
	while (str != suffix) {
		dlog_putchar(*str++);
	}

	if (flags & FLAG_MINUS) {
		/* Left-aligned. Print suffix, then print padding if needed. */
		len += print_raw_string(suffix);
		while (len < width) {
			dlog_putchar(' ');
			len++;
		}
		return;
	}

	/* Fill until we reach the desired length. */
	len += strnlen_s(suffix, DLOG_MAX_STRING_LENGTH);
	while (len < width) {
		dlog_putchar(fill);
		len++;
	}

	/* Now print the rest of the string. */
	print_raw_string(suffix);
}

/**
 * Prints a number to the debug log. The caller specifies the base, its minimum
 * width and printf-style flags.
 */
static void print_num(size_t v, size_t base, size_t width, int flags)
{
	static const char *digits_lower = "0123456789abcdefx";
	static const char *digits_upper = "0123456789ABCDEFX";
	const char *d = (flags & FLAG_UPPER) ? digits_upper : digits_lower;
	char buf[DLOG_MAX_STRING_LENGTH];
	char *ptr = &buf[sizeof(buf) - 1];
	char *num;
	*ptr = '\0';
	do {
		--ptr;
		*ptr = d[v % base];
		v /= base;
	} while (v);

	/* Num stores where the actual number begins. */
	num = ptr;

	/* Add prefix if requested. */
	if (flags & FLAG_ALT) {
		switch (base) {
		case 16:
			ptr -= 2;
			ptr[0] = '0';
			ptr[1] = d[16];
			break;

		case 8:
			ptr--;
			*ptr = '0';
			break;
		}
	}

	/* Add sign if requested. */
	if (flags & FLAG_NEG) {
		*--ptr = '-';
	} else if (flags & FLAG_PLUS) {
		*--ptr = '+';
	} else if (flags & FLAG_SPACE) {
		*--ptr = ' ';
	}
	if (flags & FLAG_ZERO) {
		print_string(ptr, num, width, flags, '0');
	} else {
		print_string(ptr, ptr, width, flags, ' ');
	}
}

/**
 * Parses the optional flags field of a printf-style format. It returns the spot
 * on the string where a non-flag character was found.
 */
const char *parse_flags(const char *p, int *flags)
{
	for (;;) {
		switch (*p) {
		case ' ':
			*flags |= FLAG_SPACE;
			break;

		case '0':
			*flags |= FLAG_ZERO;
			break;

		case '-':
			*flags |= FLAG_MINUS;
			break;

		case '+':
			*flags |= FLAG_PLUS;

		case '#':
			*flags |= FLAG_ALT;
			break;

		default:
			return p;
		}
		p++;
	}
}

/**
 * Send the contents of the given VM's log buffer to the log, preceded by the VM
 * ID and followed by a newline.
 */
void dlog_flush_vm_buffer(uint16_t id, char buffer[], size_t length)
{
	lock();

	print_raw_string("VM ");
	print_num(id, 16, 0, 0);
	print_raw_string(": ");

	for (size_t i = 0; i < length; ++i) {
		dlog_putchar(buffer[i]);
		buffer[i] = '\0';
	}
	dlog_putchar('\n');

	unlock();
}

/**
 * Same as "dlog", except that arguments are passed as a va_list
 */
void vdlog(const char *fmt, va_list args)
{
	const char *p;
	size_t w;
	int flags;
	char buf[2];

	lock();

	for (p = fmt; *p; p++) {
		switch (*p) {
		default:
			dlog_putchar(*p);
			break;

		case '%':
			/* Read optional flags. */
			flags = 0;
			p = parse_flags(p + 1, &flags) - 1;

			/* Read the minimum width, if one is specified. */
			w = 0;
			while (p[1] >= '0' && p[1] <= '9') {
				w = (w * 10) + (p[1] - '0');
				p++;
			}

			/* Read minimum width from arguments. */
			if (w == 0 && p[1] == '*') {
				int v = va_arg(args, int);

				if (v >= 0) {
					w = v;
				} else {
					w = -v;
					flags |= FLAG_MINUS;
				}
				p++;
			}

			/* Handle the format specifier. */
			switch (p[1]) {
			case 's': {
				char *str = va_arg(args, char *);

				print_string(str, str, w, flags, ' ');
				p++;
			} break;

			case 'd':
			case 'i': {
				int v = va_arg(args, int);

				if (v < 0) {
					flags |= FLAG_NEG;
					v = -v;
				}

				print_num((size_t)v, 10, w, flags);
				p++;
			} break;

			case 'X':
				flags |= FLAG_UPPER;
				print_num(va_arg(args, size_t), 16, w, flags);
				p++;
				break;

			case 'p':
				print_num(va_arg(args, size_t), 16,
					  sizeof(size_t) * 2, FLAG_ZERO);
				p++;
				break;

			case 'x':
				print_num(va_arg(args, size_t), 16, w, flags);
				p++;
				break;

			case 'l':
				p++;
			case 'u':
				print_num(va_arg(args, size_t), 10, w, flags);
				p++;
				break;

			case 'o':
				print_num(va_arg(args, size_t), 8, w, flags);
				p++;
				break;

			case 'c':
				buf[1] = 0;
				buf[0] = va_arg(args, int);
				print_string(buf, buf, w, flags, ' ');
				p++;
				break;

			case '%':
				break;

			default:
				dlog_putchar('%');
			}

			break;
		}
	}

	unlock();
}

/**
 * Prints the given format string to the debug log.
 */
void dlog(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vdlog(fmt, args);
	va_end(args);
}
