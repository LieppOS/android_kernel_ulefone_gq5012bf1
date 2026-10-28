/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MTK_PERF_CTRL_H
#define _MTK_PERF_CTRL_H

#include <linux/types.h>

/* Built-in sugov owns the state; the client owns this opaque handle's lifetime.
 * acquire/release are sleepable. Updates and legacy preemption are IRQ-safe.
 * Client must join its users before release. There is deliberately no stub API.
 */
struct mtk_perf_ctrl_lease;
int mtk_perf_ctrl_acquire(unsigned int gears, const int *limits, int wl,
	struct mtk_perf_ctrl_lease **out);
int mtk_perf_ctrl_set_gear(struct mtk_perf_ctrl_lease *lease, unsigned int gear,
	int value);
bool mtk_perf_ctrl_is_live(struct mtk_perf_ctrl_lease *lease);
void mtk_perf_ctrl_release(struct mtk_perf_ctrl_lease *lease);

/* Stable task objects, never cached numeric PIDs. One frame owns the native
 * basic-VIP/hint overlay; any legacy field writer atomically preempts it.
 */
struct task_struct;
struct mtk_perf_task_lease;
int mtk_perf_task_acquire(struct task_struct *task, bool vip,
	struct mtk_perf_task_lease **out);
int mtk_perf_task_set_hint(struct mtk_perf_task_lease *lease, int hint);
bool mtk_perf_task_is_live(struct mtk_perf_task_lease *lease);
void mtk_perf_task_release(struct mtk_perf_task_lease *lease);
void mtk_perf_task_legacy_hint(struct task_struct *task, int hint);
#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
void mtk_perf_task_legacy_vip(struct task_struct *task, bool vip);
#endif

#endif
