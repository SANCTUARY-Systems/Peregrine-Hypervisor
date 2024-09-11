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

#ifndef EXTERNAL_INCLUDE

#define MANIFEST_PRINT_WIDTH_FIX uint64_t
#include "pg/uuid.h"
#include "pg/std.h"
#include "pg/string.h"

#endif

/**
 * @brief compares two UUIDs
 * 
 * @param uuid_1 pointer to the first UUID
 * @param uuid_2 pointer to the second UUID
 * @return true if they are equal
 * @return false if they are different
 */
bool uuid_is_equal(uuid_t *uuid_1, uuid_t *uuid_2)
{
	return ((uuid_1->timeLow == uuid_2->timeLow) && (uuid_1->timeMid == uuid_2->timeMid)
			&& (uuid_1->timeHiAndVersion == uuid_2->timeHiAndVersion)
			&& (uuid_1->clockSeqAndNode[0] == uuid_2->clockSeqAndNode[0])
			&& (uuid_1->clockSeqAndNode[1] == uuid_2->clockSeqAndNode[1])
			&& (uuid_1->clockSeqAndNode[2] == uuid_2->clockSeqAndNode[2])
			&& (uuid_1->clockSeqAndNode[3] == uuid_2->clockSeqAndNode[3])
			&& (uuid_1->clockSeqAndNode[4] == uuid_2->clockSeqAndNode[4])
			&& (uuid_1->clockSeqAndNode[5] == uuid_2->clockSeqAndNode[5])
			&& (uuid_1->clockSeqAndNode[6] == uuid_2->clockSeqAndNode[6])
			&& (uuid_1->clockSeqAndNode[7] == uuid_2->clockSeqAndNode[7]));
}

/**
 * @brief Converts two 64-bit values that contain a uuid into a UUID struct
 * 
 * @param partial_uuid_1 first 64-bits of the UUID
 * @param partial_uuid_2 second 64-bits of the UUID
 * @param uuid pointer to the final UUID
 */
void uuid_from_uint64(uint64_t partial_uuid_1, uint64_t partial_uuid_2, uuid_t *uuid)
{
    /* Not sure if bitwise or is needed of if simply type-casting would be enough. */
    uuid->timeLow = (uint32_t) (partial_uuid_1 & 0xFFFFFFFF);
    uuid->timeMid = (uint16_t) ((partial_uuid_1 >> 32) & 0xFFFF);
    uuid->timeHiAndVersion = (uint16_t) (partial_uuid_1 >> 48);
    for (uint8_t i = 0; i < 8; i++) {
        uuid->clockSeqAndNode[i] = (uint8_t) ((partial_uuid_2 >> i*8) & 0xFF);
    }
}

/**
 * @brief Converts a UUID struct into two 64-bit values that contain a uuid
 * 
 * @param uuid pointer to the UUID
 * @param partial_uuid_1 pointer to the first 64-bits of the UUID
 * @param partial_uuid_2 pointer to the second 64-bits of the UUID
 */
void uuid_to_uint64(uuid_t *uuid, uint64_t *partial_uuid_1, uint64_t *partial_uuid_2)
{
    /* Not sure if bitwise or is needed of if simply type-casting would be enough. */
    *partial_uuid_1 = (uint64_t) uuid->timeLow;
    *partial_uuid_1 |= ((uint64_t) uuid->timeMid) << 32;
    *partial_uuid_1 |= ((uint64_t) uuid->timeHiAndVersion) << 48;
    *partial_uuid_2 = (uint64_t) uuid->clockSeqAndNode[0];
    for (uint8_t i = 1; i < 8; i++) {
        *partial_uuid_2 |= ((uint64_t) uuid->clockSeqAndNode[i]) << i*8;
    }
}

/*  */

/**
 * @brief Converts a UUID string to TEE_UUID
 * 
 * @param uuid_str string to convert
 * @param uuid_str_size size of the string
 * @param uuid output uuid_t
 * @return true if successful
 * @return false else
 */
bool uuid_from_str(char *uuid_str, size_t uuid_str_size, uuid_t *uuid)
{
	uint32_t time_data[3] = {0U, 0U, 0U};
	char temp_str[3];
	char temp_str_special[32];

	size_t time_count = 0;
	size_t seq_count = 0;
	char *substr_begin = &uuid_str[0];

	if (uuid_str_size != UUID_STR_SIZE || strnlen_s(uuid_str, UUID_STR_SIZE) != UUID_STR_SIZE) {
		return false;
	}

	for (size_t i = 0; i < uuid_str_size; i++) {
		if (time_count > 2) {
			for (size_t j = i; j < uuid_str_size && seq_count < 8; j+=2) {
				if (uuid_str[j] == '-') {
					j++; // skip
				}

				// put every two chars into seqdata
				string_ncpy(temp_str, &uuid_str[j], 2);
				uuid->clockSeqAndNode[seq_count] = (uint8_t) string_toul(temp_str, (char **)NULL, 16);
				seq_count++;
			}

			// finished with parsing the string
			uuid->timeLow = (uint32_t) time_data[0];
			uuid->timeMid = (uint16_t) time_data[1];
			uuid->timeHiAndVersion = (uint16_t) time_data[2];
			return true;
		} else {
			if (uuid_str[i] == '-') {
				memset_peregrine(temp_str_special, 0, 32); // reset for next part

				string_ncpy(temp_str_special, substr_begin, (&uuid_str[i]) - substr_begin);

				time_data[time_count] = (uint32_t) string_toul(temp_str_special, (char **)NULL, 16);

				substr_begin = &uuid_str[i + 1]; // beginning of next chunk
				time_count++;
			}
		}
	}

	return false;
}

#if HOST_TESTING_MODE || LOG_LEVEL >= LOG_LEVEL_INFO

/**
 * @brief Converts a TEE_UUID to a UUID string
 * 
 * @param uuid uuid_t input
 * @param uuid_str output buffer
 * @param uuid_str_size size of the output buffer
 * @return true if successful
 * @return false else
 */
bool uuid_to_str(uuid_t *uuid, char *uuid_str, size_t uuid_str_size)
{
	uint64_t ret;
	ret = string_snprintf(uuid_str, uuid_str_size, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", (MANIFEST_PRINT_WIDTH_FIX) uuid->timeLow, (MANIFEST_PRINT_WIDTH_FIX) uuid->timeMid, (MANIFEST_PRINT_WIDTH_FIX) uuid->timeHiAndVersion,
			(MANIFEST_PRINT_WIDTH_FIX) uuid->clockSeqAndNode[0], (MANIFEST_PRINT_WIDTH_FIX) uuid->clockSeqAndNode[1], (MANIFEST_PRINT_WIDTH_FIX) uuid->clockSeqAndNode[2], (MANIFEST_PRINT_WIDTH_FIX) uuid->clockSeqAndNode[3],
			(MANIFEST_PRINT_WIDTH_FIX) uuid->clockSeqAndNode[4], (MANIFEST_PRINT_WIDTH_FIX) uuid->clockSeqAndNode[5], (MANIFEST_PRINT_WIDTH_FIX) uuid->clockSeqAndNode[6], (MANIFEST_PRINT_WIDTH_FIX) uuid->clockSeqAndNode[7]);

	return ret > 0;
}

#endif
