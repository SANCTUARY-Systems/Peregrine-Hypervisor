#pragma once

#ifndef EXTERNAL_INCLUDE

#include "pg/arch/std.h"
#include "pg/arch/types.h"

#include "stdbool.h"

typedef struct {
	uint32_t timeLow;
	uint16_t timeMid;
	uint16_t timeHiAndVersion;
	uint8_t clockSeqAndNode[8];
} uuid_t;

#endif

#define UUID_STR_SIZE 36

bool uuid_is_equal(uuid_t *uuid_1, uuid_t *uuid_2);
void uuid_from_uint64(uint64_t partial_uuid_1, uint64_t partial_uuid_2, uuid_t *uuid);
void uuid_to_uint64(uuid_t *uuid, uint64_t *partial_uuid_1, uint64_t *partial_uuid_2);
bool uuid_from_str(char *uuid_str, size_t uuid_str_size, uuid_t *uuid);

#if HOST_TESTING_MODE || LOG_LEVEL >= LOG_LEVEL_INFO
bool uuid_to_str(uuid_t *uuid, char *uuid_str, size_t uuid_str_size);
#endif
