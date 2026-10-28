// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/srcu.h>
#include <linux/smp.h>
#include "../include/fpsgo_private.h"

DEFINE_STATIC_SRCU(private_readers);
static DEFINE_MUTEX(registration_lock);
static DEFINE_MUTEX(private_state_lock);
static bool bound;
static bool online;

int fpsgo_private_enter(void)
{
	int idx = srcu_read_lock(&private_readers);

	if (!smp_load_acquire(&online))
		goto closed;
	mutex_lock(&private_state_lock);
	if (!smp_load_acquire(&online)) {
		mutex_unlock(&private_state_lock);
		goto closed;
	}
	return idx;
closed:
	srcu_read_unlock(&private_readers, idx);
	return -ESHUTDOWN;
}

void fpsgo_private_leave(int idx)
{
	mutex_unlock(&private_state_lock);
	srcu_read_unlock(&private_readers, idx);
}

int fpsgo_private_begin(void)
{
	int ret = 0;

	mutex_lock(&registration_lock);
	if (bound)
		ret = -EBUSY;
	else {
		bound = true;
		smp_store_release(&online, false);
	}
	mutex_unlock(&registration_lock);
	return ret;
}
EXPORT_SYMBOL(fpsgo_private_begin);

int fpsgo_private_commit(void)
{
	int ret = 0;

	mutex_lock(&registration_lock);
	if (!bound || online)
		ret = -EINVAL;
	else
		/* Publish all initialized algorithm pointers and state together. */
		smp_store_release(&online, true);
	mutex_unlock(&registration_lock);
	return ret;
}
EXPORT_SYMBOL(fpsgo_private_commit);

void fpsgo_private_revoke(void)
{
	mutex_lock(&registration_lock);
	if (!bound)
		goto out;
	smp_store_release(&online, false);
	/* Ring writers are kernel tracepoint probes, not callback-lease readers.
	 * Unregister and synchronize them before releasing private BSS storage.
	 */
	fpsgo_xgf_private_stop();
	synchronize_srcu(&private_readers);
	fpsgo_base_private_clear();
	fpsgo_fbt_private_clear();
	fpsgo_fstb_private_clear();
	fpsgo_xgf_private_clear();
	fpsgo_composer_private_clear();
	bound = false;
out:
	mutex_unlock(&registration_lock);
}
EXPORT_SYMBOL(fpsgo_private_revoke);
