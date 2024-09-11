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


#include "pg/cpio.h"
#include <stdint.h>
#include "pg/dlog.h"
#include "pg/std.h"
#define CPIO_OLD_BINARY_FORMAT_MAGIC 070707
#pragma pack(push, 1)
struct cpio_header {
	uint16_t magic;
	uint16_t dev;
	uint16_t ino;
	uint16_t mode;
	uint16_t uid;
	uint16_t gid;
	uint16_t nlink;
	uint16_t rdev;
	uint16_t mtime[2];
	uint16_t namesize;
	uint16_t filesize[2];
};
#pragma pack(pop)
/**
 * Retrieves the next file stored in the cpio archive stored in the cpio, and
 * advances the iterator such that another call to this function would return
 * the following file.
 */
static bool cpio_next(struct memiter *iter, const char **name,
		      const void **contents, size_t *size)
{
	static const char trailer[] = "TRAILER!!!";
	size_t len;
	struct memiter lit;
	const struct cpio_header *h;
	if (!iter) {
		return false;
	}
	lit = *iter;
	h = (const struct cpio_header *)lit.next;
	if (!h) {
		return false;
	}
	if (!memiter_advance(&lit, sizeof(struct cpio_header))) {
		return false;
	}
	if (h->magic != CPIO_OLD_BINARY_FORMAT_MAGIC) {
		dlog_error("cpio: only old binary format is supported\n");
		return false;
	}
	*name = lit.next;
	len = (h->namesize + 1) & ~1;
	if (!memiter_advance(&lit, len)) {
		return false;
	}
	/* previous memiter_advance checks for boundaries */
	if (h->namesize == 0U || (*name)[h->namesize - 1] != '\0') {
		return false;
	}
	*contents = lit.next;
	len = (size_t)h->filesize[0] << 16 | h->filesize[1];
	if (!memiter_advance(&lit, (len + 1) & ~1)) {
		return false;
	}
	/* Stop enumerating files when we hit the end marker. */
	if (!strncmp(*name, trailer, sizeof(trailer))) {
		return false;
	}
	*size = len;
	*iter = lit;
	return true;
}
/**
 * Looks for a file in the given cpio archive. The file, if found, is returned
 * in the "it" argument.
 */
bool cpio_get_file(const struct memiter *cpio, const struct string *name,
		   struct memiter *it)
{
	const char *fname;
	const void *fcontents;
	size_t fsize;
	struct memiter iter = *cpio;
	while (cpio_next(&iter, &fname, &fcontents, &fsize)) {
		if (!strncmp(fname, string_data(name), STRING_MAX_SIZE)) {
			memiter_init(it, fcontents, fsize);
			return true;
		}
	}
	return false;
}
