/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_UCLAMP_LEASE_H
#define _LINUX_SCHED_UCLAMP_LEASE_H
#include <linux/types.h>

struct task_struct;
struct sched_uclamp_lease;

/* Sleepable clients retain the admitted task until acquire returns. One lease
 * per exact task; no PID retargeting. The baseline is captured on first apply,
 * not acquire. Ordinary scheduler writes preempt even for identical values.
 * A client must join ALL handle users before release. Release never fails and
 * never restores a displaced lease. No task_struct/KABI layout modification.
 */
int sched_uclamp_lease_acquire(struct task_struct *task,
	struct sched_uclamp_lease **out);
int sched_uclamp_lease_apply(struct sched_uclamp_lease *lease,
	unsigned int min_util, unsigned int max_util);
bool sched_uclamp_lease_is_live(struct sched_uclamp_lease *lease);
void sched_uclamp_lease_release(struct sched_uclamp_lease *lease);
#endif
