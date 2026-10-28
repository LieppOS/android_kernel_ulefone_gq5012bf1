/* SPDX-License-Identifier: GPL-2.0 */
/* Included by native group.c after add/remove_task_to_group(). Lock order is
 * task pi -> related_thread_group_lock -> native group/rq. CPUQoS calls/IPIs,
 * allocation, task puts and frees are deliberately OUTSIDE these locks.
 */
#include <linux/hashtable.h>
#include <linux/sched/task.h>

static int mtk_group_change_locked(struct task_struct *task, int group_id,
	bool allow_switch)
{
	struct gp_task_struct *gts =
		&((struct mtk_task *)task->android_vendor_data1)->gp_task;
	struct grp *next = group_id < 0 ? NULL : lookup_grp(group_id);

	lockdep_assert_held(&task->pi_lock);
	lockdep_assert_held(&related_thread_group_lock);
	if (group_id >= GROUP_ID_RECORD_MAX || (group_id >= 0 && !next))
		return -EINVAL;
	if ((!gts->grp && group_id < 0) ||
		(gts->grp && gts->grp == next))
		return 0;
	/* Preserve the legacy entry's no-direct-switch rule. Tokens explicitly
	 * remove/add under the same pi/group-writer lock to install or restore.
	 */
	if (gts->grp && group_id >= 0 && !allow_switch)
		return 0;
	if (gts->grp)
		remove_task_from_group(task);
	return next ? add_task_to_group(task, next) : 0;
}

#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
struct mtk_group_task_lease {
	struct hlist_node node;
	struct task_struct *task;
	struct mtk_cpuqos_task_lease *pd;
	int saved_group;
	bool saved_customized;
};
static DEFINE_HASHTABLE(mtk_group_task_leases, 6);

static bool mtk_group_task_alive(struct task_struct *task)
{
	return task && pid_alive(task) && !READ_ONCE(task->exit_state) &&
		!(READ_ONCE(task->flags) & (PF_KTHREAD | PF_EXITING));
}

static struct mtk_group_task_lease *mtk_group_find_locked(struct task_struct *task)
{
	struct mtk_group_task_lease *lease;

	lockdep_assert_held(&related_thread_group_lock);
	hash_for_each_possible(mtk_group_task_leases, lease, node, (unsigned long)task)
		if (lease->task == task)
			return lease;
	return NULL;
}

static void mtk_group_restore_locked(struct mtk_group_task_lease *lease)
{
	struct gp_task_struct *gts =
		&((struct mtk_task *)lease->task->android_vendor_data1)->gp_task;
	int group;

	lockdep_assert_held(&related_thread_group_lock);
	if (!mtk_group_task_alive(lease->task))
		return;
	/* Automatic membership follows the CURRENT cgroup, which may have changed
	 * while a temporary customized overlay suppressed automatic assignment.
	 * Explicit/manual baseline membership instead restores its saved group.
	 */
	group = lease->saved_customized ? lease->saved_group :
		cgrp_to_grpid(lease->task);
	mtk_group_change_locked(lease->task, group, true);
	WRITE_ONCE(gts->customized, lease->saved_customized);
}

static bool mtk_group_preempt_locked(struct task_struct *task)
{
	struct mtk_group_task_lease *lease = mtk_group_find_locked(task);

	if (lease) {
		mtk_group_restore_locked(lease);
		hash_del(&lease->node);
	}
	/* Caller owns the displaced descriptor and its CPUQoS token until release.
	 * The ordinary custom-group API subsequently revokes PD through its typed
	 * ordinary setter. A cgroup writer does not itself mutate task PD.
	 */
	return lease != NULL;
}
#endif

/* Automatic wake/cgroup assignment is NOT an ordinary override. The callback's
 * lockless customized fast path can race an ADPF acquisition: recheck under the
 * same pi/group writer locks used to publish customized plus membership. Never
 * preempt a custom owner merely because its flag was false before we locked.
 * Included before native group.c's automatic callback entry points.
 */
static int mtk_group_auto_set(struct task_struct *task, int group_id)
{
	struct gp_task_struct *gts =
		&((struct mtk_task *)task->android_vendor_data1)->gp_task;
	unsigned long flags;
	int ret = 0;

	if (group_id >= GROUP_ID_RECORD_MAX)
		return -EINVAL;
	raw_spin_lock_irqsave(&task->pi_lock, flags);
	write_lock(&related_thread_group_lock);
	if (!READ_ONCE(gts->customized) && group_get_mode() != GP_MODE_0)
		ret = mtk_group_change_locked(task, group_id, false);
	write_unlock(&related_thread_group_lock);
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
	return ret;
}

int __sched_set_grp_id(struct task_struct *task, int group_id)
{
	unsigned long flags;
	bool displaced = false;
	int ret;

	if (group_id >= GROUP_ID_RECORD_MAX)
		return -1;
	raw_spin_lock_irqsave(&task->pi_lock, flags);
	write_lock(&related_thread_group_lock);
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
	displaced = mtk_group_preempt_locked(task);
#endif
	/* Replacing a temporary overlay must not make a same-value ordinary
	 * write turn into a no-op against the restored, different baseline.
	 */
	ret = mtk_group_change_locked(task, group_id, displaced);
	write_unlock(&related_thread_group_lock);
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
	return ret;
}

