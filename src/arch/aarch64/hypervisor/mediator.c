#include "pg/arch/tee/mediator.h"
#include "pg/arch/tee/default_mediator.h"

#include "pg/dlog.h"

struct mediator_ops tee_mediator_ops = {
	.probe = default_mediator_probe,
	.vm_init = default_mediator_vm_init,
	.free_resources = default_mediator_free_resources,
	.handle_smccc = default_mediator_handle_smccc
};

void register_mediator(bool (*probe)(struct mm_stage1_locked mm_stage1_locked,
				     struct mpool *, struct memiter *,
				     struct memiter *, struct manifest **),
		       int (*vm_init)(uint16_t, struct memiter *,
				      struct manifest_vm *),
		       int (*free_resources)(uint16_t),
		       bool (*handle_smccc)(struct ffa_value *,
					    struct ffa_value *))
{
	tee_mediator_ops.probe = probe;
	tee_mediator_ops.vm_init = vm_init;
	tee_mediator_ops.free_resources = free_resources;
	tee_mediator_ops.handle_smccc = handle_smccc;
}

void unregister_mediator()
{
	tee_mediator_ops.probe = default_mediator_probe;
	tee_mediator_ops.vm_init = default_mediator_vm_init;
	tee_mediator_ops.free_resources = default_mediator_free_resources;
	tee_mediator_ops.handle_smccc = default_mediator_handle_smccc;
}
