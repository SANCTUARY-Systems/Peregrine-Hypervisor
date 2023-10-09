#pragma once

#include "pg/manifest.h"
#include "pg/std.h"
#include "pg/list.h"

#include "vmapi/pg/ffa.h"

bool default_mediator_probe(struct mm_stage1_locked mm_stage1_locked, struct mpool *ppool,
		 struct memiter *manifest_bin, struct memiter *manifest_sig,
		 struct manifest **manifest);

int default_mediator_vm_init(uint16_t, struct memiter *, struct manifest_vm *);

int default_mediator_free_resources(uint16_t);

bool default_mediator_handle_smccc(struct ffa_value *, struct ffa_value *);

void register_default_mediator();
