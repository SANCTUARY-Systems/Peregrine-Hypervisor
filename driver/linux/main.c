// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2018 The Hafnium Authors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <clocksource/arm_arch_timer.h>
#include <linux/atomic.h>
#include <linux/cpuhotplug.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <net/sock.h>

#include <pg/call.h>
#include <pg/ffa.h>
#include <pg/transport.h>
#include <pg/vm_ids.h>

#include "uapi/pg/socket.h"

#define HYPERVISOR_TIMER_NAME "el2_timer"

#define CONFIG_HAFNIUM_MAX_VMS   16
#define CONFIG_HAFNIUM_MAX_VCPUS 32

#define PG_VM_ID_BASE 0
#define PRIMARY_VM_ID PG_VM_ID_OFFSET
#define FIRST_SECONDARY_VM_ID (PG_VM_ID_OFFSET + 1)

struct pg_vcpu {
	struct pg_vm *vm;
	ffa_vcpu_index_t vcpu_index;
	struct task_struct *task;
	atomic_t abort_sleep;
	atomic_t waiting_for_message;
	struct hrtimer timer;
};

struct pg_vm {
	ffa_vm_id_t id;
	ffa_vcpu_count_t vcpu_count;
	struct pg_vcpu *vcpu;
};

struct pg_sock {
	/* This needs to be the first field. */
	struct sock sk;

	/*
	 * The following fields are immutable after the socket transitions to
	 * SS_CONNECTED state.
	 */
	uint64_t local_port;
	uint64_t remote_port;
	struct pg_vm *peer_vm;
};

static struct proto pg_sock_proto = {
	.name = "hafnium",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct pg_sock),
};

static struct pg_vm *pg_vms;
static ffa_vm_count_t pg_vm_count;
static struct page *pg_send_page;
static struct page *pg_recv_page;
static atomic64_t pg_next_port = ATOMIC64_INIT(0);
static DEFINE_SPINLOCK(pg_send_lock);
static DEFINE_HASHTABLE(pg_local_port_hash, 7);
static DEFINE_SPINLOCK(pg_local_port_hash_lock);
static int pg_irq;
static enum cpuhp_state pg_cpuhp_state;
static ffa_vm_id_t current_vm_id;

/**
 * Retrieves a VM from its ID, returning NULL if the VM doesn't exist.
 */
static struct pg_vm *pg_vm_from_id(ffa_vm_id_t vm_id)
{
	if (vm_id < FIRST_SECONDARY_VM_ID ||
	    vm_id >= FIRST_SECONDARY_VM_ID + pg_vm_count)
		return NULL;

	return &pg_vms[vm_id - FIRST_SECONDARY_VM_ID];
}

/**
 * Wakes up the kernel thread responsible for running the given vcpu.
 *
 * Returns 0 if the thread was already running, 1 otherwise.
 */
static int pg_vcpu_wake_up(struct pg_vcpu *vcpu)
{
	/* Set a flag indicating that the thread should not go to sleep. */
	atomic_set(&vcpu->abort_sleep, 1);

	/* Set the thread to running state. */
	return wake_up_process(vcpu->task);
}

/**
 * Puts the current thread to sleep. The current thread must be responsible for
 * running the given vcpu.
 *
 * Going to sleep will fail if pg_vcpu_wake_up() or kthread_stop() was called on
 * this vcpu/thread since the last time it [re]started running.
 */
static void pg_vcpu_sleep(struct pg_vcpu *vcpu)
{
	int abort;

	set_current_state(TASK_INTERRUPTIBLE);

	/* Check the sleep-abort flag after making thread interruptible. */
	abort = atomic_read(&vcpu->abort_sleep);
	if (!abort && !kthread_should_stop())
		schedule();

	/* Set state back to running on the way out. */
	set_current_state(TASK_RUNNING);
}

/**
 * Wakes up the thread associated with the vcpu that owns the given timer. This
 * is called when the timer the thread is waiting on expires.
 */
static enum hrtimer_restart pg_vcpu_timer_expired(struct hrtimer *timer)
{
	struct pg_vcpu *vcpu = container_of(timer, struct pg_vcpu, timer);
	/* TODO: Inject interrupt. */
	pg_vcpu_wake_up(vcpu);
	return HRTIMER_NORESTART;
}

/**
 * This function is called when Hafnium requests that the primary VM wake up a
 * vCPU that belongs to a secondary VM.
 *
 * It wakes up the thread if it's sleeping, or kicks it if it's already running.
 */
