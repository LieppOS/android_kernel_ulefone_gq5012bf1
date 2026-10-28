/* SPDX-License-Identifier: GPL-2.0 */
/* Native basic-VIP/current-hint ownership, embedded in nonlinear_opp_cap.c.
 * No task_struct or vendor-data layout change; all registry access is locked.
 * This does NOT arbitrate the scheduler's separate uclamp_req[] fields.
 */
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
#include <linux/hashtable.h>
#include <linux/sched/task.h>

struct mtk_perf_task_lease {
	struct hlist_node node;
	struct task_struct *task;
	int saved_hint;
	bool saved_vip;
};
static DEFINE_HASHTABLE(mtk_perf_task_leases, 6);
static DEFINE_RAW_SPINLOCK(mtk_perf_task_lock);

static struct mtk_perf_task_lease *mtk_perf_task_find_locked(struct task_struct *task)
{
	struct mtk_perf_task_lease *lease;

	lockdep_assert_held(&mtk_perf_task_lock);
	hash_for_each_possible(mtk_perf_task_leases, lease, node, (unsigned long)task)
		if (lease->task == task)
			return lease;
	return NULL;
}

static void mtk_perf_task_restore_locked(struct mtk_perf_task_lease *lease)
{
	struct mtk_task *mt = (struct mtk_task *)lease->task->android_vendor_data1;

	lockdep_assert_held(&mtk_perf_task_lock);
	/* No policy update is meaningful once the retained task has exited. */
	if (!pid_alive(lease->task))
		return;
	WRITE_ONCE(mt->cu_hint.hint, lease->saved_hint);
#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
	WRITE_ONCE(mt->vip_task.basic_vip, lease->saved_vip);
#endif
}

static void mtk_perf_task_preempt_locked(struct task_struct *task)
{
	struct mtk_perf_task_lease *lease = mtk_perf_task_find_locked(task);

	if (lease) {
		mtk_perf_task_restore_locked(lease);
		hash_del(&lease->node);
	}
	/* Caller owns the reference and frees the displaced descriptor later. */
}

int mtk_perf_task_acquire(struct task_struct *task, bool vip,
	struct mtk_perf_task_lease **out)
{
	struct mtk_perf_task_lease *lease;
	struct mtk_task *mt;
	unsigned long flags;
	int ret = 0;

	if (!out)
		return -EINVAL;
	*out = NULL;
	if (!task || !pid_alive(task))
		return -ESRCH;
	if (READ_ONCE(task->flags) & PF_KTHREAD)
		return -EPERM;
	if (vip && !IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK))
		return -EOPNOTSUPP;
	lease = kzalloc(sizeof(*lease), GFP_KERNEL);
	if (!lease)
		return -ENOMEM;
	get_task_struct(task);
	lease->task = task;
	mt = (struct mtk_task *)task->android_vendor_data1;
	raw_spin_lock_irqsave(&mtk_perf_task_lock, flags);
	if (!pid_alive(task))
		ret = -ESRCH;
	else if (mtk_perf_task_find_locked(task))
		ret = -EBUSY;
	if (!ret) {
		lease->saved_hint = READ_ONCE(mt->cu_hint.hint);
#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
		lease->saved_vip = READ_ONCE(mt->vip_task.basic_vip);
		if (vip)
			WRITE_ONCE(mt->vip_task.basic_vip, true);
#endif
		hash_add(mtk_perf_task_leases, &lease->node, (unsigned long)task);
	}
	raw_spin_unlock_irqrestore(&mtk_perf_task_lock, flags);
	if (ret) {
		put_task_struct(task);
		kfree(lease);
	} else {
		*out = lease;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_perf_task_acquire);

int mtk_perf_task_set_hint(struct mtk_perf_task_lease *lease, int hint)
{
	unsigned long flags;
	int ret = 0;

	if (!lease || (hint != 0 && hint != 1))
		return -EINVAL;
	raw_spin_lock_irqsave(&mtk_perf_task_lock, flags);
	if (hlist_unhashed(&lease->node))
		ret = -ESTALE;
	else if (!pid_alive(lease->task))
		ret = -ESRCH;
	else
		WRITE_ONCE(((struct mtk_task *)lease->task->android_vendor_data1)->cu_hint.hint, hint);
	raw_spin_unlock_irqrestore(&mtk_perf_task_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_perf_task_set_hint);

bool mtk_perf_task_is_live(struct mtk_perf_task_lease *lease)
{
	unsigned long flags;
	bool live;

	if (!lease)
		return false;
	raw_spin_lock_irqsave(&mtk_perf_task_lock, flags);
	live = !hlist_unhashed(&lease->node) && pid_alive(lease->task);
	raw_spin_unlock_irqrestore(&mtk_perf_task_lock, flags);
	return live;
}
EXPORT_SYMBOL_GPL(mtk_perf_task_is_live);

void mtk_perf_task_release(struct mtk_perf_task_lease *lease)
{
	unsigned long flags;

	if (!lease)
		return;
	raw_spin_lock_irqsave(&mtk_perf_task_lock, flags);
	if (!hlist_unhashed(&lease->node)) {
		mtk_perf_task_restore_locked(lease);
		hash_del(&lease->node);
	}
	raw_spin_unlock_irqrestore(&mtk_perf_task_lock, flags);
	put_task_struct(lease->task);
	kfree(lease);
}
EXPORT_SYMBOL_GPL(mtk_perf_task_release);

void mtk_perf_task_legacy_hint(struct task_struct *task, int hint)
{
	unsigned long flags;

	/* Legacy entry already owns a task ref. Preemption and the write are
	 * indivisible even for identical values; no PID relookup or sleeping.
	 */
	raw_spin_lock_irqsave(&mtk_perf_task_lock, flags);
	mtk_perf_task_preempt_locked(task);
	WRITE_ONCE(((struct mtk_task *)task->android_vendor_data1)->cu_hint.hint, hint);
	raw_spin_unlock_irqrestore(&mtk_perf_task_lock, flags);
}
EXPORT_SYMBOL_GPL(mtk_perf_task_legacy_hint);

#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
void mtk_perf_task_legacy_vip(struct task_struct *task, bool vip)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&mtk_perf_task_lock, flags);
	mtk_perf_task_preempt_locked(task);
	WRITE_ONCE(((struct mtk_task *)task->android_vendor_data1)->vip_task.basic_vip, vip);
	raw_spin_unlock_irqrestore(&mtk_perf_task_lock, flags);
}
EXPORT_SYMBOL_GPL(mtk_perf_task_legacy_vip);
#endif
#endif
