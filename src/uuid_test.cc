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
    #include "pg/uuid.h"
    #include "pg/dlog.h"
}

namespace
{

class uuid : public ::testing::Test {
    protected:
    void SetUp() override {
        uuid_1.timeLow = 0x64F8A;
        uuid_1.timeMid = 0x0C5B;
        uuid_1.timeHiAndVersion = 0x1234;
        uuid_1.clockSeqAndNode[0] = 0xA0;
        uuid_1.clockSeqAndNode[1] = 0xA1;
        uuid_1.clockSeqAndNode[2] = 0xA2;
        uuid_1.clockSeqAndNode[3] = 0xA3;
        uuid_1.clockSeqAndNode[4] = 0xA4;
        uuid_1.clockSeqAndNode[5] = 0xA5;
        uuid_1.clockSeqAndNode[6] = 0xA6;
        uuid_1.clockSeqAndNode[7] = 0xA7;


        uuid_2.timeLow = 0xB7812C74;
        uuid_2.timeMid = 0xFF4F;
        uuid_2.timeHiAndVersion = 0x4321;
        uuid_2.clockSeqAndNode[0] = 0xB0;
        uuid_2.clockSeqAndNode[1] = 0xB1;
        uuid_2.clockSeqAndNode[2] = 0xB2;
        uuid_2.clockSeqAndNode[3] = 0xB3;
        uuid_2.clockSeqAndNode[4] = 0xB4;
        uuid_2.clockSeqAndNode[5] = 0xB5;
        uuid_2.clockSeqAndNode[6] = 0xB6;
        uuid_2.clockSeqAndNode[7] = 0xB7;
    }

    uuid_t uuid_1;
    uuid_t uuid_2;
};

TEST_F(uuid, uuid_equality)
{
    EXPECT_TRUE(uuid_is_equal(&uuid_1, &uuid_1));
    EXPECT_TRUE(uuid_is_equal(&uuid_2, &uuid_2));

    EXPECT_FALSE(uuid_is_equal(&uuid_1, &uuid_2));
    EXPECT_FALSE(uuid_is_equal(&uuid_2, &uuid_1));
}

TEST_F(uuid, uuid_uint64_conversion)
{
    uint64_t partial_uuid_1;
    uint64_t partial_uuid_2;
    uuid_t recovered_uuid;

    uuid_to_uint64(&uuid_1, &partial_uuid_1, &partial_uuid_2);

    /* Maybe check something here but not needed since partial UUIDs are        */
    /* only used as parameters in hypervisor calls. So as long as the recovered */
    /* UUID is equal to the original one this is all we want to achieve.        */

    uuid_from_uint64(partial_uuid_1, partial_uuid_2, &recovered_uuid);

    EXPECT_TRUE(uuid_is_equal(&uuid_1, &recovered_uuid));
}

TEST_F(uuid, uuid_from_str_1)
{
    char in_buf[] = "b7812c74-ff4f-4321-b0b1-b2b3b4b5b6b7";

    char out_test[UUID_STR_SIZE + 1];
    char out_test2[UUID_STR_SIZE + 1];
    uuid_t uuid_3;
    
    EXPECT_TRUE(uuid_from_str(in_buf, UUID_STR_SIZE, &uuid_3));
    uuid_to_str(&uuid_3, out_test2, sizeof(out_test2));

    uuid_to_str(&uuid_2, out_test, sizeof(out_test2));
    EXPECT_TRUE(uuid_is_equal(&uuid_2, &uuid_3));
}

TEST_F(uuid, uuid_from_str_2)
{
    char in_buf[] = "b7812c74-ff4f-4321-b0b1-b2b3b4b5b6b78"; /* longer strings are trimmed */
    uuid_t uuid_3;
    
    EXPECT_TRUE(uuid_from_str(in_buf, UUID_STR_SIZE, &uuid_3));
}

TEST_F(uuid, uuid_from_str_3)
{
    char in_buf[] = "b7812c74-ff4f-4321-b0b1-b2b3b4b5b6b";
    char empty_buf[] = ""; /* empty string */
    uuid_t uuid_3;
    
    EXPECT_FALSE(uuid_from_str(in_buf, sizeof(in_buf), &uuid_3)); /* size too small leads to "false" */

    EXPECT_FALSE(uuid_from_str(in_buf, UUID_STR_SIZE, &uuid_3));  /* shorter strings lead to "false" */

    EXPECT_FALSE(uuid_from_str(empty_buf, UUID_STR_SIZE, &uuid_3));  /* empty strings lead to "false" */
}

TEST_F(uuid, uuid_from_str_malformed)
{
    char in_buf[] = "!!!!!!!!-!!!!-!!!!-!!!!-!!!!!!!!!!!!"; // non-ASCII characters break the UUID parsing
    char expected[] = "00000000-0000-0000-0000-000000000000";
                                                
    uuid_t uuid_3;
    char out_test[UUID_STR_SIZE + 1] = {'A'};

    uuid_from_str(in_buf, UUID_STR_SIZE, &uuid_3); 
    uuid_to_str(&uuid_3, out_test, sizeof(out_test));

    EXPECT_STREQ(out_test, expected); /* malformed strings lead to blank UUID */
}


} /* namespace */
