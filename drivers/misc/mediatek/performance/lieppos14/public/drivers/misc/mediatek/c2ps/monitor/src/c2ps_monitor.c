// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 MediaTek Inc. */
#include <linux/string.h>
#include "../../common/include/c2ps_common.h"
#include "../include/c2ps_monitor.h"
#include "../../uclamp_regulator/include/c2ps_uclamp_regulator.h"

static void reset_history_info(struct c2ps_task_info *info)
{
	mutex_lock(&info->mlock);
	memset(info->hist_proc_time, 0, sizeof(info->hist_proc_time));
	memset(info->hist_loading, 0, sizeof(info->hist_loading));
	info->nr_hist_info = 0;
	info->hist_proc_time_sum = 0;
	mutex_unlock(&info->mlock);
}

int monitor_task_start(struct task_struct *task, int task_id)
{
	struct c2ps_task_info *info = c2ps_find_task_info_by_tskid(task_id);
	struct regulator_req *req;
	int ret;

	if (!info)
		return -ENOENT;
	mutex_lock(&info->mlock);
	ret = c2ps_task_begin_locked(info, task);
	if (ret)
		goto out;
	info->start_time = c2ps_get_time();
	info->sum_exec_runtime_start = c2ps_task_sched_runtime(task);
	if (info->tsk_group) {
		mutex_lock(&info->tsk_group->mlock);
		if (is_group_head(info)) {
			info->tsk_group->accumulate_time = 0;
			info->tsk_group->group_start_time = info->start_time;
		} else {
			info->tsk_group->accumulate_time = info->start_time >=
				info->tsk_group->group_start_time ? info->start_time -
				info->tsk_group->group_start_time : 0;
		}
		mutex_unlock(&info->tsk_group->mlock);
	}
	req = get_regulator_req();
	if (!req) {
		c2ps_task_release_locked(info);
		ret = -ENOMEM;
		goto out;
	}
	req->tsk_info = info;
	req->glb_info = get_glb_info();
	req->frame_seq = info->frame_seq;
	send_regulator_req(req);
out:
	mutex_unlock(&info->mlock);
	return ret;
}

int monitor_task_end(struct task_struct *task, int task_id)
{
	struct c2ps_task_info *info = c2ps_find_task_info_by_tskid(task_id);
	u64 runtime;

	if (!info)
		return -ENOENT;
	mutex_lock(&info->mlock);
	/* A recycled number is not the task admitted for this frame. */
	if (!info->is_active || info->task != task) {
		mutex_unlock(&info->mlock);
		return -ESTALE;
	}
	info->end_time = c2ps_get_time();
	info->proc_time = info->end_time >= info->start_time ?
		info->end_time - info->start_time : 0;
	runtime = c2ps_task_sched_runtime(task);
	info->real_exec_runtime = runtime >= info->sum_exec_runtime_start ?
		runtime - info->sum_exec_runtime_start : 0;
	info->overlap_task = NULL;
	/* Advancing frame_seq and restoring task state occur under the same
	 * mutex used by regulator_process. Old requests cannot reboost a closed
	 * frame, even if a new frame begins before the workqueue reaches them.
	 */
	c2ps_task_release_locked(info);
	mutex_unlock(&info->mlock);
	c2ps_update_task_info_hist(info);
	c2ps_critical_task_systrace(info);
	return 0;
}

int monitor_vsync(u64 ts)
{
	update_vsync_time(ts);
	return 0;
}

int monitor_camfps(int camfps)
{
	update_camfps(camfps);
	return 0;
}

int monitor_task_scene_change(int task_id, int scene_mode)
{
	struct c2ps_task_info *info = c2ps_find_task_info_by_tskid(task_id);

	if (!info)
		return -ENOENT;
	reset_history_info(info);
	return 0;
}
