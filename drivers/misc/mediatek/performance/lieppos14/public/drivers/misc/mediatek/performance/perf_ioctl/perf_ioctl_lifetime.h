/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LIEPPOS_PERF_IOCTL_LIFETIME_H
#define LIEPPOS_PERF_IOCTL_LIFETIME_H

#include <linux/fs.h>

/* Source-owned lifetime protocol. This is not a stock callback ABI witness.
 * Bind after constructing all wait queues and before publishing any slot.
 * Publish only once all slots are installed. Revoke closes admission, wakes
 * waiters, drains leases, and only then clears the provider's publications.
 * Revoke must not be called from a leased callback or holding a queue lock.
 */
int perf_ioctl_dispatch_bind(void (*stop_waiters)(void), void (*clear_slots)(void));
void perf_ioctl_dispatch_publish(void);
void perf_ioctl_dispatch_revoke(void);
int perf_ioctl_dispatch_enter(void);
long perf_ioctl_dispatch_leave(int index, long result);

static inline long perf_ioctl_dispatch_call(
	long (*handler)(struct file *, unsigned int, unsigned long),
	struct file *file, unsigned int cmd, unsigned long arg)
{
	int index = perf_ioctl_dispatch_enter();
	long ret;

	if (index < 0)
		return index;
	ret = handler(file, cmd, arg);
	return perf_ioctl_dispatch_leave(index, ret);
}

#endif
