#include "pg/arch/tee/mediator.h"
#include "pg/arch/tee/default_mediator.h"
#include "pg/manifest.h"
#include <stdarg.h>
#include "pg/std.h"
#include "pg/vm.h"

#include "../smc.h"

/**
 * @brief Parse the manifest
 *
 */
bool default_mediator_probe(struct mm_stage1_locked mm_stage1_locked_in,
		 struct mpool *ppool, struct memiter *manifest_bin,
		 struct memiter *manifest_sig, struct manifest **manifest)
{
	// parse manifest
	manifest_init(ppool, manifest, manifest_bin);
	return true;
}

/**
 * @brief Init function that needs to be called per VM
 * 
 * @param id ID of the VM
 * @param manifest_bin Address of the binary manifest
 * @param manifest Target address of manifest
 * @return int Indicating whether the init call was sucessful
 */
int default_mediator_vm_init(uint16_t id, struct memiter *manifest_bin, struct manifest_vm *manifest) {
	return 0;
}

/**
 * @brief Handle SMC call.
 * 
 * @param args Input registers.
 * @param ret Output registers.
 * @return true if the call could be processed.
 * @return false else.
 */
bool default_mediator_handle_smccc(struct ffa_value *args, struct ffa_value *ret)
{
	(*ret) = smc_forward(args->func, args->arg1, args->arg2,
					args->arg3, args->arg4, args->arg5,
					args->arg6, args->arg7);
	return true;
}


int default_mediator_free_resources(uint16_t id)
{
	return 0;
}

void register_default_mediator()
{
	register_mediator(default_mediator_probe, default_mediator_vm_init, default_mediator_free_resources,
			  default_mediator_handle_smccc);
}