static void pg_handle_wake_up_request(ffa_vm_id_t vm_id,
				      ffa_vcpu_index_t vcpu)
{
	struct pg_vm *vm = pg_vm_from_id(vm_id);

	if (!vm) {
		pr_warn("Request to wake up non-existent VM id: %u\n", vm_id);
		return;
	}

	if (vcpu >= vm->vcpu_count) {
		pr_warn("Request to wake up non-existent vCPU: %u.%u\n",
			vm_id, vcpu);
		return;
	}

	if (pg_vcpu_wake_up(&vm->vcpu[vcpu]) == 0) {
		/*
		 * The task was already running (presumably on a different
		 * physical CPU); interrupt it. This gives Hafnium a chance to
		 * inject any new interrupts.
		 */
		kick_process(vm->vcpu[vcpu].task);
	}
}

/**
 * Injects an interrupt into a vCPU of the VM and ensures the vCPU will run to
 * handle the interrupt.
 */
static void pg_interrupt_vm(ffa_vm_id_t vm_id, uint64_t int_id)
{
	struct pg_vm *vm = pg_vm_from_id(vm_id);
	ffa_vcpu_index_t vcpu;
	int64_t ret;

	if (!vm) {
		pr_warn("Request to wake up non-existent VM id: %u\n", vm_id);
		return;
	}

	/*
	 * TODO: For now we're picking the first vcpu to interrupt, but
	 * we want to be smarter.
	 */
	vcpu = 0;
	ret = pg_interrupt_inject(vm_id, vcpu, int_id);

	if (ret == -1) {
		pr_warn("Failed to inject interrupt %lld to vCPU %d of VM %d",
			int_id, vcpu, vm_id);
		return;
	}

	if (ret != 1) {
		/* We don't need to wake up the vcpu. */
		return;
	}

	pg_handle_wake_up_request(vm_id, vcpu);
}

/**
 * Notify all waiters on the given VM.
 */
static void pg_notify_waiters(ffa_vm_id_t vm_id)
{
	ffa_vm_id_t waiter_vm_id;

	while ((waiter_vm_id = pg_mailbox_waiter_get(vm_id)) != -1) {
		if (waiter_vm_id == PRIMARY_VM_ID) {
			/*
			 * TODO: Use this information when implementing per-vm
			 * queues.
			 */
		} else {
			pg_interrupt_vm(waiter_vm_id,
					PG_MAILBOX_WRITABLE_INTID);
		}
	}
}

/**
 * Delivers a message to a VM.
 */
static void pg_deliver_message(ffa_vm_id_t vm_id)
{
	struct pg_vm *vm = pg_vm_from_id(vm_id);
	ffa_vcpu_index_t i;

	if (!vm) {
		pr_warn("Tried to deliver message to non-existent VM id: %u\n",
			vm_id);
		return;
	}

	/* Try to wake a vCPU that is waiting for a message. */
	for (i = 0; i < vm->vcpu_count; i++) {
		if (atomic_read(&vm->vcpu[i].waiting_for_message)) {
			pg_handle_wake_up_request(vm->id,
						  vm->vcpu[i].vcpu_index);
			return;
		}
	}

	/* None were waiting for a message so interrupt one. */
	pg_interrupt_vm(vm->id, PG_MAILBOX_READABLE_INTID);
}

/**
 * Handles a message delivered to this VM by validating that it's well-formed
 * and then queueing it for delivery to the appropriate socket.
 */
static void pg_handle_message(struct pg_vm *sender, size_t len,
			      const void *message)
{
	struct pg_sock *hsock;
	const struct pg_msg_hdr *hdr = (struct pg_msg_hdr *)message;
	struct sk_buff *skb;
	int err;

	/* Ignore messages that are too small to hold a header. */
	if (len < sizeof(struct pg_msg_hdr)) {
		pr_err("Message received without header of length %d\n", len);
		ffa_rx_release();
		return;
	}

	len -= sizeof(struct pg_msg_hdr);

	/* Go through the colliding sockets. */
	rcu_read_lock();
	hash_for_each_possible_rcu(pg_local_port_hash, hsock, sk.sk_node,
				   hdr->dst_port) {
		if (hsock->peer_vm == sender &&
		    hsock->remote_port == hdr->src_port) {
			sock_hold(&hsock->sk);
			break;
		}
	}
	rcu_read_unlock();

	/* Nothing to do if we couldn't find the target. */
	if (!hsock) {
		ffa_rx_release();
		return;
	}

	/*
	 * TODO: From this point on, there are two failure paths: when we
	 * create the skb below, and when we enqueue it to the socket. What
	 * should we do if they fail? Ideally we would have some form of flow
	 * control to prevent message loss, but how to do it efficiently?
	 *
	 * One option is to have a pre-allocated message that indicates to the
	 * sender that a message was dropped. This way we guarantee that the
	 * sender will be aware of loss and should back-off.
	 */
	/* Create the skb. */
	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		goto exit;

	memcpy(skb_put(skb, len), hdr + 1, len);

	/*
	 * Add the skb to the receive queue of the target socket. On success it
	 * calls sk->sk_data_ready, which is currently set to sock_def_readable,
	 * which wakes up any waiters.
	 */
	err = sock_queue_rcv_skb(&hsock->sk, skb);
	if (err)
		kfree_skb(skb);

exit:
	sock_put(&hsock->sk);

	if (ffa_rx_release().func == FFA_RX_RELEASE_32)
		pg_notify_waiters(PRIMARY_VM_ID);
}

