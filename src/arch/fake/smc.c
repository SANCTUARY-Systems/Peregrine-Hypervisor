/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "smc.h"

#include <stdint.h>

#include "vmapi/pg/ffa.h"

static struct ffa_value smc_internal(uint32_t func, uint64_t arg0,
				     uint64_t arg1, uint64_t arg2,
				     uint64_t arg3, uint64_t arg4,
				     uint64_t arg5, uint64_t arg6)
{
	return (struct ffa_value){.func = func,
				  .arg1 = arg0,
				  .arg2 = arg1,
				  .arg3 = arg2,
				  .arg4 = arg3,
				  .arg5 = arg4,
				  .arg6 = arg5,
				  .arg7 = arg6};
}

/** Make an SMC call following the 32-bit SMC calling convention. */
struct ffa_value smc32(uint32_t func, uint32_t arg0, uint32_t arg1,
		       uint32_t arg2, uint32_t arg3, uint32_t arg4,
		       uint32_t arg5, uint32_t caller_id)
{
	return smc_internal(func | SMCCC_32_BIT, arg0, arg1, arg2, arg3, arg4,
			    arg5, caller_id);
}

/** Make an SMC call following the 64-bit SMC calling convention. */
struct ffa_value smc64(uint32_t func, uint64_t arg0, uint64_t arg1,
		       uint64_t arg2, uint64_t arg3, uint64_t arg4,
		       uint64_t arg5, uint32_t caller_id)
{
	return smc_internal(func | SMCCC_64_BIT, arg0, arg1, arg2, arg3, arg4,
			    arg5, caller_id);
}

/** Forward a raw SMC on to EL3. */
struct ffa_value smc_forward(uint32_t func, uint64_t arg0, uint64_t arg1,
			     uint64_t arg2, uint64_t arg3, uint64_t arg4,
			     uint64_t arg5, uint32_t caller_id)
{
	return smc_internal(func, arg0, arg1, arg2, arg3, arg4, arg5,
			    caller_id);
}

/**
 * Make an FF-A call up to EL3. Assumes the function ID is already masked
 * appropriately for the 32-bit or 64-bit SMCCC.
 */
struct ffa_value smc_ffa_call(struct ffa_value args)
{
	return smc_internal(args.func, args.arg1, args.arg2, args.arg3,
			    args.arg4, args.arg5, args.arg6, args.arg7);
}
