/*
 * Copyright 2022 The Sanctuary Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include <gmock/gmock.h>

extern "C" {
#include "pg/std.h"
#include "pg/dlog.h"
}

namespace
{
TEST(std, memset)
{
    bool error_free = true;
	uint8_t *src_buf = (uint8_t *) malloc(4096);
    memset_unsafe(src_buf, 0xAAu, 4096);
    for (int i = 0; i < 4096; i++) {
        error_free &= src_buf[i] == 0xAAu;
    }

    EXPECT_TRUE(error_free);
}

TEST(std, memcpy)
{
    bool error_free = true;
	uint8_t *src_buf = (uint8_t *) malloc(4096);
    memset_unsafe(src_buf, 0xAAu, 4096);

    uint8_t *dst_buf = (uint8_t *) malloc(4096);

    memcpy_unsafe(dst_buf, src_buf, 4096);

    for (int i = 0; i < 4096; i++) {
        error_free &= dst_buf[i] == 0xAAu;
    }

    EXPECT_TRUE(error_free);
}

TEST(std, memmove)
{
    char str[] = "san-ctuary";
    memmove_unsafe(&str[3],&str[4],7);

    EXPECT_STREQ(str, "sanctuary");
}

} /* namespace */
