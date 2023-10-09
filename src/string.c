/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/string.h"

#include "pg/static_assert.h"
#include "pg/std.h"
#include "pg/dlog.h"

#define	LONGLONG_MIN	(-9223372036854775807LL-1)
#define	LONGLONG_MAX	9223372036854775807LL
#define ULONG_MAX 		4294967295UL
#define	ULONGLONG_MAX	18446744073709551615ULL

/* Nonzero if either X or Y is not aligned on a "long" boundary.  */
#define UNALIGNED(X, Y) \
  (((long)(X) & (sizeof (long) - 1)) | ((long)(Y) & (sizeof (long) - 1)))

/* Nonzero if X (a long int) contains a NULL byte. */
#define DETECTNULL(X) (((X) - 0x0101010101010101) & ~(X) & 0x8080808080808080)

#define TOO_SMALL(LEN) ((LEN) < sizeof (long))

void string_init_empty(struct string *str)
{
	static_assert(sizeof(str->data) >= 1, "String buffer too small");
	str->data[0] = '\0';
}

/**
 * Caller must guarantee that `data` points to a NULL-terminated string.
 * The constructor checks that it fits into the internal buffer and copies
 * the string there.
 */
enum string_return_code string_init(struct string *str,
					const struct memiter *data)
{
	const char *base = memiter_base(data);
	size_t size = memiter_size(data);

	/*
	 * Require that the value contains exactly one NULL character and that
	 * it is the last byte.
	 */
	if (size < 1 || memchr(base, '\0', size) != &base[size - 1]) {
		return STRING_ERROR_INVALID_INPUT;
	}

	if (size > sizeof(str->data)) {
		return STRING_ERROR_TOO_LONG;
	}

	memcpy_s(str->data, sizeof(str->data), base, size);
	return STRING_SUCCESS;
}

bool string_is_empty(const struct string *str)
{
	return str->data[0] == '\0';
}

const char *string_data(const struct string *str)
{
	return str->data;
}

/**
 * Returns true if the iterator `data` contains string `str`.
 * Only characters until the first null terminator are compared.
 */
bool string_eq(const struct string *str, const struct memiter *data)
{
	const char *base = memiter_base(data);
	size_t len = memiter_size(data);

	return (len <= sizeof(str->data)) &&
	       (strncmp(str->data, base, len) == 0);
}

char *string_ncpy(char * dst0, const char * src0, size_t count)
{
    char       *dst = dst0;
    const char *src = src0;
    long       *aligned_dst;
    const long *aligned_src;

    /* If SRC and DEST is aligned and count large enough, then copy words.  */
    if (!UNALIGNED(src, dst) && !TOO_SMALL(count)) {
        aligned_dst = (long *)dst;
        aligned_src = (long *)src;

        /* SRC and DEST are both "long int" aligned, try to do "long int"
	 sized copies.  */
        while (count >= sizeof(long int) && !DETECTNULL(*aligned_src)) {
            count -= sizeof(long int);
            *aligned_dst++ = *aligned_src++;
        }

        dst = (char *)aligned_dst;
        src = (char *)aligned_src;
    }

    while (count > 0) {
        --count;
        if ((*dst++ = *src++) == '\0')
            break;
    }

    while (count-- > 0)
        *dst++ = '\0';

    return dst0;
}

static int isspace_l (int c)
{
	return c == ' ' || (unsigned)c - '\t' < 5;
}


/*
 * Convert a string to an unsigned long integer.
 */
static unsigned long string_toul_l (const char * nptr, char ** endptr, uint16_t base)
{
	register const unsigned char *s = (const unsigned char *)nptr;
	register unsigned long acc;
	register unsigned char c;
	register unsigned long cutoff, cutlim;
	register bool neg = false;
	register int64_t any;

	do {
		c = *s++;
	} while (isspace_l(c));
	if (c == '-') {
		neg = true;
		c = *s++;
	} else if (c == '+')
		c = *s++;
	if ((base == 0 || base == 16) &&
	    c == '0' && (*s == 'x' || *s == 'X')) {
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;
	cutoff = ULONG_MAX / base;
	cutlim = ULONG_MAX % base;
	for (acc = 0, any = 0;; c = *s++) {
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'A' && c <= 'Z')
			c -= 'A' - 10;
		else if (c >= 'a' && c <= 'z')
			c -= 'a' - 10;
		else
			break;
		if (c >= base)
			break;
               if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
			any = -1;
		else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}
	if (any < 0) {
		acc = ULONG_MAX;
	} else if (neg)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char *) (any ? (char *)s - 1 : nptr);
	return (acc);
}

