// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 MediaTek Inc.
 * Maintained from the authenticated MT6878 donor in ../../DONOR.json.
 */
#include "../../common/include/c2ps_common.h"
#include "../include/c2ps_uclamp_regulator.h"
#include "../include/c2ps_regulator_policy.h"
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

static struct workqueue_struct *regulator_wq;
static struct kmem_cache *regulator_reqs;
static atomic_t pending = ATOMIC_INIT(0);
static int c2ps_regulator_process_mode;
static unsigned int c2ps_remote_monitor_proc_time;
static unsigned int c2ps_remote_monitor_uclamp;
static char c2ps_remote_monitor_task[30] = "None";
static int set_process_mode(const char *value, const struct kernel_param *kp)
{
	int mode, ret = kstrtoint(value, 0, &mode);

	if (ret)
		return ret;
	/* Background mode is selected internally only for a request without a
	 * task. Invalid modes must not silently discard registered frame work.
	 */
	if (mode < C2PS_REGULATOR_MODE_FIX || mode > C2PS_REGULATOR_MODE_DEBUG)
		return -EINVAL;
	WRITE_ONCE(*(int *)kp->arg, mode);
	return 0;
}
static const struct kernel_param_ops process_mode_ops = {
	.set = set_process_mode,
	.get = param_get_int,
};
module_param_cb(c2ps_regulator_process_mode, &process_mode_ops,
	&c2ps_regulator_process_mode, 0644);
module_param(c2ps_remote_monitor_proc_time, uint, 0444);
module_param(c2ps_remote_monitor_uclamp, uint, 0444);
module_param_string(c2ps_remote_monitor_task, c2ps_remote_monitor_task, 30, 0644);

static void regulator_process(struct work_struct *work)
{
	struct regulator_req *req = container_of(work, struct regulator_req, work);
	int mode = req->tsk_info ? READ_ONCE(c2ps_regulator_process_mode) :
		C2PS_REGULATOR_BGMODE_SIMPLE;

	if (req->tsk_info) {
		mutex_lock(&req->tsk_info->mlock);
		if (!req->tsk_info->is_active || !req->tsk_info->task ||
		    req->frame_seq != req->tsk_info->frame_seq)
			goto release_task;
	}
	if (req->tsk_info &&
	    strstr(c2ps_remote_monitor_task, req->tsk_info->task_name)) {
		c2ps_remote_monitor_uclamp = req->tsk_info->latest_uclamp;
		c2ps_remote_monitor_proc_time = req->tsk_info->hist_proc_time_sum /
			clamp_t(int, READ_ONCE(proc_time_window_size), 1, MAX_WINDOW_SIZE);
	}
	switch (mode) {
	case C2PS_REGULATOR_MODE_FIX:
		c2ps_regulator_policy_fix_uclamp(req);
		break;
	case C2PS_REGULATOR_MODE_SIMPLE:
		c2ps_regulator_policy_simple(req);
		break;
	case C2PS_REGULATOR_MODE_DEBUG:
		c2ps_regulator_policy_debug_uclamp(req);
		break;
	case C2PS_REGULATOR_BGMODE_SIMPLE:
		if (!req->glb_info)
			break;
		mutex_lock(&req->glb_info->mlock);
		c2ps_regulator_bgpolicy_simple(req);
		mutex_unlock(&req->glb_info->mlock);
		break;
	default:
		break;
	}
release_task:
	if (req->tsk_info)
		mutex_unlock(&req->tsk_info->mlock);
	kmem_cache_free(regulator_reqs, req);
	atomic_dec(&pending);
}

/* Parent owns admission: stop/join its notifier and cancel its timers before
 * exit. During a session stop the sole notifier producer is serialized with
 * this allocation-free barrier. No interruptible flush token can escape and
 * leave task/global pointers in flight while their tables are destroyed.
 */
void c2ps_uclamp_regulator_flush(void)
{
	if (regulator_wq)
		flush_workqueue(regulator_wq);
}

struct regulator_req *get_regulator_req(void)
{
	struct regulator_req *req;

	if (atomic_inc_return(&pending) > 128) {
		atomic_dec(&pending);
		return NULL;
	}
	req = kmem_cache_zalloc(regulator_reqs, GFP_KERNEL);
	if (!req) {
		atomic_dec(&pending);
		return NULL;
	}
	INIT_WORK(&req->work, regulator_process);
	return req;
}

void send_regulator_req(struct regulator_req *req)
{
	if (req)
		queue_work(regulator_wq, &req->work);
}

int uclamp_regulator_init(void)
{
	regulator_reqs = kmem_cache_create("regulator_reqs",
		sizeof(struct regulator_req), 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!regulator_reqs)
		return -ENOMEM;
	regulator_wq = alloc_ordered_workqueue("c2ps_regulator", WQ_MEM_RECLAIM);
	if (!regulator_wq) {
		kmem_cache_destroy(regulator_reqs);
		regulator_reqs = NULL;
		return -ENOMEM;
	}
	return 0;
}

void uclamp_regulator_exit(void)
{
	if (regulator_wq) {
		destroy_workqueue(regulator_wq);
		regulator_wq = NULL;
	}
	if (regulator_reqs) {
		kmem_cache_destroy(regulator_reqs);
		regulator_reqs = NULL;
	}
}
