/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include <gmock/gmock.h>

extern "C" {
#include "pg/string.h"
}

namespace
{
TEST(string, valid)
{
	struct string str;
	struct memiter it;
	constexpr const char data[] = "test";

	string_init_empty(&str);
	ASSERT_TRUE(string_is_empty(&str));
	ASSERT_STREQ(string_data(&str), "");

	memiter_init(&it, data, sizeof(data));
	ASSERT_EQ(string_init(&str, &it), STRING_SUCCESS);
	ASSERT_FALSE(string_is_empty(&str));
	ASSERT_STRNE(string_data(&str), "");
	ASSERT_STREQ(string_data(&str), "test");
}

TEST(string, data_zero_size)
{
	struct string str;
	struct memiter it;
	constexpr const char data[] = "test";

	memiter_init(&it, data, 0);
	ASSERT_EQ(string_init(&str, &it), STRING_ERROR_INVALID_INPUT);
}

TEST(string, data_no_null_terminator)
{
	struct string str;
	struct memiter it;
	constexpr const char data[] = {'t', 'e', 's', 't'};

	memiter_init(&it, data, sizeof(data));
	ASSERT_EQ(string_init(&str, &it), STRING_ERROR_INVALID_INPUT);
}

TEST(string, data_two_null_terminators)
{
	struct string str;
	struct memiter it;
	constexpr const char data[] = {'\0', 't', 'e', 's', 't', '\0'};

	memiter_init(&it, data, sizeof(data));
	ASSERT_EQ(string_init(&str, &it), STRING_ERROR_INVALID_INPUT);
}

TEST(string, string_toul_ascii)
{
	char input[] = "12345";
	unsigned long output = 0;

	output = string_toul(input, (char **)NULL, 10);

	EXPECT_EQ(output, 12345U);
}

TEST(string, string_toul_hex)
{
	char input[] = "0x12345";
	unsigned long output = 0;

	output = string_toul(input, (char **)NULL, 16);

	EXPECT_EQ(output, 0x12345U);
}

} /* namespace */