/**
 * This is the main loop of each vcpu.
 */
static int pg_vcpu_thread(void *data)
{
	struct pg_vcpu *vcpu = data;
	struct ffa_value ret;

	hrtimer_init(&vcpu->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vcpu->timer.function = &pg_vcpu_timer_expired;

	while (!kthread_should_stop()) {
		ffa_vcpu_index_t i;

		/*
		 * We're about to run the vcpu, so we can reset the abort-sleep
		 * flag.
		 */
		atomic_set(&vcpu->abort_sleep, 0);

		/* Call into Hafnium to run vcpu. */
		ret = ffa_run(vcpu->vm->id, vcpu->vcpu_index);

		switch (ret.func) {
		/* Preempted. */
		case FFA_INTERRUPT_32:
			if (need_resched())
				schedule();
			break;

		/* Yield. */
		case FFA_YIELD_32:
			if (!kthread_should_stop())
				schedule();
			break;

		/* WFI. */
		case PG_FFA_RUN_WAIT_FOR_INTERRUPT:
			if (ret.arg2 != FFA_SLEEP_INDEFINITE) {
				hrtimer_start(&vcpu->timer, ret.arg2,
					      HRTIMER_MODE_REL);
			}
			pg_vcpu_sleep(vcpu);
			hrtimer_cancel(&vcpu->timer);
			break;

		/* Waiting for a message. */
		case FFA_MSG_WAIT_32:
			atomic_set(&vcpu->waiting_for_message, 1);
			if (ret.arg2 != FFA_SLEEP_INDEFINITE) {
				hrtimer_start(&vcpu->timer, ret.arg2,
					      HRTIMER_MODE_REL);
			}
			pg_vcpu_sleep(vcpu);
			hrtimer_cancel(&vcpu->timer);
			atomic_set(&vcpu->waiting_for_message, 0);
			break;

		/* Wake up another vcpu. */
		case PG_FFA_RUN_WAKE_UP:
			pg_handle_wake_up_request(ffa_vm_id(ret),
						  ffa_vcpu_index(ret));
			break;

		/* Response available. */
		case FFA_MSG_SEND_32:
			if (ffa_receiver(ret) == PRIMARY_VM_ID) {
				pg_handle_message(vcpu->vm,
						  ffa_msg_send_size(ret),
						  page_address(pg_recv_page));
			} else {
				pg_deliver_message(ffa_receiver(ret));
			}
			break;

		/* Notify all waiters. */
		case FFA_RX_RELEASE_32:
			pg_notify_waiters(vcpu->vm->id);
			break;

		case FFA_ERROR_32:
			pr_warn("FF-A error %d running VM %d vCPU %d", ret.arg2,
				vcpu->vm->id, vcpu->vcpu_index);
			switch (ret.arg2) {
			/* Abort was triggered. */
			case FFA_ABORTED:
				for (i = 0; i < vcpu->vm->vcpu_count; i++) {
					if (i == vcpu->vcpu_index)
						continue;
					pg_handle_wake_up_request(vcpu->vm->id,
								  i);
				}
				pg_vcpu_sleep(vcpu);
				break;
			default:
				/* Treat as a yield and try again later. */
				if (!kthread_should_stop())
					schedule();
				break;
			}
			break;
		}
	}

	return 0;
}

/**
 * Converts a pointer to a struct sock into a pointer to a struct pg_sock. It
 * relies on the fact that the first field of pg_sock is a sock.
 */
static struct pg_sock *hsock_from_sk(struct sock *sk)
{
	return (struct pg_sock *)sk;
}

/**
 * This is called when the last reference to the outer socket is released. For
 * example, if it's a user-space socket, when the last file descriptor pointing
 * to this socket is closed.
 *
 * It begins cleaning up resources, though some can only be cleaned up after all
 * references to the underlying socket are released, which is handled by
 * pg_sock_destruct().
 */
static int pg_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct pg_sock *hsock = hsock_from_sk(sk);
	unsigned long flags;

	if (!sk)
		return 0;

	/* Shutdown for both send and receive. */
	lock_sock(sk);
	sk->sk_shutdown |= RCV_SHUTDOWN | SEND_SHUTDOWN;
	sk->sk_state_change(sk);
	release_sock(sk);

	/* Remove from the hash table, so lookups from now on won't find it. */
	spin_lock_irqsave(&pg_local_port_hash_lock, flags);
	hash_del_rcu(&hsock->sk.sk_node);
	spin_unlock_irqrestore(&pg_local_port_hash_lock, flags);

	/*
	 * TODO: When we implement a tx queue, we need to clear it here so that
	 * sk_wmem_alloc will not prevent sk from being freed (sk_free).
	 */

	/*
	 * Wait for in-flight lookups to finish. We need to do this here because
	 * in-flight lookups rely on the reference to the socket we're about to
	 * release.
	 */
	synchronize_rcu();
	sock_put(sk);
	sock->sk = NULL;

	return 0;
}

