// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef _C2PS_MONITOR_C2PS_MONITOR_H
#define _C2PS_MONITOR_C2PS_MONITOR_H

#include "../../common/include/c2ps_common.h"

int monitor_task_start(struct task_struct *task, int task_id);
int monitor_task_end(struct task_struct *task, int task_id);
int monitor_vsync(u64 ts);
int monitor_camfps(int camfps);
int monitor_task_scene_change(int task_id, int scene_mode);

/* Native eas_plus exposes only an opaque rq_flags pointer here. */
struct rq_flags;
#include <drivers/misc/mediatek/sched/eas/eas_plus.h>

#endif