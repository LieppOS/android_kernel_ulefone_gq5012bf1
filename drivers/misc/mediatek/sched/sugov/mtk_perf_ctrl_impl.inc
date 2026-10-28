/* SPDX-License-Identifier: GPL-2.0 */
/* Embedded after gear_uclamp_max/set_gear_uclamp_max in the native TU. No
 * scheduler emulation: these are the real gu_ctrl/cu_ctrl/WL/gear controls.
 */
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
static void mtk_perf_ctrl_restore_locked(struct mtk_perf_ctrl_lease *lease)
{
	unsigned int i;

	lockdep_assert_held(&mtk_perf_ctrl_lock);
	for (i = 0; i < lease->gears; i++)
		WRITE_ONCE(gear_uclamp_max[i], lease->saved_max[i]);
	WRITE_ONCE(wl_type_manual, lease->saved_wl);
	WRITE_ONCE(cu_ctrl, lease->saved_cu);
	WRITE_ONCE(gu_ctrl, lease->saved_gu);
}

static void mtk_perf_ctrl_preempt_locked(void)
{
	lockdep_assert_held(&mtk_perf_ctrl_lock);
	/* A legacy writer wins. Withdraw the complete temporary overlay BEFORE
	 * its write, atomically with respect to all other writers. Even a write
	 * of the same numeric value revokes ownership; there is no ABA compare.
	 * The client retains/frees its now-stale handle, never the native writer.
	 */
	if (mtk_perf_ctrl_owner) {
		mtk_perf_ctrl_restore_locked(mtk_perf_ctrl_owner);
		mtk_perf_ctrl_owner = NULL;
	}
}

int mtk_perf_ctrl_acquire(unsigned int gears, const int *limits, int wl,
	struct mtk_perf_ctrl_lease **out)
{
	struct mtk_perf_ctrl_lease *lease;
	unsigned long flags;
	unsigned int i;
	int ret = 0;

	if (!out)
		return -EINVAL;
	*out = NULL;
	if (!limits || !gears || gears > ARRAY_SIZE(gear_uclamp_max))
		return -EINVAL;
	for (i = 0; i < gears; i++)
		if (limits[i] < 0 || limits[i] > SCHED_CAPACITY_SCALE)
			return -ERANGE;
	/* Match the native setter's workload selection semantics. */
	if (wl < 0 || wl >= nr_wl_type || !is_wl_support())
		wl = -1;
	lease = kzalloc(sizeof(*lease), GFP_KERNEL);
	if (!lease)
		return -ENOMEM;
	lease->gears = gears;

	raw_spin_lock_irqsave(&mtk_perf_ctrl_lock, flags);
	if (gears != pd_count)
		ret = -ENODEV;
	else if (mtk_perf_ctrl_owner)
		ret = -EBUSY;
	if (!ret) {
		lease->saved_gu = gu_ctrl;
		lease->saved_cu = cu_ctrl;
		lease->saved_wl = wl_type_manual;
		for (i = 0; i < gears; i++) {
			lease->saved_max[i] = gear_uclamp_max[i];
			WRITE_ONCE(gear_uclamp_max[i], limits[i]);
		}
		WRITE_ONCE(wl_type_manual, wl);
		WRITE_ONCE(cu_ctrl, true);
		WRITE_ONCE(gu_ctrl, true);
		mtk_perf_ctrl_owner = lease;
	}
	raw_spin_unlock_irqrestore(&mtk_perf_ctrl_lock, flags);
	if (ret)
		kfree(lease);
	else
		*out = lease;
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_perf_ctrl_acquire);

int mtk_perf_ctrl_set_gear(struct mtk_perf_ctrl_lease *lease, unsigned int gear,
	int value)
{
	unsigned long flags;
	int ret = 0;

	if (!lease || gear >= lease->gears)
		return -EINVAL;
	if (value < 0 || value > SCHED_CAPACITY_SCALE)
		return -ERANGE;
	raw_spin_lock_irqsave(&mtk_perf_ctrl_lock, flags);
	if (mtk_perf_ctrl_owner != lease)
		ret = -ESTALE;
	else
		WRITE_ONCE(gear_uclamp_max[gear], value);
	raw_spin_unlock_irqrestore(&mtk_perf_ctrl_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(mtk_perf_ctrl_set_gear);

bool mtk_perf_ctrl_is_live(struct mtk_perf_ctrl_lease *lease)
{
	unsigned long flags;
	bool live;

	raw_spin_lock_irqsave(&mtk_perf_ctrl_lock, flags);
	live = lease && mtk_perf_ctrl_owner == lease;
	raw_spin_unlock_irqrestore(&mtk_perf_ctrl_lock, flags);
	return live;
}
EXPORT_SYMBOL_GPL(mtk_perf_ctrl_is_live);

void mtk_perf_ctrl_release(struct mtk_perf_ctrl_lease *lease)
{
	unsigned long flags;

	if (!lease)
		return;
	raw_spin_lock_irqsave(&mtk_perf_ctrl_lock, flags);
	if (mtk_perf_ctrl_owner == lease) {
		mtk_perf_ctrl_restore_locked(lease);
		mtk_perf_ctrl_owner = NULL;
	}
	raw_spin_unlock_irqrestore(&mtk_perf_ctrl_lock, flags);
	kfree(lease);
}
EXPORT_SYMBOL_GPL(mtk_perf_ctrl_release);
#endif