/**
 * This is called when there are no more references to the socket. It frees all
 * resources that haven't been freed during release.
 */
static void pg_sock_destruct(struct sock *sk)
{
	/*
	 * Clear the receive queue now that the handler cannot add any more
	 * skbs to it.
	 */
	skb_queue_purge(&sk->sk_receive_queue);
}

/**
 * Connects the Hafnium socket to the provided VM and port. After the socket is
 * connected, it can be used to exchange datagrams with the specified peer.
 */
static int pg_sock_connect(struct socket *sock, struct sockaddr *saddr, int len,
			   int connect_flags)
{
	struct sock *sk = sock->sk;
	struct pg_sock *hsock = hsock_from_sk(sk);
	struct pg_vm *vm;
	struct pg_sockaddr *addr;
	int err;
	unsigned long flags;

	/* Basic address validation. */
	if (len < sizeof(struct pg_sockaddr) || saddr->sa_family != AF_HF)
		return -EINVAL;

	addr = (struct pg_sockaddr *)saddr;
	vm = pg_vm_from_id(addr->vm_id);
	if (!vm)
		return -ENETUNREACH;

	/*
	 * TODO: Once we implement access control in Hafnium, check that the
	 * caller is allowed to contact the specified VM. Return -ECONNREFUSED
	 * if access is denied.
	 */

	/* Take lock to make sure state doesn't change as we connect. */
	lock_sock(sk);

	/* Only unconnected sockets are allowed to become connected. */
	if (sock->state != SS_UNCONNECTED) {
		err = -EISCONN;
		goto exit;
	}

	hsock->local_port = atomic64_inc_return(&pg_next_port);
	hsock->remote_port = addr->port;
	hsock->peer_vm = vm;

	sock->state = SS_CONNECTED;

	/* Add socket to hash table now that it's fully initialised. */
	spin_lock_irqsave(&pg_local_port_hash_lock, flags);
	hash_add_rcu(pg_local_port_hash, &sk->sk_node, hsock->local_port);
	spin_unlock_irqrestore(&pg_local_port_hash_lock, flags);

	err = 0;
exit:
	release_sock(sk);
	return err;
}

/**
 * Sends the given skb to the appropriate VM by calling Hafnium. It will also
 * trigger the wake up of a recipient VM.
 *
 * Takes ownership of the skb on success.
 */
static int pg_send_skb(struct sk_buff *skb)
{
	unsigned long flags;
	struct ffa_value ret;
	struct pg_sock *hsock = hsock_from_sk(skb->sk);
	struct pg_vm *vm = hsock->peer_vm;
	void *message = page_address(pg_send_page);

	/*
	 * Call Hafnium under the send lock so that we serialize the use of the
	 * global send buffer.
	 */
	spin_lock_irqsave(&pg_send_lock, flags);
	memcpy(message, skb->data, skb->len);

	ret = ffa_msg_send(current_vm_id, vm->id, skb->len, 0);
	spin_unlock_irqrestore(&pg_send_lock, flags);

	if (ret.func == FFA_ERROR_32) {
		switch (ret.arg2) {
		case FFA_INVALID_PARAMETERS:
			return -ENXIO;
		case FFA_NOT_SUPPORTED:
			return -EIO;
		case FFA_DENIED:
		case FFA_BUSY:
		default:
			return -EAGAIN;
		}
	}

	/* Ensure the VM will run to pick up the message. */
	pg_deliver_message(vm->id);

	kfree_skb(skb);

	return 0;
}

