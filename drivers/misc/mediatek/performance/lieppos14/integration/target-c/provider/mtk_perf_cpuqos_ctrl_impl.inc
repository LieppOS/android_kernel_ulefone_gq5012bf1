/* SPDX-License-Identifier: GPL-2.0 */
/* Included by the genuine native cpuqos_v3 provider, after its map/kick helpers.
 * Task references, never numeric relookup, cross this API. All operations except
 * is_live() are sleepable. Notify IPIs must run with no registry lock held and
 * must read current state, not write back an earlier caller's cached PD.
 */
#include <linux/hashtable.h>
#include <linux/sched/task.h>

static DEFINE_RAW_SPINLOCK(mtk_cpuqos_task_lock);

static bool mtk_cpuqos_task_alive(struct task_struct *task)
{
	return task && pid_alive(task) && !READ_ONCE(task->exit_state) &&
		!(READ_ONCE(task->flags) & (PF_KTHREAD | PF_EXITING));
}

static bool mtk_cpuqos_available(void)
{
	return READ_ONCE(plat_enable) && READ_ONCE(cpuqos_perf_mode) != DISABLE;
}

/* Requires the writer lock; the hardware notification deliberately comes later. */
static void mtk_cpuqos_write_locked(struct task_struct *task, int pd)
{
	struct cpuqos_task_struct *cqts =
		&((struct mtk_task *)task->android_vendor_data1)->cpuqos_task;

	lockdep_assert_held(&mtk_cpuqos_task_lock);
	if (pd >= 0) {
		WRITE_ONCE(cqts->pd, pd);
		WRITE_ONCE(cqts->rank, TASK_RANK);
	} else {
		WRITE_ONCE(cqts->rank, GROUP_RANK);
		WRITE_ONCE(cqts->pd, cpuqos_v3_map_task_pd(task));
	}
}

static void mtk_cpuqos_notify(struct task_struct *task)
{
	/* -1 means notification only: cpuqos_v3_kick_task must not overwrite PD.
	 * A newer writer may already have replaced this operation by this point.
	 * The IPI maps the current rank/PD, so delayed old notifications are safe.
	 */
	if (mtk_cpuqos_task_alive(task))
		cpuqos_v3_kick_task(task, -1);
}

#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
struct mtk_cpuqos_task_lease {
	struct hlist_node node;
	struct task_struct *task;
	unsigned int saved_pd;
	unsigned int saved_rank;
};
static DEFINE_HASHTABLE(mtk_cpuqos_task_leases, 6);

static struct mtk_cpuqos_task_lease *mtk_cpuqos_find_locked(struct task_struct *task)
{
	struct mtk_cpuqos_task_lease *lease;

	lockdep_assert_held(&mtk_cpuqos_task_lock);
	hash_for_each_possible(mtk_cpuqos_task_leases, lease, node, (unsigned long)task)
		if (lease->task == task)
			return lease;
	return NULL;
}
#endif

int cpuqos_task_pd_set(struct task_struct *task, int pd)
{
	struct cgroup_subsys_state *css;
	unsigned long flags;
	int old_pd, new_pd, css_id;
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
	struct mtk_cpuqos_task_lease *lease;
#endif

	if (pd < -1 || pd > PD9)
		return -EINVAL;
	if (!mtk_cpuqos_task_alive(task))
		return -ESRCH;
	if (!mtk_cpuqos_available())
		return -EOPNOTSUPP;
	might_sleep();
	raw_spin_lock_irqsave(&mtk_cpuqos_task_lock, flags);
	if (!mtk_cpuqos_task_alive(task)) {
		raw_spin_unlock_irqrestore(&mtk_cpuqos_task_lock, flags);
		return -ESRCH;
	}
	if (!mtk_cpuqos_available()) {
		raw_spin_unlock_irqrestore(&mtk_cpuqos_task_lock, flags);
		return -EOPNOTSUPP;
	}
	old_pd = cpuqos_v3_map_task_pd(task);
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
	/* Even a same-value ordinary write owns the result. The displaced token
	 * stays caller-owned but can no longer restore over this writer.
	 */
	lease = mtk_cpuqos_find_locked(task);
	if (lease)
		hash_del(&lease->node);
#endif
	mtk_cpuqos_write_locked(task, pd);
	new_pd = cpuqos_v3_map_task_pd(task);
	raw_spin_unlock_irqrestore(&mtk_cpuqos_task_lock, flags);

	/* Capture the numeric trace field while CSS is protected, not css itself. */
	rcu_read_lock();
	css = task_css(task, cpuqos_subsys_id);
	css_id = css ? css->id : -1;
	rcu_read_unlock();
	trace_cpuqos_set_task_pd(task->pid, css_id, pd, old_pd, new_pd,
		get_task_rank(task), READ_ONCE(cpuqos_perf_mode));
	mtk_cpuqos_notify(task);
	return 0;
}
EXPORT_SYMBOL_GPL(cpuqos_task_pd_set);

