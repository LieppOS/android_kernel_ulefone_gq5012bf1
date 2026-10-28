// SPDX-License-Identifier: GPL-2.0
/* C2PS referenced-task adapter for pinned Target C.
 * Native hint/basic-VIP state and scheduler clamps use referenced-task leases.
 * Ordinary writers preempt inside the real provider/scheduler locks.
 * No numeric-PID lookup is permitted after notifier admission.
 */
#include "../include/c2ps_common.h"
#include <linux/rcupdate.h>
#include <linux/limits.h>

struct task_struct *c2ps_get_task(int pid)
{
	struct task_struct *task = NULL;

	if (pid <= 0)
		return NULL;
	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();
	return task;
}

/* Notifier and regulator serialize on info->mlock. A request's frame_seq
 * prevents old queued policy from acting on a new frame of the same task id.
 */
int c2ps_task_begin_locked(struct c2ps_task_info *info, struct task_struct *task)
{
	struct mtk_perf_task_lease *native_task;
	struct sched_uclamp_lease *uclamp_lease;
	struct pid *identity;
	int ret;

	lockdep_assert_held(&info->mlock);
	if (info->is_active)
		return -EALREADY;
	if (!task || !pid_alive(task))
		return -ESRCH;
	/* Camera requests may not boost kernel/stopper threads. */
	if (READ_ONCE(task->flags) & PF_KTHREAD)
		return -EPERM;
	identity = get_task_pid(task, PIDTYPE_PID);
	if (!identity)
		return -ESRCH;
	if (info->identity && !info->is_dynamic_tid && info->identity != identity) {
		put_pid(identity);
		return -ESTALE;
	}
	/* Do not replace a static/dynamic identity or publish a frame if the
	 * native state is owned by a different frame/client or allocation fails.
	 */
	ret = sched_uclamp_lease_acquire(task, &uclamp_lease);
	if (ret) {
		put_pid(identity);
		return ret;
	}
	ret = mtk_perf_task_acquire(task, info->is_vip_task, &native_task);
	if (ret) {
		sched_uclamp_lease_release(uclamp_lease);
		put_pid(identity);
		return ret;
	}
	put_pid(info->identity);
	info->identity = identity;
	info->native_task = native_task;
	info->uclamp_lease = uclamp_lease;
	get_task_struct(task);
	info->task = task;
	info->pid = task_pid_nr(task);
	info->frame_seq++;
	info->is_active = true;
	return 0;
}

int c2ps_apply_task_clamp(struct regulator_req *req, unsigned int max_util,
	unsigned int min_util)
{
	struct c2ps_task_info *info = req->tsk_info;
	unsigned int min, max;
	struct task_struct *task = info->task;
	int ret;

	lockdep_assert_held(&info->mlock);
	if (!info->is_active || req->frame_seq != info->frame_seq ||
	    !task || !pid_alive(task))
		return -ESRCH;
	if (!mtk_perf_task_is_live(info->native_task)) {
		ret = -ESTALE;
		goto revoked;
	}
	min = clamp(min_util, 1U, 1024U);
	max = clamp(max_util, 1U, 1024U);
	if (min > max)
		return -EINVAL;
	ret = sched_uclamp_lease_apply(info->uclamp_lease, min, max);
	if (ret)
		goto revoked;
	ret = mtk_perf_task_set_hint(info->native_task, 1);
revoked:
	/* Drop the other half of a split task-resource transaction immediately
	 * when either native writer preempts. Rejected input/validation leaves
	 * a still-owned frame intact; no unrelated policy is reset on error.
	 */
	if (ret == -ESTALE)
		c2ps_task_release_locked(info);
	return ret;
}

void c2ps_task_release_locked(struct c2ps_task_info *info)
{
	struct task_struct *task = info->task;

	lockdep_assert_held(&info->mlock);
	info->is_active = false;
	info->frame_seq++;
	info->task = NULL;
	if (!task)
		return;
	sched_uclamp_lease_release(info->uclamp_lease);
	info->uclamp_lease = NULL;
	mtk_perf_task_release(info->native_task);
	info->native_task = NULL;
	put_task_struct(task);
}