/**
 * Determines if the given socket is in the connected state. It acquires and
 * releases the socket lock.
 */
static bool pg_sock_is_connected(struct socket *sock)
{
	bool ret;

	lock_sock(sock->sk);
	ret = sock->state == SS_CONNECTED;
	release_sock(sock->sk);

	return ret;
}

/**
 * Sends a message to the VM & port the socket is connected to. All variants
 * of write/send/sendto/sendmsg eventually call this function.
 */
static int pg_sock_sendmsg(struct socket *sock, struct msghdr *m, size_t len)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int err;
	struct pg_msg_hdr *hdr;
	struct pg_sock *hsock = hsock_from_sk(sk);
	size_t payload_max_len = PG_MAILBOX_SIZE - sizeof(struct pg_msg_hdr);

	/* Check length. */
	if (len > payload_max_len)
		return -EMSGSIZE;

	/* We don't allow the destination address to be specified. */
	if (m->msg_namelen > 0)
		return -EISCONN;

	/* We don't support out of band messages. */
	if (m->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	/*
	 * Ensure that the socket is connected. We don't need to hold the socket
	 * lock (acquired and released by pg_sock_is_connected) for the
	 * remainder of the function because the fields we care about are
	 * immutable once the state is SS_CONNECTED.
	 */
	if (!pg_sock_is_connected(sock))
		return -ENOTCONN;

	/*
	 * Allocate an skb for this write. If there isn't enough room in the
	 * socket's send buffer (sk_wmem_alloc >= sk_sndbuf), this will block
	 * (if it's a blocking call). On success, it increments sk_wmem_alloc
	 * and sets up the skb such that sk_wmem_alloc gets decremented when
	 * the skb is freed (sock_wfree gets called).
	 */
	skb = sock_alloc_send_skb(sk, len + sizeof(struct pg_msg_hdr),
				  m->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return err;

	/* Reserve room for the header and initialise it. */
	skb_reserve(skb, sizeof(struct pg_msg_hdr));
	hdr = skb_push(skb, sizeof(struct pg_msg_hdr));
	hdr->src_port = hsock->local_port;
	hdr->dst_port = hsock->remote_port;

	/* Allocate area for the contents, then copy into skb. */
	if (!copy_from_iter_full(skb_put(skb, len), len, &m->msg_iter)) {
		err = -EFAULT;
		goto err_cleanup;
	}

	/*
	 * TODO: We currently do this inline, but when we have support for
	 * readiness notification from Hafnium, we must add this to a per-VM tx
	 * queue that can make progress when the VM becomes writable. This will
	 * fix send buffering and poll readiness notification.
	 */
	err = pg_send_skb(skb);
	if (err)
		goto err_cleanup;

	return 0;

err_cleanup:
	kfree_skb(skb);
	return err;
}

/**
 * Receives a message originated from the VM & port the socket is connected to.
 * All variants of read/recv/recvfrom/recvmsg eventually call this function.
 */
static int pg_sock_recvmsg(struct socket *sock, struct msghdr *m, size_t len,
			   int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int err;
	size_t copy_len;

	if (!pg_sock_is_connected(sock))
		return -ENOTCONN;

	/* Grab the next skb from the receive queue. */
	skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &err);
	if (!skb)
		return err;

	/* Make sure we don't copy more than what fits in the output buffer. */
	copy_len = skb->len;
	if (copy_len > len) {
		copy_len = len;
		m->msg_flags |= MSG_TRUNC;
	}

	/* Make sure we don't overflow the return value type. */
	if (copy_len > INT_MAX) {
		copy_len = INT_MAX;
		m->msg_flags |= MSG_TRUNC;
	}

	/* Copy skb to output iterator, then free it. */
	err = skb_copy_datagram_msg(skb, 0, m, copy_len);
	skb_free_datagram(sk, skb);
	if (err)
		return err;

	return copy_len;
}

/**
 * This function is called when a Hafnium socket is created. It initialises all
 * state such that the caller will be able to connect the socket and then send
 * and receive messages through it.
 */