static int mtk_group_custom_set(struct task_struct *task, int group_id)
{
	struct gp_task_struct *gts =
		&((struct mtk_task *)task->android_vendor_data1)->gp_task;
	unsigned long flags;
	bool displaced = false;
	int ret;

	raw_spin_lock_irqsave(&task->pi_lock, flags);
	write_lock(&related_thread_group_lock);
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
	displaced = mtk_group_preempt_locked(task);
#endif
	/* This flag and the actual group share the owner lock. A token cannot
	 * save half of an ordinary writer's customized/group update.
	 */
	WRITE_ONCE(gts->customized, group_id >= 0);
	ret = mtk_group_change_locked(task, group_id, displaced);
	write_unlock(&related_thread_group_lock);
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
	return ret;
}

#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
int mtk_group_task_acquire(struct task_struct *task, int group_id,
	struct mtk_group_task_lease **out)
{
	struct mtk_group_task_lease *lease;
	struct gp_task_struct *gts;
	unsigned long flags;
	int ret;

	if (!out)
		return -EINVAL;
	*out = NULL;
	if (group_id < 0 || group_id >= GROUP_ID_RECORD_MAX)
		return -EINVAL;
	if (!mtk_group_task_alive(task))
		return -ESRCH;
	if (group_get_mode() == GP_MODE_0 || !lookup_grp(group_id))
		return -EOPNOTSUPP;
	lease = kzalloc(sizeof(*lease), GFP_KERNEL);
	if (!lease)
		return -ENOMEM;
	get_task_struct(task);
	lease->task = task;
	/* Genuine native PD ownership, not a numeric setter or compare/restore.
	 * On later group failure this token restores only if it is still owner.
	 */
	ret = mtk_cpuqos_task_acquire(task, group_id + FLT_GROUP_START_IDX,
		&lease->pd);
	if (ret)
		goto fail;
	gts = &((struct mtk_task *)task->android_vendor_data1)->gp_task;
	raw_spin_lock_irqsave(&task->pi_lock, flags);
	write_lock(&related_thread_group_lock);
	if (!mtk_group_task_alive(task))
		ret = -ESRCH;
	else if (group_get_mode() == GP_MODE_0 || !lookup_grp(group_id))
		ret = -EOPNOTSUPP;
	else if (mtk_group_find_locked(task))
		ret = -EBUSY;
	else {
		lease->saved_group = gts->grp ? gts->grp->id : -1;
		lease->saved_customized = READ_ONCE(gts->customized);
		ret = mtk_group_change_locked(task, group_id, true);
		if (!ret) {
			WRITE_ONCE(gts->customized, true);
			hash_add(mtk_group_task_leases, &lease->node, (unsigned long)task);
		}
	}
	write_unlock(&related_thread_group_lock);
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
	if (ret) {
		mtk_cpuqos_task_release(lease->pd);
		goto fail;
	}
	/* The two providers are NOT one atomic cross-module transaction. Detect
	 * an ordinary PD writer racing group installation and unwind our half.
	 * Clients must keep checking liveness; later preemption is always possible.
	 */
	if (!mtk_cpuqos_task_is_live(lease->pd)) {
		mtk_group_task_release(lease);
		return -ESTALE;
	}
	*out = lease;
	return 0;
fail:
	put_task_struct(task);
	kfree(lease);
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_group_task_acquire);

bool mtk_group_task_is_live(struct mtk_group_task_lease *lease)
{
	unsigned long flags;
	bool live;

	if (!lease || !mtk_cpuqos_task_is_live(lease->pd))
		return false;
	raw_spin_lock_irqsave(&lease->task->pi_lock, flags);
	read_lock(&related_thread_group_lock);
	live = !hlist_unhashed(&lease->node) &&
		mtk_group_task_alive(lease->task) && group_get_mode() != GP_MODE_0;
	read_unlock(&related_thread_group_lock);
	raw_spin_unlock_irqrestore(&lease->task->pi_lock, flags);
	return live;
}
EXPORT_SYMBOL_GPL(mtk_group_task_is_live);

void mtk_group_task_release(struct mtk_group_task_lease *lease)
{
	unsigned long flags;

	if (!lease)
		return;
	raw_spin_lock_irqsave(&lease->task->pi_lock, flags);
	write_lock(&related_thread_group_lock);
	if (!hlist_unhashed(&lease->node)) {
		mtk_group_restore_locked(lease);
		hash_del(&lease->node);
	}
	write_unlock(&related_thread_group_lock);
	raw_spin_unlock_irqrestore(&lease->task->pi_lock, flags);
	mtk_cpuqos_task_release(lease->pd);
	put_task_struct(lease->task);
	kfree(lease);
}
EXPORT_SYMBOL_GPL(mtk_group_task_release);
#endif /* CONFIG_MTK_PERF_CTRL_LEASE */