/* Preserve the legacy numeric API/error convention; resolve exactly once. */
int set_task_pd(int pid, int pd)
{
	struct task_struct *task;
	int ret;

	if (pid <= 0)
		return -1;
	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();
	if (!task)
		return -1;
	ret = cpuqos_task_pd_set(task, pd < 0 ? -1 : pd);
	put_task_struct(task);
	return ret ? -1 : 0;
}
EXPORT_SYMBOL_GPL(set_task_pd);

#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
int mtk_cpuqos_task_acquire(struct task_struct *task, int pd,
	struct mtk_cpuqos_task_lease **out)
{
	struct mtk_cpuqos_task_lease *lease;
	struct cpuqos_task_struct *cqts;
	unsigned long flags;
	int ret = 0;

	if (!out)
		return -EINVAL;
	*out = NULL;
	if (pd < -1 || pd > PD9)
		return -EINVAL;
	if (!mtk_cpuqos_task_alive(task))
		return -ESRCH;
	if (!mtk_cpuqos_available())
		return -EOPNOTSUPP;
	lease = kzalloc(sizeof(*lease), GFP_KERNEL);
	if (!lease)
		return -ENOMEM;
	get_task_struct(task);
	lease->task = task;
	cqts = &((struct mtk_task *)task->android_vendor_data1)->cpuqos_task;
	raw_spin_lock_irqsave(&mtk_cpuqos_task_lock, flags);
	if (!mtk_cpuqos_task_alive(task))
		ret = -ESRCH;
	else if (!mtk_cpuqos_available())
		ret = -EOPNOTSUPP;
	else if (mtk_cpuqos_find_locked(task))
		ret = -EBUSY;
	if (!ret) {
		lease->saved_pd = READ_ONCE(cqts->pd);
		lease->saved_rank = READ_ONCE(cqts->rank);
		mtk_cpuqos_write_locked(task, pd);
		hash_add(mtk_cpuqos_task_leases, &lease->node, (unsigned long)task);
	}
	raw_spin_unlock_irqrestore(&mtk_cpuqos_task_lock, flags);
	if (ret) {
		put_task_struct(task);
		kfree(lease);
		return ret;
	}
	*out = lease;
	mtk_cpuqos_notify(task);
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_cpuqos_task_acquire);

bool mtk_cpuqos_task_is_live(struct mtk_cpuqos_task_lease *lease)
{
	unsigned long flags;
	bool live;

	if (!lease)
		return false;
	raw_spin_lock_irqsave(&mtk_cpuqos_task_lock, flags);
	live = !hlist_unhashed(&lease->node) &&
		mtk_cpuqos_task_alive(lease->task) && mtk_cpuqos_available();
	raw_spin_unlock_irqrestore(&mtk_cpuqos_task_lock, flags);
	return live;
}
EXPORT_SYMBOL_GPL(mtk_cpuqos_task_is_live);

void mtk_cpuqos_task_release(struct mtk_cpuqos_task_lease *lease)
{
	struct cpuqos_task_struct *cqts;
	unsigned long flags;
	bool restored = false;

	if (!lease)
		return;
	cqts = &((struct mtk_task *)lease->task->android_vendor_data1)->cpuqos_task;
	raw_spin_lock_irqsave(&mtk_cpuqos_task_lock, flags);
	if (!hlist_unhashed(&lease->node)) {
		if (mtk_cpuqos_task_alive(lease->task)) {
			WRITE_ONCE(cqts->pd, lease->saved_pd);
			WRITE_ONCE(cqts->rank, lease->saved_rank);
			restored = true;
		}
		hash_del(&lease->node);
	}
	raw_spin_unlock_irqrestore(&mtk_cpuqos_task_lock, flags);
	if (restored)
		mtk_cpuqos_notify(lease->task);
	put_task_struct(lease->task);
	kfree(lease);
}
EXPORT_SYMBOL_GPL(mtk_cpuqos_task_release);
#endif /* CONFIG_MTK_PERF_CTRL_LEASE */