static int pg_sock_create(struct net *net, struct socket *sock, int protocol,
			  int kern)
{
	static const struct proto_ops ops = {
		.family = PF_HF,
		.owner = THIS_MODULE,
		.release = pg_sock_release,
		.bind = sock_no_bind,
		.connect = pg_sock_connect,
		.socketpair = sock_no_socketpair,
		.accept = sock_no_accept,
		.ioctl = sock_no_ioctl,
		.listen = sock_no_listen,
		.shutdown = sock_no_shutdown,
		// .setsockopt = sock_no_setsockopt,
		// .getsockopt = sock_no_getsockopt,
		.sendmsg = pg_sock_sendmsg,
		.recvmsg = pg_sock_recvmsg,
		.mmap = sock_no_mmap,
		.sendpage = sock_no_sendpage,
		.poll = datagram_poll,
	};
	struct sock *sk;

	if (sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	if (protocol != 0)
		return -EPROTONOSUPPORT;

	/*
	 * For now we only allow callers with sys admin capability to create
	 * Hafnium sockets.
	 */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* Allocate and initialise socket. */
	sk = sk_alloc(net, PF_HF, GFP_KERNEL, &pg_sock_proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	sk->sk_destruct = pg_sock_destruct;
	sock->ops = &ops;
	sock->state = SS_UNCONNECTED;

	return 0;
}

/**
 * Frees all resources, including threads, associated with the Hafnium driver.
 */
static void pg_free_resources(void)
{
	uint16_t i;
	ffa_vcpu_index_t j;

	/*
	 * First stop all worker threads. We need to do this before freeing
	 * resources because workers may reference each other, so it is only
	 * safe to free resources after they have all stopped.
	 */
	for (i = 0; i < pg_vm_count; i++) {
		struct pg_vm *vm = &pg_vms[i];

		for (j = 0; j < vm->vcpu_count; j++)
			kthread_stop(vm->vcpu[j].task);
	}

	/* Free resources. */
	for (i = 0; i < pg_vm_count; i++) {
		struct pg_vm *vm = &pg_vms[i];

		for (j = 0; j < vm->vcpu_count; j++)
			put_task_struct(vm->vcpu[j].task);
		kfree(vm->vcpu);
	}

	kfree(pg_vms);

	ffa_rx_release();
	if (pg_send_page) {
		__free_page(pg_send_page);
		pg_send_page = NULL;
	}
	if (pg_recv_page) {
		__free_page(pg_recv_page);
		pg_recv_page = NULL;
	}
}

/**
 * Handles the hypervisor timer interrupt.
 */
static irqreturn_t pg_nop_irq_handler(int irq, void *dev)
{
	/*
	 * No need to do anything, the interrupt only exists to return to the
	 * primary vCPU so that the virtual timer will be restored and fire as
	 * normal.
	 */
	return IRQ_HANDLED;
}

/**
 * Enables the hypervisor timer interrupt on a CPU, when it starts or after the
 * driver is first loaded.
 */
static int pg_starting_cpu(unsigned int cpu)
{
	if (pg_irq != 0) {
		/* Enable the interrupt, and set it to be edge-triggered. */
		enable_percpu_irq(pg_irq, IRQ_TYPE_EDGE_RISING);
	}

	return 0;
}

/**
 * Disables the hypervisor timer interrupt on a CPU when it is powered down.
 */
static int pg_dying_cpu(unsigned int cpu)
{
	if (pg_irq != 0) {
		/* Disable the interrupt while the CPU is asleep. */
		disable_percpu_irq(pg_irq);
	}

	return 0;
}

/**
 * Registers for the hypervisor timer interrupt.
 */
static int pg_int_driver_probe(struct platform_device *pdev)
{
	int irq;
	int ret;

	/*
	 * Register a handler for the hyperviser timer IRQ, as it is needed for
	 * Hafnium to emulate the virtual timer for Linux while a secondary vCPU
	 * is running.
	 */
	irq = platform_get_irq(pdev, ARCH_TIMER_HYP_PPI);
	if (irq < 0) {
		pr_err("Error getting hypervisor timer IRQ: %d\n", irq);
		return irq;
	}
	pg_irq = irq;

	ret = request_percpu_irq(irq, pg_nop_irq_handler, HYPERVISOR_TIMER_NAME,
				 pdev);
	if (ret != 0) {
		pr_err("Error registering hypervisor timer IRQ %d: %d\n",
		       irq, ret);
		return ret;
	}
	pr_info("Hafnium registered for IRQ %d\n", irq);
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"hafnium/hypervisor_timer:starting",
				pg_starting_cpu, pg_dying_cpu);
	if (ret < 0) {
		pr_err("Error enabling timer on all CPUs: %d\n", ret);
		free_percpu_irq(irq, pdev);
		return ret;
	}
	pg_cpuhp_state = ret;

	return 0;
}

/**
 * Unregisters for the hypervisor timer interrupt.
 */
static int pg_int_driver_remove(struct platform_device *pdev)
{
	/*
	 * This will cause pg_dying_cpu to be called on each CPU, which will
	 * disable the IRQs.
	 */
	cpuhp_remove_state(pg_cpuhp_state);
	free_percpu_irq(pg_irq, pdev);

	return 0;
}

static const struct of_device_id pg_int_driver_id[] = {
	{.compatible = "arm,armv7-timer"},
	{.compatible = "arm,armv8-timer"},
	{}
};

static struct platform_driver pg_int_driver = {
	.driver = {
		.name = HYPERVISOR_TIMER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(pg_int_driver_id),
	},
	.probe = pg_int_driver_probe,
	.remove = pg_int_driver_remove,
};

/**
 * Print the error code of the given FF-A value if it is an error, or the
 * function ID otherwise.
 */
static void print_ffa_error(struct ffa_value ffa_ret)
{
	if (ffa_ret.func == FFA_ERROR_32)
		pr_err("FF-A error code %d\n", ffa_ret.arg2);
	else
		pr_err("Unexpected FF-A function %#x\n", ffa_ret.func);
}

/**
 * Initializes the Hafnium driver by creating a thread for each vCPU of each
 * virtual machine.
 */
static int __init pg_init(void)
{
	static const struct net_proto_family proto_family = {
		.family = PF_HF,
		.create = pg_sock_create,
		.owner = THIS_MODULE,
	};
	int64_t ret;
	struct ffa_value ffa_ret;
	ffa_vm_id_t i;
	ffa_vcpu_index_t j;
	struct ffa_uuid null_uuid;
	ffa_vm_count_t secondary_vm_count;
	const struct ffa_partition_info *partition_info;
	uint32_t total_vcpu_count;

	/* Allocate a page for send and receive buffers. */
	pg_send_page = alloc_page(GFP_KERNEL);
	if (!pg_send_page) {
		pr_err("Unable to allocate send buffer\n");
		return -ENOMEM;
	}

	pg_recv_page = alloc_page(GFP_KERNEL);
	if (!pg_recv_page) {
		__free_page(pg_send_page);
		pg_send_page = NULL;
		pr_err("Unable to allocate receive buffer\n");
		return -ENOMEM;
	}

	/*
	 * Configure both addresses. Once configured, we cannot free these pages
	 * because the hypervisor will use them, even if the module is
	 * unloaded.
	 */
	ffa_ret = ffa_rxtx_map(page_to_phys(pg_send_page),
				 page_to_phys(pg_recv_page));
	if (ffa_ret.func != FFA_SUCCESS_32) {
		pr_err("Unable to configure VM mailbox.\n");
		print_ffa_error(ffa_ret);
		ret = -EIO;
		goto fail_with_cleanup;
	}

	/* Get information about secondary VMs. */
	ffa_uuid_init(0, 0, 0, 0, &null_uuid);
	ffa_ret = ffa_partition_info_get(&null_uuid);
	if (ffa_ret.func != FFA_SUCCESS_32) {
		pr_err("Unable to get VM information.\n");
		print_ffa_error(ffa_ret);
		ret = -EIO;
		goto fail_with_cleanup;
	}
	secondary_vm_count = ffa_ret.arg2 - 1;
	partition_info = page_address(pg_recv_page);

	pr_info("secondary_vm_count: %d\n", secondary_vm_count);

	/* Confirm the maximum number of VMs looks sane. */
	BUILD_BUG_ON(CONFIG_HAFNIUM_MAX_VMS < 1);
	BUILD_BUG_ON(CONFIG_HAFNIUM_MAX_VMS > U16_MAX);

	/* Validate the number of VMs. There must at least be the primary. */
	if (secondary_vm_count > CONFIG_HAFNIUM_MAX_VMS - 1) {
		pr_err("Number of VMs is out of range: %d\n",
		       secondary_vm_count);
		ret = -EDQUOT;
		goto fail_with_cleanup;
	}

	/* Only track the secondary VMs. */
	pg_vms = kmalloc_array(secondary_vm_count, sizeof(struct pg_vm),
			       GFP_KERNEL);
	if (!pg_vms) {
		ret = -ENOMEM;
		goto fail_with_cleanup;
	}

	/* Cache the VM id for later usage. */
	current_vm_id = pg_vm_get_id();

	/* Initialize each VM. */
	total_vcpu_count = 0;
	for (i = 0; i < secondary_vm_count; i++) {
		struct pg_vm *vm = &pg_vms[i];
		ffa_vcpu_count_t vcpu_count;

		/* Adjust the index as only the secondaries are tracked. */
		vm->id = partition_info[i + 1].vm_id;
		vcpu_count = partition_info[i + 1].vcpu_count;

		/* Avoid overflowing the vcpu count. */
		if (vcpu_count > (U32_MAX - total_vcpu_count)) {
			pr_err("Too many vcpus: %u\n", total_vcpu_count);
			ret = -EDQUOT;
			goto fail_with_cleanup;
		}

		/* Confirm the maximum number of VCPUs looks sane. */
		BUILD_BUG_ON(CONFIG_HAFNIUM_MAX_VCPUS < 1);
		BUILD_BUG_ON(CONFIG_HAFNIUM_MAX_VCPUS > U16_MAX);

		/* Enforce the limit on vcpus. */
		total_vcpu_count += vcpu_count;
		if (total_vcpu_count > CONFIG_HAFNIUM_MAX_VCPUS) {
			pr_err("Too many vcpus: %u\n", total_vcpu_count);
			ret = -EDQUOT;
			goto fail_with_cleanup;
		}

		vm->vcpu_count = vcpu_count;
		vm->vcpu = kmalloc_array(vm->vcpu_count, sizeof(struct pg_vcpu),
					 GFP_KERNEL);
		if (!vm->vcpu) {
			ret = -ENOMEM;
			goto fail_with_cleanup;
		}

		/* Update the number of initialized VMs. */
		pg_vm_count = i + 1;

		/* Create a kernel thread for each vcpu. */
		for (j = 0; j < vm->vcpu_count; j++) {
			struct pg_vcpu *vcpu = &vm->vcpu[j];

			vcpu->task =
				kthread_create(pg_vcpu_thread, vcpu,
					       "vcpu_thread_%u_%u", vm->id, j);
			if (IS_ERR(vcpu->task)) {
				pr_err("Error creating task (vm=%u,vcpu=%u): %ld\n",
				       vm->id, j, PTR_ERR(vcpu->task));
				vm->vcpu_count = j;
				ret = PTR_ERR(vcpu->task);
				goto fail_with_cleanup;
			}

			get_task_struct(vcpu->task);
			vcpu->vm = vm;
			vcpu->vcpu_index = j;
			atomic_set(&vcpu->abort_sleep, 0);
			atomic_set(&vcpu->waiting_for_message, 0);
		}
	}

	ffa_ret = ffa_rx_release();
	if (ffa_ret.func != FFA_SUCCESS_32) {
		pr_err("Unable to release RX buffer.\n");
		print_ffa_error(ffa_ret);
		ret = -EIO;
		goto fail_with_cleanup;
	}

	/* Register protocol and socket family. */
	ret = proto_register(&pg_sock_proto, 0);
	if (ret) {
		pr_err("Unable to register protocol: %lld\n", ret);
		goto fail_with_cleanup;
	}

	ret = sock_register(&proto_family);
	if (ret) {
		pr_err("Unable to register Hafnium's socket family: %lld\n",
		       ret);
		goto fail_unregister_proto;
	}

	/*
	 * Register as a driver for the timer device, so we can register a
	 * handler for the hyperviser timer IRQ.
	 */
	ret = platform_driver_register(&pg_int_driver);
	if (ret != 0) {
		pr_err("Error registering timer driver %lld\n", ret);
		goto fail_unregister_socket;
	}

	/*
	 * Start running threads now that all is initialized.
	 *
	 * Any failures from this point on must also unregister the driver with
	 * platform_driver_unregister().
	 */
	for (i = 0; i < pg_vm_count; i++) {
		struct pg_vm *vm = &pg_vms[i];

		for (j = 0; j < vm->vcpu_count; j++)
			wake_up_process(vm->vcpu[j].task);
	}

	/* Dump vm/vcpu count info. */
	pr_info("Hafnium successfully loaded with %u VMs:\n", pg_vm_count);
	for (i = 0; i < pg_vm_count; i++) {
		struct pg_vm *vm = &pg_vms[i];

		pr_info("\tVM %u: %u vCPUS\n", vm->id, vm->vcpu_count);
	}

	pr_info("+42\n");

	return 0;

fail_unregister_socket:
	sock_unregister(PF_HF);
fail_unregister_proto:
	proto_unregister(&pg_sock_proto);
fail_with_cleanup:
	pg_free_resources();
	return ret;
}

/**
 * Frees up all resources used by the Hafnium driver in preparation for
 * unloading it.
 */
static void __exit pg_exit(void)
{
	pr_info("Preparing to unload Hafnium\n");
	sock_unregister(PF_HF);
	proto_unregister(&pg_sock_proto);
	pg_free_resources();
	platform_driver_unregister(&pg_int_driver);
	pr_info("Hafnium ready to unload\n");
	pr_info("-42\n");
}

MODULE_LICENSE("GPL v2");

module_init(pg_init);
module_exit(pg_exit);