unsigned long string_toul(const char *s, char **ptr, int base)
{
    return string_toul_l(s, ptr, base);
}

#if HOST_TESTING_MODE || LOG_LEVEL >= LOG_LEVEL_INFO

static inline void sprintf_putchar(char *out, size_t size, uint64_t *idx, char c) {
	if (*idx < size - 1) {
		out[(*idx)++] = c;
	}
}

/**
 * Prints a raw string to the debug log and returns its length.
 */
static size_t sprint_raw_string(char *out, size_t size, uint64_t *idx, const char *str)
{
	const char *c = str;

	while (*c != '\0') {
		sprintf_putchar(out, size, idx, *c++);
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
static void sprint_string(char *out, size_t size, uint64_t *idx, const char *str, const char *suffix, size_t width,
			 int flags, char fill)
{
	size_t len = suffix - str;

	/* Print the string up to the beginning of the suffix. */
	while (str != suffix) {
		sprintf_putchar(out, size, idx, *str++);
	}

	if (flags & FLAG_MINUS) {
		/* Left-aligned. Print suffix, then print padding if needed. */
		len += sprint_raw_string(out, size, idx, suffix);
		while (len < width) {
			sprintf_putchar(out, size, idx, ' ');
			len++;
		}
		return;
	}

	/* Fill until we reach the desired length. */
	len += strnlen_s(suffix, DLOG_MAX_STRING_LENGTH);
	while (len < width) {
		sprintf_putchar(out, size, idx, fill);
		len++;
	}

	/* Now print the rest of the string. */
	sprint_raw_string(out, size, idx, suffix);
}


/**
 * Prints a number to the debug log. The caller specifies the base, its minimum
 * width and printf-style flags.
 */
static void sprint_num(char *out, size_t size, uint64_t *idx,  size_t v, size_t base, size_t width, int flags)
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
		sprint_string(out, size, idx, ptr, num, width, flags, '0');
	} else {
		sprint_string(out, size, idx, ptr, ptr, width, flags, ' ');
	}
}

/**
 * Same as "dlog", except that arguments are passed as a va_list
 */
static uint64_t vsprintf(char *out, size_t size, const char *fmt, va_list args)
{
	const char *p;
	size_t w;
	int flags;
	char buf[2];
	uint64_t idx = 0; // whenever putchar is called, we should increment this index - and check in putchar
	// char **out = &out_buf;

	for (p = fmt; *p && idx < size - 1; p++) {
		switch (*p) {
		default:
			sprintf_putchar(out, size, &idx, *p);
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

				sprint_string(out, size, &idx,  str, str, w, flags, ' ');
				p++;
			} break;

			case 'd':
			case 'i': {
				int v = va_arg(args, int);

				if (v < 0) {
					flags |= FLAG_NEG;
					v = -v;
				}

				sprint_num(out, size, &idx, (size_t)v, 10, w, flags);
				p++;
			} break;

			case 'X':
				flags |= FLAG_UPPER;
				sprint_num(out, size, &idx, va_arg(args, size_t), 16, w, flags);
				p++;
				break;

			case 'p':
				sprint_num(out, size, &idx, va_arg(args, size_t), 16,
					  sizeof(size_t) * 2, FLAG_ZERO);
				p++;
				break;

			case 'x':
				sprint_num(out, size, &idx, va_arg(args, size_t), 16, w, flags);
				p++;
				break;

			case 'u':
				sprint_num(out, size, &idx, va_arg(args, size_t), 10, w, flags);
				p++;
				break;

			case 'o':
				sprint_num(out, size, &idx, va_arg(args, size_t), 8, w, flags);
				p++;
				break;

			case 'c':
				buf[1] = 0;
				buf[0] = va_arg(args, int);
				sprint_string(out, size, &idx, buf, buf, w, flags, ' ');
				p++;
				break;

			case '%':
				break;

			default:
				sprintf_putchar(out, size, &idx, '%');
			}

			break;
		}
	}

	out[idx++] = '\0';

	return idx;
}

uint64_t string_snprintf(char *out, size_t size, const char *fmt, ...)
{
	va_list args;
	uint64_t ret;

	va_start(args, fmt);
	ret = vsprintf(out, size, fmt, args);
	va_end(args);

	return ret;
}
#else
inline uint64_t string_snprintf(char *out, size_t size, const char *fmt, ...)
{
	return 0;
}
#endif
