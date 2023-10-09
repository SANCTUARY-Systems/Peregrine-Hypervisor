/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include <gmock/gmock.h>

extern "C" {
#include "pg/boot_params.h"
#include "pg/fdt_handler.h"
#include "pg/mpool.h"
}

#include <memory>

namespace
{
using ::testing::Eq;
using ::testing::NotNull;

constexpr size_t TEST_HEAP_SIZE = PAGE_SIZE * 10;

/*
 * /dts-v1/;
 *
 * / {
 *       #address-cells = <2>;
 *       #size-cells = <2>;
 *
 *       memory@0 {
 *           device_type = "memory";
 *           reg = <0x00000000 0x00000000 0x00000000 0x20000000
 *                  0x00000000 0x30000000 0x00000000 0x00010000>;
 *       };
 *       memory@1 {
 *           device_type = "memory";
 *           reg = <0x00000000 0x30020000 0x00000000 0x00010000>;
 *       };
 *
 *       chosen {
 *           linux,initrd-start = <0x00000000>;
 *           linux,initrd-end = <0x00000000>;
 *       };
 * };
 *
 * $ dtc --boot-cpu 0 --in-format dts --out-format dtb --out-version 17 test.dts
 * | xxd -i
 */

constexpr uint8_t test_dtb[] = {
	0xd0, 0x0d, 0xfe, 0xed, 0x00, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x00, 0x38,
	0x00, 0x00, 0x01, 0x30, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x11,
	0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f,
	0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03,
	0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x01, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x40, 0x30,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07,
	0x00, 0x00, 0x00, 0x1b, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x27,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x01, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x40, 0x31,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07,
	0x00, 0x00, 0x00, 0x1b, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x27,
	0x00, 0x00, 0x00, 0x00, 0x30, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
	0x63, 0x68, 0x6f, 0x73, 0x65, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
	0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x3e,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x09, 0x23, 0x61, 0x64, 0x64, 0x72, 0x65, 0x73, 0x73,
	0x2d, 0x63, 0x65, 0x6c, 0x6c, 0x73, 0x00, 0x23, 0x73, 0x69, 0x7a, 0x65,
	0x2d, 0x63, 0x65, 0x6c, 0x6c, 0x73, 0x00, 0x64, 0x65, 0x76, 0x69, 0x63,
	0x65, 0x5f, 0x74, 0x79, 0x70, 0x65, 0x00, 0x72, 0x65, 0x67, 0x00, 0x6c,
	0x69, 0x6e, 0x75, 0x78, 0x2c, 0x69, 0x6e, 0x69, 0x74, 0x72, 0x64, 0x2d,
	0x73, 0x74, 0x61, 0x72, 0x74, 0x00, 0x6c, 0x69, 0x6e, 0x75, 0x78, 0x2c,
	0x69, 0x6e, 0x69, 0x74, 0x72, 0x64, 0x2d, 0x65, 0x6e, 0x64, 0x00};

TEST(fdt, find_memory_ranges)
{
	struct mpool ppool;
	std::unique_ptr<uint8_t[]> test_heap(new uint8_t[TEST_HEAP_SIZE]);

	mpool_init(&ppool, sizeof(struct mm_page_table));
	mpool_add_chunk(&ppool, test_heap.get(), TEST_HEAP_SIZE);
	mm_init(&ppool);

	struct fdt fdt;
	struct boot_params params = {};

	struct mm_stage1_locked mm_stage1_locked = mm_lock_stage1();
	struct string memory = STRING_INIT("memory");
	ASSERT_TRUE(fdt_map(&fdt, mm_stage1_locked,
			    pa_init((uintpaddr_t)&test_dtb), &ppool));
	fdt_find_memory_ranges(&fdt, &memory, params.mem_ranges,
			       &params.mem_ranges_count, MAX_MEM_RANGES);
	ASSERT_TRUE(fdt_unmap(&fdt, mm_stage1_locked, &ppool));
	mm_unlock_stage1(&mm_stage1_locked);

	EXPECT_THAT(params.mem_ranges_count, Eq(3));
	EXPECT_THAT(pa_addr(params.mem_ranges[0].begin), Eq(0x00000000));
	EXPECT_THAT(pa_addr(params.mem_ranges[0].end), Eq(0x20000000));
	EXPECT_THAT(pa_addr(params.mem_ranges[1].begin), Eq(0x30000000));
	EXPECT_THAT(pa_addr(params.mem_ranges[1].end), Eq(0x30010000));
	EXPECT_THAT(pa_addr(params.mem_ranges[2].begin), Eq(0x30020000));
	EXPECT_THAT(pa_addr(params.mem_ranges[2].end), Eq(0x30030000));
}

} /* namespace */
