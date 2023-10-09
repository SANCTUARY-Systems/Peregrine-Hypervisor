#include "pg/manifest.h"
#include "pg/mm.h"
#include "pg/std.h"

#include "vmapi/pg/ffa.h"

#ifndef register_external_mediator
#  define register_external_mediator(fdt) false
#endif

struct mediator_ops {
	/*
	 * Probe for TEE. Should return true if TEE found and
	 * mediator is initialized.
	 */
	bool (*probe)(struct mm_stage1_locked mm_stage1_locked,
		      struct mpool *ppool, struct memiter *manifest_bin,
		      struct memiter *manifest_sig, struct manifest **manifest);

	/*
	 * Called during VM construction if toolstack requests to enable
	 * TEE support so mediator can inform TEE about new
	 * guest and create own structures for the new domain.
	 */
	int (*vm_init)(uint16_t, struct memiter *,
		       struct manifest_vm *);

	/*
	 * Called during VM destruction to relinquish resources used
	 * by mediator itself. This function can return -ERESTART to indicate
	 * that it does not finished work and should be called again.
	 */
	int (*free_resources)(uint16_t);

	/* Handle SMCCC call for current VM. */
	bool (*handle_smccc)(struct ffa_value *, struct ffa_value *);
};

extern struct mediator_ops tee_mediator_ops;

// bool register_external_mediator(struct fdt *);

void register_mediator(bool (*probe)(struct mm_stage1_locked mm_stage1_locked,
				     struct mpool *ppool, struct memiter *,
				     struct memiter *, struct manifest **),
		       int (*vm_init)(uint16_t, struct memiter *,
				      struct manifest_vm *),
		       int (*free_resources)(uint16_t),
		       bool (*handle_smccc)(struct ffa_value *,
					    struct ffa_value *));

void unregister_mediator();
