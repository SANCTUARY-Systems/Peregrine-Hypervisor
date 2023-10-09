/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include <stdatomic.h>

#include "pg/arch/types.h"

#include "pg/cpu.h"
#include "pg/interrupt_desc.h"
#include "pg/list.h"
#include "pg/mm.h"
#include "pg/mpool.h"
#include "pg/string.h"
#include "vmapi/pg/ffa.h"
#include "pg/manifest.h"

#define LOG_BUFFER_SIZE 256
#define VM_MANIFEST_MAX_INTERRUPTS 64

/**
 * The state of an RX buffer.
 *
 * EMPTY is the initial state. The follow state transitions are possible:
 * * EMPTY => RECEIVED: message sent to the VM.
 * * RECEIVED => READ: secondary VM returns from FFA_MSG_WAIT or
 *   FFA_MSG_POLL, or primary VM returns from FFA_RUN with an FFA_MSG_SEND
 *   where the receiver is itself.
 * * READ => EMPTY: VM called FFA_RX_RELEASE.
 */
enum mailbox_state {
	/** There is no message in the mailbox. */
	MAILBOX_STATE_EMPTY,

	/** There is a message in the mailbox that is waiting for a reader. */
	MAILBOX_STATE_RECEIVED,

	/** There is a message in the mailbox that has been read. */
	MAILBOX_STATE_READ,
};

struct wait_entry {
	/** The VM that is waiting for a mailbox to become writable. */
	struct vm *waiting_vm;

	/**
	 * Links used to add entry to a VM's waiter_list. This is protected by
	 * the notifying VM's lock.
	 */
	struct list_entry wait_links;

	/**
	 * Links used to add entry to a VM's ready_list. This is protected by
	 * the waiting VM's lock.
	 */
	struct list_entry ready_links;
};

struct mailbox {
	enum mailbox_state state;
	void *recv;
	const void *send;

	/** The ID of the VM which sent the message currently in `recv`. */
	uint16_t recv_sender;

	/** The size of the message currently in `recv`. */
	uint32_t recv_size;

	/**
	 * The FF-A function ID to use to deliver the message currently in
	 * `recv`.
	 */
	uint32_t recv_func;

	/**
	 * List of wait_entry structs representing VMs that want to be notified
	 * when the mailbox becomes writable. Once the mailbox does become
	 * writable, the entry is removed from this list and added to the
	 * waiting VM's ready_list.
	 */
	struct list_entry waiter_list;

	/**
	 * List of wait_entry structs representing VMs whose mailboxes became
	 * writable since the owner of the mailbox registers for notification.
	 */
	struct list_entry ready_list;
};

struct vm {
	uint16_t id;
	uuid_t uuid;
	struct smc_whitelist smc_whitelist;

	/** See api.c for the partial ordering on locks. */
	struct spinlock lock;
	ffa_vcpu_count_t vcpu_count;
	struct vcpu vcpus[MAX_CPUS];
	struct mm_ptable ptable;
	struct mailbox mailbox;
	char log_buffer[LOG_BUFFER_SIZE];
	uint16_t log_buffer_length;

	/*
	 * Stores IDs of physical cores assigned to the VM
	 */
	cpu_id_t cpus[MAX_CPUS];

	/**
	 * Wait entries to be used when waiting on other VM mailboxes. See
	 * comments on `struct wait_entry` for the lock discipline of these.
	 */
#if MAX_VMS == 0
	struct wait_entry wait_entries[MAX_VMS+1];
#else
	struct wait_entry wait_entries[MAX_VMS];
#endif

	atomic_bool aborting;

	/**
	 * Booting parameters (FF-A SP partitions).
	 */
	bool initialized;
	uint16_t boot_order;
	bool supports_managed_exit;
	struct vm *next_boot;

	/**
	 * Secondary entry point supplied by FFA_SECONDARY_EP_REGISTER used
	 * for cold and warm boot of SP execution contexts.
	 */
	ipaddr_t secondary_ep;

	/** Arch-specific VM information. */
	struct arch_vm arch;

	/** Interrupt descriptor */
	struct interrupt_descriptor interrupt_desc[VM_MANIFEST_MAX_INTERRUPTS];
	struct virt_gic* vgic;

	/* IPA memory information */
	ipaddr_t ipa_mem_begin;
	ipaddr_t ipa_mem_end;
};

/** Encapsulates a VM whose lock is held. */
struct vm_locked {
	struct vm *vm;
};

/** Container for two vm_locked structures */
struct two_vm_locked {
	struct vm_locked vm1;
	struct vm_locked vm2;
};

struct vm *vm_init(uint16_t, uint16_t, uint16_t, uint32_t *, struct mpool *);
bool vm_init_next(uint16_t, uint16_t, uint32_t *,
                  struct mpool *, struct vm **);

uint16_t vm_local_cpu_index(struct cpu *);

uint16_t vm_get_count(void);
struct vm *vm_find(uint16_t id);
struct vm *vm_find_uuid(uuid_t *uuid);
struct vm *vm_find_index(uint16_t index);
struct vm *vm_find_from_cpu(struct cpu *cpu);
struct vm_locked vm_lock(struct vm *vm);
struct two_vm_locked vm_lock_both(struct vm *vm1, struct vm *vm2);
void vm_unlock(struct vm_locked *locked);
struct vcpu *vm_get_vcpu(struct vm *vm, uint16_t vcpu_index);
struct wait_entry *vm_get_wait_entry(struct vm *vm, uint16_t for_vm);
uint16_t vm_id_for_wait_entry(struct vm *vm, struct wait_entry *entry);

bool vm_identity_map(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
		     uint32_t mode, struct mpool *ppool, ipaddr_t *ipa);
bool vm_identity_map_and_reserve(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
		     uint32_t mode, struct mpool *ppool, ipaddr_t *ipa);
bool vm_identity_prepare(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
			 uint32_t mode, struct mpool *ppool);
void vm_identity_commit(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
			uint32_t mode, struct mpool *ppool, ipaddr_t *ipa);
bool vm_unmap(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
	      struct mpool *ppool);
bool vm_unmap_hypervisor(struct vm_locked vm_locked, struct mpool *ppool);

void vm_update_boot(struct vm *vm);
struct vm *vm_get_first_boot(void);
