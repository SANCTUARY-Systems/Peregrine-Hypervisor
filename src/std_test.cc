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
