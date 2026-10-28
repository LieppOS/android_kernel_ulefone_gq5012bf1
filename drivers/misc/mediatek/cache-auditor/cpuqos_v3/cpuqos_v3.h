/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef CPUQOS_V3_H
#define CPUQOS_V3_H
#include <linux/ioctl.h>
#include "common.h"

/* Caller retains task for the full sleepable call. Token ownership is exclusive;
 * ordinary task-PD writers atomically revoke it, including same-value writes.
 */
int cpuqos_task_pd_set(struct task_struct *task, int pd);
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
struct mtk_cpuqos_task_lease;
int mtk_cpuqos_task_acquire(struct task_struct *task, int pd,
	struct mtk_cpuqos_task_lease **out);
bool mtk_cpuqos_task_is_live(struct mtk_cpuqos_task_lease *lease);
void mtk_cpuqos_task_release(struct mtk_cpuqos_task_lease *lease);
#endif

#if IS_ENABLED(CONFIG_MTK_SLBC)
extern u32 slbc_sram_read(u32 offset);
extern void slbc_sram_write(u32 offset, u32 val);
#endif

struct _CPUQOS_V3_PACKAGE {
	__u32 mode;
	__u32 pid;
	__u32 set_task;
	__u32 group_id;
	__u32 set_group;
};

#define CPUQOS_V3_SET_CPUQOS_MODE		_IOW('g', 14, struct _CPUQOS_V3_PACKAGE)
#define CPUQOS_V3_SET_CT_TASK			_IOW('g', 15, struct _CPUQOS_V3_PACKAGE)
#define CPUQOS_V3_SET_CT_GROUP			_IOW('g', 16, struct _CPUQOS_V3_PACKAGE)

#endif

