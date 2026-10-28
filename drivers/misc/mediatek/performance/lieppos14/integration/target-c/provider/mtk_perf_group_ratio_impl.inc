/* SPDX-License-Identifier: GPL-2.0 */
/* Embedded in actual grp_awr.c: active requests aggregate by maximum, while
 * zero withdraws one owner's request. With no active requests restore the
 * captured per-CPU baseline. Ordinary writers revoke ALL group overlays under
 * the same raw lock, before applying their own write (including same values).
 */
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/cpumask.h>

static DEFINE_RAW_SPINLOCK(mtk_group_ratio_lock);

static bool mtk_group_ratio_valid(int group)
{
	return READ_ONCE(grp_awr_init_finished) &&
		group >= 0 && group < GROUP_ID_RECORD_MAX;
}

#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
struct mtk_group_ratio_lease {
	struct list_head node;
	int group;
	unsigned int cap;
};
static LIST_HEAD(mtk_group_ratio_leases);
static unsigned int mtk_group_ratio_count[GROUP_ID_RECORD_MAX];
static int mtk_group_ratio_saved[GROUP_ID_RECORD_MAX][NR_CPUS];

static void mtk_group_ratio_apply_locked(int group)
{
	struct mtk_group_ratio_lease *lease;
	unsigned int cap = 0;
	int cpu;

	lockdep_assert_held(&mtk_group_ratio_lock);
	list_for_each_entry(lease, &mtk_group_ratio_leases, node)
		if (lease->group == group)
			cap = max(cap, lease->cap);
	for_each_possible_cpu(cpu)
		WRITE_ONCE(pcpu_pgrp_act_rto_cap[cpu][group], cap ?
			(int)cap : mtk_group_ratio_saved[group][cpu]);
}

static void mtk_group_ratio_preempt_locked(int group)
{
	struct mtk_group_ratio_lease *lease, *next;
	int cpu;

	lockdep_assert_held(&mtk_group_ratio_lock);
	if (!mtk_group_ratio_count[group])
		return;
	for_each_possible_cpu(cpu)
		WRITE_ONCE(pcpu_pgrp_act_rto_cap[cpu][group],
			mtk_group_ratio_saved[group][cpu]);
	list_for_each_entry_safe(lease, next, &mtk_group_ratio_leases, node)
		if (lease->group == group)
			list_del_init(&lease->node);
	mtk_group_ratio_count[group] = 0;
}
#endif

static void mtk_group_ratio_cpu_set(int cpu, int group, int cap)
{
	unsigned long flags;

	if (!mtk_group_ratio_valid(group) || cpu < 0 || cpu >= nr_cpu_ids ||
		!cpu_possible(cpu))
		return;
	raw_spin_lock_irqsave(&mtk_group_ratio_lock, flags);
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
	mtk_group_ratio_preempt_locked(group);
#endif
	WRITE_ONCE(pcpu_pgrp_act_rto_cap[cpu][group], cap);
	raw_spin_unlock_irqrestore(&mtk_group_ratio_lock, flags);
}

static void mtk_group_ratio_all_set(int group, int cap)
{
	unsigned long flags;
	int cpu;

	if (!mtk_group_ratio_valid(group))
		return;
	raw_spin_lock_irqsave(&mtk_group_ratio_lock, flags);
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
	mtk_group_ratio_preempt_locked(group);
#endif
	for_each_possible_cpu(cpu)
		WRITE_ONCE(pcpu_pgrp_act_rto_cap[cpu][group], cap);
	raw_spin_unlock_irqrestore(&mtk_group_ratio_lock, flags);
}

#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
int mtk_group_ratio_acquire(int group, struct mtk_group_ratio_lease **out)
{
	struct mtk_group_ratio_lease *lease;
	unsigned long flags;
	int cpu;

	if (!out)
		return -EINVAL;
	*out = NULL;
	if (group < 0 || group >= GROUP_ID_RECORD_MAX)
		return -EINVAL;
	if (!mtk_group_ratio_valid(group))
		return -ENODEV;
	lease = kzalloc(sizeof(*lease), GFP_KERNEL);
	if (!lease)
		return -ENOMEM;
	INIT_LIST_HEAD(&lease->node);
	lease->group = group;
	raw_spin_lock_irqsave(&mtk_group_ratio_lock, flags);
	if (!mtk_group_ratio_count[group])
		for_each_possible_cpu(cpu)
			mtk_group_ratio_saved[group][cpu] =
				READ_ONCE(pcpu_pgrp_act_rto_cap[cpu][group]);
	mtk_group_ratio_count[group]++;
	list_add_tail(&lease->node, &mtk_group_ratio_leases);
	raw_spin_unlock_irqrestore(&mtk_group_ratio_lock, flags);
	*out = lease;
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_group_ratio_acquire);

int mtk_group_ratio_set(struct mtk_group_ratio_lease *lease, unsigned int cap)
{
	unsigned long flags;
	int ret = 0;

	if (!lease)
		return -EINVAL;
	if (cap > SCHED_CAPACITY_SCALE)
		return -ERANGE;
	raw_spin_lock_irqsave(&mtk_group_ratio_lock, flags);
	if (list_empty(&lease->node))
		ret = -ESTALE;
	else {
		lease->cap = cap;
		mtk_group_ratio_apply_locked(lease->group);
	}
	raw_spin_unlock_irqrestore(&mtk_group_ratio_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_group_ratio_set);

bool mtk_group_ratio_is_live(struct mtk_group_ratio_lease *lease)
{
	unsigned long flags;
	bool live;

	if (!lease)
		return false;
	raw_spin_lock_irqsave(&mtk_group_ratio_lock, flags);
	live = !list_empty(&lease->node);
	raw_spin_unlock_irqrestore(&mtk_group_ratio_lock, flags);
	return live;
}
EXPORT_SYMBOL_GPL(mtk_group_ratio_is_live);

void mtk_group_ratio_release(struct mtk_group_ratio_lease *lease)
{
	unsigned long flags;

	if (!lease)
		return;
	raw_spin_lock_irqsave(&mtk_group_ratio_lock, flags);
	if (!list_empty(&lease->node)) {
		list_del_init(&lease->node);
		mtk_group_ratio_count[lease->group]--;
		mtk_group_ratio_apply_locked(lease->group);
	}
	raw_spin_unlock_irqrestore(&mtk_group_ratio_lock, flags);
	kfree(lease);
}
EXPORT_SYMBOL_GPL(mtk_group_ratio_release);
#endif /* CONFIG_MTK_PERF_CTRL_LEASE */
