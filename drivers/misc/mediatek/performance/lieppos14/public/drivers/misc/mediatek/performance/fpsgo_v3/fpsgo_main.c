// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/sched/cputime.h>
#include <sched/sched.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/cpufreq.h>
#include <linux/topology.h>
#include <linux/arch_topology.h>
#include <linux/cpumask.h>
#include <trace/events/power.h>
#include <linux/tracepoint.h>
#include <linux/kallsyms.h>
#include <uapi/linux/sched/types.h>
#include <trace/hooks/cpufreq.h>
#include "mtk_irq_mon.h"
#include <drivers/misc/mediatek/sched/sugov/cpufreq.h>
#if IS_ENABLED(CONFIG_LIEPPOS_PERFORMANCE_TARGET_C)
#include <drivers/misc/mediatek/sched/eas/eas_adpf.h>
#endif

#include "mt-plat/fpsgo_common.h"
#include "base/include/fpsgo_base.h"
#include "base/include/fpsgo_sysfs.h"
#include "base/include/fpsgo_usedext.h"
#include "base/include/fpsgo_cpu_policy.h"
#include "fbt/include/fbt_cpu.h"
#include "fstb/fstb.h"
#include "composer/include/fps_composer.h"
#include "fbt/include/xgf.h"
#include "mtk_drm_arr.h"
#include "../powerhal_cpu_ctrl/powerhal_cpu_ctrl.h"
#include "gbe/gbe_common.h"
#include "fbt/include/fbt_cpu_ux.h"
#include "../perf_ioctl/perf_ioctl_lifetime.h"

#define CREATE_TRACE_POINTS
#define MAX_MAGT_TARGET_FPS_NUM 10
#define MAX_MAGT_DEP_LIST_NUM 10
#define TARGET_UNLIMITED_FPS 240

enum FPSGO_NOTIFIER_PUSH_TYPE {
	FPSGO_NOTIFIER_SWITCH_FPSGO			= 0x00,
	FPSGO_NOTIFIER_QUEUE_DEQUEUE		= 0x01,
	FPSGO_NOTIFIER_CONNECT				= 0x02,
	FPSGO_NOTIFIER_DFRC_FPS				= 0x03,
	FPSGO_NOTIFIER_BQID					= 0x04,
	FPSGO_NOTIFIER_VSYNC				= 0x05,
	FPSGO_NOTIFIER_SWAP_BUFFER          = 0x06,
	FPSGO_NOTIFIER_SBE_RESCUE           = 0x07,
	FPSGO_NOTIFIER_ACQUIRE              = 0x08,
	FPSGO_NOTIFIER_BUFFER_QUOTA         = 0x09,
	FPSGO_NOTIFIER_ADPF_HINT            = 0x0a,
	FPSGO_NOTIFIER_MAGT_TARGET_FPS      = 0x0b,
	FPSGO_NOTIFIER_MAGT_DEP_LIST        = 0x0c,
	FPSGO_NOTIFIER_VSYNC_PERIOD         = 0x0d,
	FPSGO_NOTIFIER_FRAME_HINT			= 0x10,
	FPSGO_NOTIFIER_SBE_POLICY			= 0x11,

};

struct fpsgo_adpf_session {
	struct fpsgo_task_identity identity;
	int cmd;
	int sid;
	int tgid;
	int uid;
	int dep_task_arr[ADPF_MAX_THREAD];
	struct pid *dep_ids[ADPF_MAX_THREAD]; /* borrowed during synchronous callback */
	int dep_task_num;
	unsigned long long workload_tcpu_arr[ADPF_MAX_THREAD];
	unsigned long long workload_ts_arr[ADPF_MAX_THREAD];
	int workload_num;
	int used;
	unsigned long long target_time;
};

struct fpsgo_magt_target_fps {
	int pid_arr[MAX_MAGT_TARGET_FPS_NUM];
	int tid_arr[MAX_MAGT_TARGET_FPS_NUM];
	int tfps_arr[MAX_MAGT_TARGET_FPS_NUM];
	int num;
};

struct fpsgo_magt_dep_list {
	int pid;
	int dep_task_arr[MAX_MAGT_DEP_LIST_NUM];
	int dep_task_num;
};


/* TODO: use union*/
struct FPSGO_NOTIFIER_PUSH_TAG {
	enum FPSGO_NOTIFIER_PUSH_TYPE ePushType;

	int pid;
	unsigned long long cur_ts;

	int enable;

	int qudeq_cmd;
	unsigned int queue_arg;

	unsigned long long bufID;
	int connectedAPI;
	int queue_SF;
	unsigned long long identifier;
	int create;

	int dfrc_fps;
	int buffer_quota;

	int enhance;
	unsigned long long frameID;
	struct list_head queue_list;

	int consumer_pid;
	int consumer_tid;
	int producer_pid;

	char name[16];
	unsigned long mask;
	char specific_name[1000];
	int num;
	int mode;

	unsigned long long sf_buf_id;
	unsigned int frameid;
	unsigned int type;
	unsigned int status;
	unsigned long long tv_ts;

	/* Queue/dequeue events retain producer-time identity until consumed or
	 * cancelled. Never reconstruct the owner from an old queued number.
	 */
	struct fpsgo_task_identity render_identity;
	struct fpsgo_adpf_session adpf_hint;
	struct fpsgo_magt_target_fps *magt_tfps_hint;
	struct fpsgo_magt_dep_list *magt_dep_hint;
};

static struct mutex notify_lock;
static struct task_struct *kfpsgo_tsk;
static int fpsgo_adpf_slot = -1;
#if IS_ENABLED(CONFIG_LIEPPOS_PERFORMANCE_TARGET_C)
static int fpsgo_eas_slot = -1;
#endif
static int fpsgo_enable;
/* Native hooks may run synchronously with registration. Only READY callbacks
 * may touch governors. READY is terminal: restricted RVH registration cannot
 * be undone on Android 6.1, so its module and referenced state are permanent.
 */
enum fpsgo_runtime_state {
	FPSGO_UNINITIALIZED,
	FPSGO_PREPARING,
	FPSGO_READY,
	FPSGO_FAILED,
};
static enum fpsgo_runtime_state fpsgo_runtime;
static int fpsgo_force_onoff;
static int perfserv_ta;
static int sbe2fpsgo_query_is_running;

int powerhal_tid;

void (*rsu_cpufreq_notifier_fp)(int cluster_id, unsigned long freq);
void (*game_engine_cooler_set_last_sleep_duration_fp)(int cur_pid);
EXPORT_SYMBOL_GPL(game_engine_cooler_set_last_sleep_duration_fp);

/* TODO: event register & dispatch */
int fpsgo_is_enable(void)
{
	int enable;

	mutex_lock(&notify_lock);
	enable = fpsgo_enable;
	mutex_unlock(&notify_lock);

	FPSGO_LOGI("[FPSGO_CTRL] isenable %d\n", enable);
	return enable;
}

static void fpsgo_notifier_wq_cb_vsync(unsigned long long ts)
{
	FPSGO_LOGI("[FPSGO_CB] vsync: %llu\n", ts);

	if (!fpsgo_is_enable())
		return;

	// fpsgo_ctrl2fstb_vsync(ts);
	fpsgo_ctrl2fbt_vsync(ts);
}

static void fpsgo_notifier_wq_cb_vsync_period(unsigned long long period_ts)
{
	FPSGO_LOGI("[FPSGO_CB] vsync: %llu\n", period_ts);

	if (!fpsgo_is_enable())
		return;

	fpsgo_ctrl2fbt_vsync_period(period_ts);
}

static void fpsgo_notifier_wq_cb_swap_buffer(int pid)
{
	FPSGO_LOGI("[FPSGO_CB] swap_buffer: %d\n", pid);

	if (!fpsgo_is_enable())
		return;

	fpsgo_update_swap_buffer(pid);
}

static void fpsgo_notifier_wq_cb_sbe_rescue(int pid, int start, int enhance,
		unsigned long long frameID)
{
	FPSGO_LOGI("[FPSGO_CB] sbe_rescue: %d\n", pid);
	if (!fpsgo_is_enable())
		return;
	fpsgo_sbe_rescue_traverse(pid, start, enhance, frameID);
}

static void fpsgo_notifier_wq_cb_buffer_quota(unsigned long long curr_ts,
		int pid, int quota, unsigned long long id)
{
	FPSGO_LOGI("[FPSGO_CB] buffer_quota: ts %llu, pid %d,quota %d, id %llu\n",
		curr_ts, pid, quota, id);
	if (!fpsgo_is_enable())
		return;
	fpsgo_ctrl2fbt_buffer_quota(curr_ts, pid, quota, id);
}


static void fpsgo_notifier_wq_cb_dfrc_fps(int dfrc_fps)
{
	FPSGO_LOGI("[FPSGO_CB] dfrc_fps %d\n", dfrc_fps);

	fpsgo_ctrl2fstb_dfrc_fps(dfrc_fps);
	fpsgo_ctrl2fbt_dfrc_fps(dfrc_fps);
}

static void fpsgo_notifier_wq_cb_connect(int pid,
		int connectedAPI, unsigned long long id)
{
	FPSGO_LOGI(
		"[FPSGO_CB] connect: pid %d, API %d, id %llu\n",
		pid, connectedAPI, id);

	if (connectedAPI == WINDOW_DISCONNECT)
		fpsgo_ctrl2comp_disconnect_api(pid, connectedAPI, id);
	else
		fpsgo_ctrl2comp_connect_api(pid, connectedAPI, id);
}

static void fpsgo_notifier_wq_cb_bqid(int pid, unsigned long long bufID,
	int queue_SF, unsigned long long id, int create)
{
	FPSGO_LOGI(
		"[FPSGO_CB] bqid: pid %d, bufID %llu, queue_SF %d, id %llu, create %d\n",
		pid, bufID, queue_SF, id, create);

	fpsgo_ctrl2comp_bqid(pid, bufID, queue_SF, id, create);
}

static void fpsgo_notify_wq_cb_acquire(int consumer_pid, int consumer_tid,
	int producer_pid, int connectedAPI, unsigned long long buffer_id,
	unsigned long long ts)
{
	FPSGO_LOGI(
		"[FPSGO_CB] acquire: p_pid %d, c_pid:%d, c_tid:%d, api:%d, bufID:0x%llx\n",
		producer_pid, consumer_pid, consumer_tid, connectedAPI, buffer_id);

	if (!fpsgo_is_enable())
		return;

	if (!fpsgo_get_acquire_hint_is_enable())
		return;

	fpsgo_ctrl2comp_acquire(producer_pid, consumer_pid, consumer_tid,
		connectedAPI, buffer_id, ts);
}

static void fpsgo_notifier_wq_cb_qudeq(int qudeq,
		unsigned int startend, int cur_pid,
		unsigned long long curr_ts, unsigned long long id,
		unsigned long long sf_buf_id,
		const struct fpsgo_task_identity *identity)
{
	FPSGO_LOGI("[FPSGO_CB] qudeq: %d-%d, pid %d, ts %llu, id %llu\n",
		qudeq, startend, cur_pid, curr_ts, id);

	if (!fpsgo_task_identity_alive(identity))
		return;
	if (!fpsgo_is_enable())
		return;

	switch (qudeq) {
	case 1:
		if (startend) {
			FPSGO_LOGI("[FPSGO_CB] QUEUE Start: pid %d\n",
					cur_pid);
			fpsgo_ctrl2comp_enqueue_start(cur_pid,
					curr_ts, id, identity);
		} else {
			FPSGO_LOGI("[FPSGO_CB] QUEUE End: pid %d\n",
					cur_pid);
			fpsgo_ctrl2comp_enqueue_end(cur_pid, curr_ts,
					id, sf_buf_id, identity);
			if (game_engine_cooler_set_last_sleep_duration_fp)
				game_engine_cooler_set_last_sleep_duration_fp(cur_pid);
		}
		break;
	case 0:
		if (startend) {
			FPSGO_LOGI("[FPSGO_CB] DEQUEUE Start: pid %d\n",
					cur_pid);
			fpsgo_ctrl2comp_dequeue_start(cur_pid,
					curr_ts, id, identity);
		} else {
			FPSGO_LOGI("[FPSGO_CB] DEQUEUE End: pid %d\n",
					cur_pid);
			fpsgo_ctrl2comp_dequeue_end(cur_pid,
					curr_ts, id, identity);
		}
		break;
	default:
		break;
	}
}

static void fpsgo_notifier_wq_cb_enable(int enable)
{
	FPSGO_LOGI(
	"[FPSGO_CB] enable %d, fpsgo_enable %d, force_onoff %d\n",
	enable, fpsgo_enable, fpsgo_force_onoff);

	mutex_lock(&notify_lock);
	if (enable == fpsgo_enable) {
		mutex_unlock(&notify_lock);
		return;
	}

	if (fpsgo_force_onoff != FPSGO_FREE &&
			enable != fpsgo_force_onoff) {
		mutex_unlock(&notify_lock);
		return;
	}

	fpsgo_ctrl2fbt_switch_fbt(enable);
	fpsgo_ctrl2fstb_switch_fstb(enable);
	fpsgo_ctrl2xgf_switch_xgf(enable);

	fpsgo_enable = enable;

	if (!fpsgo_enable)
		fpsgo_clear();

	FPSGO_LOGI("[FPSGO_CB] fpsgo_enable %d\n",
			fpsgo_enable);
	mutex_unlock(&notify_lock);

	if (!enable)
		fpsgo_com_notify_fpsgo_is_boost(0);
}

static void fpsgo_notifier_wq_cb_hint_frame(int qudeq,
		int cur_pid, unsigned long long frameID,
		unsigned long long curr_ts, unsigned long long id,
		int dep_mode, char *dep_name, int dep_num)
{
	FPSGO_LOGI("[FPSGO_CB] uxframe: %d, pid %d, ts %llu, id %llu\n",
		qudeq, cur_pid, curr_ts, id);

	if (!fpsgo_is_enable())
		return;

	switch (qudeq) {
	case -1:
		FPSGO_LOGI("[FPSGO_CB] uxframe UX err (no queue): pid %d\n",
				cur_pid);
		fpsgo_ctrl2comp_hint_frame_err(cur_pid, frameID, curr_ts, id);
		break;
	case 0:
		FPSGO_LOGI("[FPSGO_CB] uxframe UX Start: pid %d\n",
				cur_pid);
		fpsgo_ctrl2comp_hint_frame_start(cur_pid, frameID, curr_ts, id);
		break;
	case 1:
		FPSGO_LOGI("[FPSGO_CB] uxframe UX End: pid %d\n",
				cur_pid);
		fpsgo_ctrl2comp_hint_frame_end(cur_pid, frameID, curr_ts, id);
		break;
	default:
		break;
	}

	if (dep_mode && dep_num > 0)
		fpsgo_ctrl2comp_hint_frame_dep_task(cur_pid, id,
			dep_mode, dep_name, dep_num);

	sbe2fpsgo_query_is_running = fpsgo_ctrl2base_query_sbe_spid_loading();
}

static int fpsgo_notifier_wq_cb_adpf_hint(struct fpsgo_adpf_session *session)
{
	unsigned long long local_key;

	if (!session)
		return -EINVAL;
	/* CLOSE must drain even after process exit or FPSGO disable. Ownership
	 * matching at the composer protects a replacement process/session.
	 */
	if (session->cmd != ADPF_CLOSE && (!fpsgo_is_enable() ||
		!fpsgo_task_identity_alive(&session->identity)))
		return 0; /* observer declined: disabled or obsolete, no mutation */

	/* Preserve all 32 bits of both process and session IDs. The donor's
	 * 8-bit SID packing aliases distinct PowerHAL sessions.
	 */
	local_key = ((unsigned long long)(unsigned int)session->tgid << 32) |
		(unsigned int)session->sid;
	switch (session->cmd) {
	case ADPF_CREATE_HINT_SESSION:
		return fpsgo_ctrl2comp_adpf_create_session(session->tgid, session->tgid,
			local_key, session->dep_task_arr, session->dep_task_num,
			session->dep_ids, session->target_time, &session->identity);
	case ADPF_UPDATE_TARGET_WORK_DURATION:
		return fpsgo_ctrl2comp_set_target_time(session->tgid, session->tgid,
			local_key, session->target_time, &session->identity);
	case ADPF_REPORT_ACTUAL_WORK_DURATION:
		return fpsgo_ctrl2comp_report_workload(session->tgid, session->tgid,
			local_key, session->workload_tcpu_arr,
			session->workload_ts_arr, session->workload_num, &session->identity);
	case ADPF_PAUSE:
		fpsgo_ctrl2comp_adpf_pause(session->tgid, local_key, &session->identity);
		break;
	case ADPF_RESUME:
		fpsgo_ctrl2comp_adpf_resume(session->tgid, local_key, &session->identity);
		break;
	case ADPF_CLOSE:
		fpsgo_ctrl2comp_adpf_close(session->tgid, session->tgid, local_key,
			&session->identity);
		break;
	case ADPF_SENT_HINT:
		break;
	case ADPF_SET_THREADS:
		return fpsgo_ctrl2comp_adpf_set_dep_list(session->tgid, session->tgid,
			local_key, session->dep_task_arr, session->dep_task_num,
			session->dep_ids, &session->identity);
	default:
		FPSGO_LOGE("[FPSGO_CB] %s unknown cmd:%d\n", __func__, session->cmd);
		return -EINVAL;
	}
	return 0;
}

static void fpsgo_notifier_wq_cb_magt_target_fps(struct fpsgo_magt_target_fps *iter)
{
	if (!iter || !fpsgo_is_enable())
		return;

	fpsgo_ctrl2fstb_magt_set_target_fps(iter->pid_arr, iter->tid_arr, iter->tfps_arr, iter->num);
}

static void fpsgo_notifier_wq_cb_magt_dep_list(struct fpsgo_magt_dep_list *iter)
{
	if (!iter || !fpsgo_is_enable())
		return;

	fpsgo_ctrl2xgf_magt_set_dep_list(iter->pid, iter->dep_task_arr, iter->dep_task_num, XGF_ADD_DEP);
}

static LIST_HEAD(head);
static int condition_notifier_wq;
static bool notifier_closing = true;
static unsigned int notifier_pending;
static DEFINE_MUTEX(notifier_wq_lock);
static DECLARE_WAIT_QUEUE_HEAD(notifier_wq_queue);
static void fpsgo_free_event(struct FPSGO_NOTIFIER_PUSH_TAG *event)
{
	/* The queue, not a conditionally enabled consumer, owns nested payloads. */
	if (event->ePushType == FPSGO_NOTIFIER_QUEUE_DEQUEUE)
		fpsgo_task_identity_put(&event->render_identity);
	if (event->ePushType == FPSGO_NOTIFIER_ADPF_HINT)
		fpsgo_task_identity_put(&event->adpf_hint.identity);
	if (event->ePushType == FPSGO_NOTIFIER_MAGT_TARGET_FPS)
		fpsgo_free(event->magt_tfps_hint, sizeof(*event->magt_tfps_hint));
	else if (event->ePushType == FPSGO_NOTIFIER_MAGT_DEP_LIST)
		fpsgo_free(event->magt_dep_hint, sizeof(*event->magt_dep_hint));
	fpsgo_free(event, sizeof(*event));
}

static void fpsgo_queue_work(struct FPSGO_NOTIFIER_PUSH_TAG *vpPush)
{
	if (vpPush->ePushType == FPSGO_NOTIFIER_QUEUE_DEQUEUE &&
		fpsgo_task_identity_get(vpPush->pid, &vpPush->render_identity)) {
		fpsgo_free_event(vpPush);
		return;
	}
	mutex_lock(&notifier_wq_lock);
	if (notifier_closing || notifier_pending >= 1024) {
		mutex_unlock(&notifier_wq_lock);
		fpsgo_free_event(vpPush);
		return;
	}
	list_add_tail(&vpPush->queue_list, &head);
	notifier_pending++;
	condition_notifier_wq = 1;
	mutex_unlock(&notifier_wq_lock);
	wake_up_interruptible(&notifier_wq_queue);
}

static void fpsgo_stop_worker(void)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *event, *tmp;
	LIST_HEAD(discard);

	mutex_lock(&notifier_wq_lock);
	notifier_closing = true;
	mutex_unlock(&notifier_wq_lock);
	if (kfpsgo_tsk) {
		/* Its wait predicate includes kthread_should_stop(), even on idle. */
		kthread_stop(kfpsgo_tsk);
		kfpsgo_tsk = NULL;
	}
	mutex_lock(&notifier_wq_lock);
	list_splice_init(&head, &discard);
	notifier_pending = 0;
	condition_notifier_wq = 0;
	mutex_unlock(&notifier_wq_lock);
	list_for_each_entry_safe(event, tmp, &discard, queue_list) {
		list_del(&event->queue_list);
		fpsgo_free_event(event);
	}
}

static void fpsgo_notifier_wq_cb(void)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	wait_event_interruptible(notifier_wq_queue,
		READ_ONCE(condition_notifier_wq) || kthread_should_stop());
	if (kthread_should_stop())
		return;
	mutex_lock(&notifier_wq_lock);

	if (!list_empty(&head)) {
		vpPush = list_first_entry(&head,
			struct FPSGO_NOTIFIER_PUSH_TAG, queue_list);
		list_del(&vpPush->queue_list);
		notifier_pending--;
		if (list_empty(&head))
			condition_notifier_wq = 0;
		mutex_unlock(&notifier_wq_lock);
	} else {
		condition_notifier_wq = 0;
		mutex_unlock(&notifier_wq_lock);
		return;
	}

	switch (vpPush->ePushType) {
	case FPSGO_NOTIFIER_SWITCH_FPSGO:
		fpsgo_notifier_wq_cb_enable(vpPush->enable);
		break;
	case FPSGO_NOTIFIER_QUEUE_DEQUEUE:
		fpsgo_notifier_wq_cb_qudeq(vpPush->qudeq_cmd,
				vpPush->queue_arg, vpPush->pid,
				vpPush->cur_ts, vpPush->identifier, vpPush->sf_buf_id,
				&vpPush->render_identity);
		break;
	case FPSGO_NOTIFIER_CONNECT:
		fpsgo_notifier_wq_cb_connect(vpPush->pid,
				vpPush->connectedAPI, vpPush->identifier);
		break;
	case FPSGO_NOTIFIER_DFRC_FPS:
		fpsgo_notifier_wq_cb_dfrc_fps(vpPush->dfrc_fps);
		break;
	case FPSGO_NOTIFIER_BQID:
		fpsgo_notifier_wq_cb_bqid(vpPush->pid, vpPush->bufID,
			vpPush->queue_SF, vpPush->identifier, vpPush->create);
		break;
	case FPSGO_NOTIFIER_VSYNC:
		fpsgo_notifier_wq_cb_vsync(vpPush->cur_ts);
		break;
	case FPSGO_NOTIFIER_VSYNC_PERIOD:
		fpsgo_notifier_wq_cb_vsync_period(vpPush->cur_ts);
		break;
	case FPSGO_NOTIFIER_SWAP_BUFFER:
		fpsgo_notifier_wq_cb_swap_buffer(vpPush->pid);
		break;
	case FPSGO_NOTIFIER_SBE_RESCUE:
		fpsgo_notifier_wq_cb_sbe_rescue(vpPush->pid, vpPush->enable, vpPush->enhance,
						vpPush->frameID);
		break;
	case FPSGO_NOTIFIER_ACQUIRE:
		fpsgo_notify_wq_cb_acquire(vpPush->consumer_pid,
			vpPush->consumer_tid, vpPush->producer_pid,
			vpPush->connectedAPI, vpPush->bufID, vpPush->cur_ts);
		break;
	case FPSGO_NOTIFIER_BUFFER_QUOTA:
		fpsgo_notifier_wq_cb_buffer_quota(vpPush->cur_ts,
				vpPush->pid,
				vpPush->buffer_quota,
				vpPush->identifier);
		break;
	case FPSGO_NOTIFIER_FRAME_HINT:
		fpsgo_notifier_wq_cb_hint_frame(vpPush->qudeq_cmd,
				vpPush->pid, vpPush->frameID,
				vpPush->cur_ts, vpPush->identifier,
				vpPush->mode, vpPush->specific_name, vpPush->num);
		break;
	case FPSGO_NOTIFIER_SBE_POLICY:
		fpsgo_ctrl2comp_set_sbe_policy(vpPush->pid,
				vpPush->name, vpPush->mask, vpPush->qudeq_cmd,
				vpPush->specific_name, vpPush->num);
		break;
	case FPSGO_NOTIFIER_ADPF_HINT:
		fpsgo_notifier_wq_cb_adpf_hint(&vpPush->adpf_hint);
		break;
	case FPSGO_NOTIFIER_MAGT_TARGET_FPS:
		fpsgo_notifier_wq_cb_magt_target_fps(vpPush->magt_tfps_hint);
		break;
	case FPSGO_NOTIFIER_MAGT_DEP_LIST:
		fpsgo_notifier_wq_cb_magt_dep_list(vpPush->magt_dep_hint);
		break;
	default:
		FPSGO_LOGE("[FPSGO_CTRL] unhandled push type = %d\n",
				vpPush->ePushType);
		break;
	}
	fpsgo_free_event(vpPush);

}

struct task_struct *fpsgo_get_kfpsgo(void)
{
	return kfpsgo_tsk ? kfpsgo_tsk : NULL;
}

static int kfpsgo(void *arg)
{
	struct sched_attr attr = {};

	attr.sched_policy = -1;
	attr.sched_flags =
		SCHED_FLAG_KEEP_ALL |
		SCHED_FLAG_UTIL_CLAMP |
		SCHED_FLAG_RESET_ON_FORK;
	attr.sched_util_min = 1;
	attr.sched_util_max = 1024;
	if (sched_setattr_nocheck(current, &attr) != 0)
		FPSGO_LOGE("[FPSGO_CTRL] %s set uclamp fail\n", __func__);

	set_user_nice(current, -20);

	while (!kthread_should_stop())
		fpsgo_notifier_wq_cb();

	return 0;
}
int fpsgo_notify_qudeq(int qudeq,
		unsigned int startend,
		int pid, unsigned long long id, unsigned long long sf_buf_id)
{
	unsigned long long cur_ts;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] qudeq %d-%d, id %llu pid %d\n",
		qudeq, startend, id, pid);

	if (!fpsgo_is_enable())
		return 0;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return 0;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return 0;
	}

	cur_ts = fpsgo_get_time();

	vpPush->ePushType = FPSGO_NOTIFIER_QUEUE_DEQUEUE;
	vpPush->pid = pid;
	vpPush->cur_ts = cur_ts;
	vpPush->qudeq_cmd = qudeq;
	vpPush->queue_arg = startend;
	vpPush->identifier = id;
	vpPush->sf_buf_id = sf_buf_id;

	fpsgo_queue_work(vpPush);

	return FPSGO_VERSION_CODE;
}
void fpsgo_notify_connect(int pid,
		int connectedAPI, unsigned long long id)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI(
		"[FPSGO_CTRL] connect pid %d, id %llu, API %d\n",
		pid, id, connectedAPI);

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_CONNECT;
	vpPush->pid = pid;
	vpPush->connectedAPI = connectedAPI;
	vpPush->identifier = id;

	fpsgo_queue_work(vpPush);
}

void fpsgo_notify_bqid(int pid, unsigned long long bufID,
	int queue_SF, unsigned long long id, int create)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] bqid pid %d, buf %llu, queue_SF %d, id %llu\n",
		pid, bufID, queue_SF, id);

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_BQID;
	vpPush->pid = pid;
	vpPush->bufID = bufID;
	vpPush->queue_SF = queue_SF;
	vpPush->identifier = id;
	vpPush->create = create;

	fpsgo_queue_work(vpPush);
}

void fpsgo_notify_acquire(int consumer_pid, int producer_pid,
	int connectedAPI, unsigned long long buffer_id)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_ACQUIRE;
	vpPush->consumer_pid = consumer_pid;
	vpPush->consumer_tid = current->pid;
	vpPush->producer_pid = producer_pid;
	vpPush->connectedAPI = connectedAPI;
	vpPush->bufID = buffer_id;
	vpPush->cur_ts = fpsgo_get_time();

	fpsgo_queue_work(vpPush);
}

int fpsgo_perfserv_ta_value(void)
{
	int value;

	mutex_lock(&notify_lock);
	value = perfserv_ta;
	mutex_unlock(&notify_lock);

	return value;
}

void fpsgo_set_perfserv_ta(int value)
{
	mutex_lock(&notify_lock);
	perfserv_ta = value;
	mutex_unlock(&notify_lock);
	fpsgo_ctrl2fbt_switch_uclamp(!value);
}


void fpsgo_notify_vsync(void)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] vsync\n");

	if (!fpsgo_is_enable())
		return;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_VSYNC;
	vpPush->cur_ts = fpsgo_get_time();

	fpsgo_queue_work(vpPush);
}

void fpsgo_notify_vsync_period(unsigned long long period)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] vsync period\n");

	if (!fpsgo_is_enable())
		return;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_VSYNC_PERIOD;
	vpPush->cur_ts = period;

	fpsgo_queue_work(vpPush);
}


void fpsgo_notify_swap_buffer(int pid)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] swap_buffer\n");

	if (!fpsgo_is_enable())
		return;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_SWAP_BUFFER;
	vpPush->pid = pid;

	fpsgo_queue_work(vpPush);
}

void fpsgo_notify_sbe_rescue(int pid, int start, int enhance, unsigned long long frameID)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] sbe_rescue\n");

	if (!fpsgo_is_enable())
		return;

	vpPush = (struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_SBE_RESCUE;
	vpPush->pid = pid;
	vpPush->enable = start;
	vpPush->enhance = enhance;
	vpPush->frameID = frameID;

	fpsgo_queue_work(vpPush);
}

void fpsgo_get_fps(int *pid, int *fps)
{
	//int pid = -1, fps = -1;
	if (unlikely(powerhal_tid == 0))
		powerhal_tid = current->pid;

	fpsgo_ctrl2fstb_get_fps(pid, fps);

	FPSGO_LOGI("[FPSGO_CTRL] get_fps %d %d\n", *pid, *fps);

	//return fps;
}

void fpsgo_get_cmd(int *cmd, int *value1, int *value2)
{
	int _cmd = -1, _value1 = -1, _value2 = -1;

	fpsgo_ctrl2base_get_pwr_cmd(&_cmd, &_value1, &_value2);


	FPSGO_LOGI("[FPSGO_CTRL] get_cmd %d %d %d\n", _cmd, _value1, _value2);
	*cmd = _cmd;
	*value1 = _value1;
	*value2 = _value2;

}

int fpsgo_get_fstb_active(long long time_diff)
{
	return is_fstb_active(time_diff);
}

int fpsgo_wait_fstb_active(void)
{
	return fpsgo_ctrl2fstb_wait_fstb_active();
}

void fpsgo_get_pid(int cmd, int *pid, int op, int value1, int value2)
{
	int local_tgid = 0;
	int local_rtid = 0;
	int local_blc = 0;
	unsigned long long cur_ts;

	if (!pid)
		return;

	switch (cmd) {
	case CAMERA_CLOSE:
		fpsgo_ctrl2base_notify_cam_close();
		break;
	case CAMERA_APK:
	case CAMERA_SERVER:
	case CAMERA_HWUI:
		op ? fpsgo_ctrl2base_wait_cam(cmd, pid) :
			fpsgo_ctrl2base_get_cam_pid(cmd, pid);
		break;
	case CAMERA_DO_FRAME:
		cur_ts = fpsgo_get_time();
		fpsgo_ctrl2fstb_cam_queue_time_update(cur_ts);
		break;
	case CAMERA_APP_MIN_FPS:
		cur_ts = fpsgo_get_time();
		fpsgo_ctrl2comp_set_app_meta_fps(value1, value2, cur_ts);
		break;
	case CAMERA_PERF_IDX:
		local_tgid = value1;
		fpsgo_ctrl2base_get_cam_perf(local_tgid, &local_rtid, &local_blc);
		*pid = local_blc;
		break;
	default:
		FPSGO_LOGE("[FPSGO_CTRL] wrong cmd:%d\n", cmd);
		break;
	}
}

void fpsgo_notify_buffer_quota(int pid, int quota, unsigned long long identifier)
{
	unsigned long long cur_ts;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] buffer_quota %d, pid %d, id %llu\n",
		quota, pid, identifier);

	if (!fpsgo_is_enable())
		return;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	cur_ts = fpsgo_get_time();

	vpPush->ePushType = FPSGO_NOTIFIER_BUFFER_QUOTA;
	vpPush->cur_ts = cur_ts;
	vpPush->pid = pid;
	vpPush->buffer_quota = quota;
	vpPush->identifier = identifier;

	fpsgo_queue_work(vpPush);
}

int fpsgo_notify_frame_hint(int qudeq,
	int pid, int frameID, unsigned long long id,
	int dep_mode, char *dep_name, int dep_num)
{
	int ret;
	unsigned long long cur_ts;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	FPSGO_LOGI("[FPSGO_CTRL] ux_qudeq %d, id %llu pid %d\n",
		qudeq, id, pid);

	if (!fpsgo_is_enable())
		return -EINVAL;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return -ENOMEM;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return -ENOMEM;
	}

	cur_ts = fpsgo_get_time();

	vpPush->ePushType = FPSGO_NOTIFIER_FRAME_HINT;
	vpPush->pid = pid;
	vpPush->cur_ts = cur_ts;
	vpPush->qudeq_cmd = qudeq;
	vpPush->frameID = frameID;
	// FPSGO UX: bufid magic number.
	vpPush->identifier = 5566;
	vpPush->mode = dep_mode;
	if (dep_mode && dep_num > 0) {
		memcpy(vpPush->specific_name, dep_name, 1000);
		vpPush->num = dep_num;
	} else
		vpPush->num = 0;

	fpsgo_queue_work(vpPush);

	ret = fpsgo_ctrl2ux_get_perf();

	return ret;
}

int fpsgo_notify_sbe_policy(int pid, char *name, unsigned long mask,
	int start, char *specific_name, int num)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	if (!fpsgo_is_enable())
		return -EINVAL;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return -ENOMEM;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return -ENOMEM;
	}

	if (test_bit(FPSGO_RUNNING_QUERY, &mask)) {
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return sbe2fpsgo_query_is_running ? 10001 : 0;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_SBE_POLICY;
	vpPush->pid = pid;
	vpPush->mask = mask;
	vpPush->qudeq_cmd = start;
	memcpy(vpPush->name, name, 16);
	memcpy(vpPush->specific_name, specific_name, 1000);
	vpPush->num = num;
	fpsgo_queue_work(vpPush);

	return 0;
}

#if IS_ENABLED(CONFIG_LIEPPOS_PERFORMANCE_TARGET_C)
static int fpsgo_notify_native_adpf(struct _SESSION *session)
{
	if (!session)
		return -EFAULT;
	if (session->cmd != ADPF_CLOSE &&
		smp_load_acquire(&fpsgo_runtime) != FPSGO_READY)
		return -ESHUTDOWN;
	return sched_adpf_callback(session);
}
#endif

int fpsgo_notify_adpf_hint(struct _SESSION *session)
{
	struct fpsgo_adpf_session hint = {0};
	int i;

	if (!session)
		return -EFAULT;
	if (session->cmd != ADPF_CLOSE &&
		smp_load_acquire(&fpsgo_runtime) != FPSGO_READY)
		return -ESHUTDOWN;
	if (!session->owner || !session->target || pid_nr(session->target) != session->tgid)
		return -EINVAL;
	if (session->threadIds_size < 0 || session->threadIds_size > ADPF_MAX_THREAD ||
		session->work_duration_size < 0 || session->work_duration_size > ADPF_MAX_THREAD)
		return -EINVAL;
	for (i = 0; i < session->work_duration_size; i++) {
		if (!session->workDuration[i])
			return -EFAULT;
	}

	/* PowerHAL already delivers committed snapshots on an ordered sleepable
	 * queue under its callback SRCU lease. A second bounded/allocation-prone
	 * queue silently lost CLOSE under pressure. Consume synchronously and
	 * borrow the producer's actual identity, never a fresh numeric lookup.
	 */
	hint.identity.tid = session->target;
	hint.identity.tgid = session->target;
	hint.identity.process = true;
	hint.cmd = session->cmd;
	hint.sid = session->sid;
	hint.tgid = session->tgid;
	hint.uid = session->uid;
	hint.dep_task_num = session->threadIds_size;
	for (i = 0; i < hint.dep_task_num; i++) {
		hint.dep_task_arr[i] = session->threadIds[i];
		hint.dep_ids[i] = session->thread_owners[i];
	}
	hint.workload_num = session->work_duration_size;
	for (i = 0; i < hint.workload_num; i++) {
		hint.workload_tcpu_arr[i] = session->workDuration[i]->durationNanos;
		hint.workload_ts_arr[i] = session->workDuration[i]->timeStampNanos;
	}
	hint.used = session->used;
	hint.target_time = session->durationNanos;
	return fpsgo_notifier_wq_cb_adpf_hint(&hint);
}

int fpsgo_notify_magt_target_fps(int *pid_arr, int *tid_arr,
	int *tfps_arr, int num)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush = NULL;

	if (!pid_arr || !tid_arr || !tfps_arr || num < 0 ||
		num > MAX_MAGT_TARGET_FPS_NUM)
		return -EINVAL;

	if (!kfpsgo_tsk)
		return -ENOMEM;

	vpPush = fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush)
		return -ENOMEM;

	vpPush->magt_tfps_hint = NULL;
	vpPush->magt_tfps_hint = fpsgo_alloc_atomic(sizeof(struct fpsgo_magt_target_fps));
	if (!vpPush->magt_tfps_hint) {
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return -ENOMEM;
	}

	memset(vpPush->magt_tfps_hint->pid_arr, 0, MAX_MAGT_TARGET_FPS_NUM * sizeof(int));
	memcpy(vpPush->magt_tfps_hint->pid_arr, pid_arr, num * sizeof(int));
	memset(vpPush->magt_tfps_hint->tid_arr, 0, MAX_MAGT_TARGET_FPS_NUM * sizeof(int));
	memcpy(vpPush->magt_tfps_hint->tid_arr, tid_arr, num * sizeof(int));
	memset(vpPush->magt_tfps_hint->tfps_arr, 0, MAX_MAGT_TARGET_FPS_NUM * sizeof(int));
	memcpy(vpPush->magt_tfps_hint->tfps_arr, tfps_arr, num * sizeof(int));
	vpPush->magt_tfps_hint->num = num;
	vpPush->ePushType = FPSGO_NOTIFIER_MAGT_TARGET_FPS;

	fpsgo_queue_work(vpPush);

	return 0;
}

int fpsgo_notify_magt_dep_list(int pid, int *dep_task_arr, int dep_task_num)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush = NULL;

	if (dep_task_num < 0 || dep_task_num > MAX_MAGT_DEP_LIST_NUM ||
		(!dep_task_arr && dep_task_num != 0))
		return -EINVAL;

	if (!kfpsgo_tsk)
		return -ENOMEM;

	vpPush = fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
	if (!vpPush)
		return -ENOMEM;

	vpPush->magt_dep_hint = NULL;
	vpPush->magt_dep_hint = fpsgo_alloc_atomic(sizeof(struct fpsgo_magt_dep_list));
	if (!vpPush->magt_dep_hint) {
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return -ENOMEM;
	}

	vpPush->magt_dep_hint->pid = pid;
	memset(vpPush->magt_dep_hint->dep_task_arr, 0,
		MAX_MAGT_DEP_LIST_NUM * sizeof(int));
	if (dep_task_arr)
		memcpy(vpPush->magt_dep_hint->dep_task_arr, dep_task_arr,
			dep_task_num * sizeof(int));
	vpPush->magt_dep_hint->dep_task_num = dep_task_num;
	vpPush->ePushType = FPSGO_NOTIFIER_MAGT_DEP_LIST;

	fpsgo_queue_work(vpPush);

	return 0;
}

void dfrc_fps_limit_cb(unsigned int fps_limit)
{
	unsigned int vTmp = TARGET_UNLIMITED_FPS;
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush;

	if (fps_limit > 0 && fps_limit <= TARGET_UNLIMITED_FPS)
		vTmp = fps_limit;

	FPSGO_LOGI("[FPSGO_CTRL] dfrc_fps %d\n", vTmp);

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		fpsgo_free(vpPush, sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_DFRC_FPS;
	vpPush->dfrc_fps = vTmp;

	fpsgo_queue_work(vpPush);
}

/* FPSGO control */
void fpsgo_switch_enable(int enable)
{
	struct FPSGO_NOTIFIER_PUSH_TAG *vpPush = NULL;

	if (!kfpsgo_tsk) {
		FPSGO_LOGE("[FPSGO_CTRL] NULL WorkQueue\n");
		return;
	}

	if (fpsgo_arch_nr_clusters() <= 0) {
		FPSGO_LOGE("[%s] DON'T ENABLE FPSGO: nr_cluster <= 0", __func__);
		return;
	}

	FPSGO_LOGI("[FPSGO_CTRL] switch enable %d\n", enable);

	if (fpsgo_is_force_enable() !=
			FPSGO_FREE && enable !=
			fpsgo_is_force_enable())
		return;

	vpPush =
		(struct FPSGO_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FPSGO_NOTIFIER_PUSH_TAG));

	if (!vpPush) {
		FPSGO_LOGE("[FPSGO_CTRL] OOM\n");
		return;
	}

	vpPush->ePushType = FPSGO_NOTIFIER_SWITCH_FPSGO;
	vpPush->enable = enable;

	fpsgo_queue_work(vpPush);
}

int fpsgo_is_force_enable(void)
{
	int temp_onoff;

	mutex_lock(&notify_lock);
	temp_onoff = fpsgo_force_onoff;
	mutex_unlock(&notify_lock);

	return temp_onoff;
}

void fpsgo_force_switch_enable(int enable)
{
	mutex_lock(&notify_lock);
	fpsgo_force_onoff = enable;
	mutex_unlock(&notify_lock);

	fpsgo_switch_enable(enable?1:0);
}

/* FSTB control */
int fpsgo_is_fstb_enable(void)
{
	return is_fstb_enable();
}

int fpsgo_switch_fstb(int enable)
{
	return fpsgo_ctrl2fstb_switch_fstb(enable);
}

int fpsgo_fstb_process_fps_range(char *proc_name,
	int nr_level, struct fps_level *level)
{
	return switch_process_fps_range(proc_name, nr_level, level);
}

int fpsgo_fstb_thread_fps_range(pid_t pid,
	int nr_level, struct fps_level *level)
{
	return switch_thread_fps_range(pid, nr_level, level);
}

#if FPSGO_DYNAMIC_WL

void fpsgo_notify_cpufreq_cap(int cid, int cap)
{
	FPSGO_LOGI("[FPSGO_CTRL] cid %d, cpufreq %d\n", cid, cap);

	if (!fpsgo_enable)
		return;

	fpsgo_ctrl2fbt_cpufreq_cb_cap(cid, cap);
}

static void fpsgo_cpu_frequency_cap_tracer(void *ignore, struct cpufreq_policy *policy)
{
	unsigned int cpu, capacity;
	int cluster;

	if (smp_load_acquire(&fpsgo_runtime) != FPSGO_READY || !policy)
		return;

	irq_log_store();

	cpu = cpumask_first(policy->related_cpus);
	cluster = fpsgo_get_cpu_policy_id(cpu);
	if (cluster < 0 || !fpsgo_cpu_policy_matches(cluster, policy->related_cpus))
		return;

	irq_log_store();

	capacity = get_curr_cap(cpu);

	irq_log_store();

	if (capacity)
		fpsgo_notify_cpufreq_cap(cluster, capacity);
	else
		FPSGO_LOGE("[%s] cluster:%d, cpu:%d, freq:%d\n", __func__,
			cluster, cpu, capacity);

	irq_log_store();
}

int register_fpsgo_android_cpufreq_transition_hook(void)
{
	int ret;

	ret = register_trace_android_rvh_cpufreq_transition(fpsgo_cpu_frequency_cap_tracer, NULL);
	/* The pinned Target C rvh_func_add() returns positive ENOMEM when
	 * rvh_zalloc_funcs() fails. do_init_module() treats a positive init
	 * result as success, even after our rollback. Normalize before the
	 * transaction sees it; no callback is installed on this error path.
	 */
	if (ret > 0)
		ret = -ret;
	if (ret)
		pr_info("register android_rvh_cpufreq_transition hooks failed, returned %d\n", ret);
	return ret;
}

#else  // FPSGO_DYNAMIC_WL

struct tracepoints_table {
	const char *name;
	void *func;
	struct tracepoint *tp;
	bool registered;
};

void fpsgo_notify_cpufreq(int cid, unsigned long freq)
{
	FPSGO_LOGI("[FPSGO_CTRL] cid %d, cpufreq %lu\n", cid, freq);

	if (rsu_cpufreq_notifier_fp)
		rsu_cpufreq_notifier_fp(cid, freq);

	if (!fpsgo_enable)
		return;

	fpsgo_ctrl2fbt_cpufreq_cb_exp(cid, freq);
}

static void fpsgo_cpu_frequency_tracer(void *ignore, unsigned int frequency, unsigned int cpu_id)
{
	int cluster;

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	u64 ts[2];

	ts[0] = sched_clock();
#endif

	if (smp_load_acquire(&fpsgo_runtime) != FPSGO_READY)
		return;

	/* The immutable domain map is safe in tracepoint/atomic context. Do not
	 * reacquire policy refs or infer domain IDs by skipping CPU numbers.
	 */
	cluster = fpsgo_get_cpu_policy_id(cpu_id);
	if (cluster < 0 || cpu_id != fpsgo_get_cpu_policy_cpu(cluster))
		return;
	fpsgo_notify_cpufreq(cluster, frequency);

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	ts[1] = sched_clock();

	if ((ts[1] - ts[0] > 100000ULL) && in_hardirq()) {
		printk_deferred("%s duration %llu, ts[0]=%llu, ts[1]=%llu\n",
			__func__, ts[1] - ts[0], ts[0], ts[1]);
	}
#endif

}

struct tracepoints_table fpsgo_tracepoints[] = {
	{.name = "cpu_frequency", .func = fpsgo_cpu_frequency_tracer},
};

#define FOR_EACH_INTEREST(i) \
	for (i = 0; i < sizeof(fpsgo_tracepoints) / sizeof(struct tracepoints_table); i++)

static void lookup_tracepoints(struct tracepoint *tp, void *ignore)
{
	int i;

	FOR_EACH_INTEREST(i) {
		if (strcmp(fpsgo_tracepoints[i].name, tp->name) == 0)
			fpsgo_tracepoints[i].tp = tp;
	}
}

void tracepoint_cleanup(void)
{
	int i;

	FOR_EACH_INTEREST(i) {
		if (fpsgo_tracepoints[i].registered) {
			tracepoint_probe_unregister(
				fpsgo_tracepoints[i].tp,
				fpsgo_tracepoints[i].func, NULL);
			fpsgo_tracepoints[i].registered = false;
		}
	}
}

int register_fpsgo_cpufreq_transition_hook(void)
{
	int i, ret = 0;

	for_each_kernel_tracepoint(lookup_tracepoints, NULL);

	FOR_EACH_INTEREST(i) {
		if (fpsgo_tracepoints[i].tp == NULL) {
			FPSGO_LOGE("FPSGO Error, %s not found\n", fpsgo_tracepoints[i].name);
			tracepoint_cleanup();
			return -ENODEV;
		}
	}
	ret = tracepoint_probe_register(fpsgo_tracepoints[0].tp, fpsgo_tracepoints[0].func,  NULL);
	if (ret) {
		FPSGO_LOGE("cpu_frequency: Couldn't activate tracepoint\n");
		return ret;
	}
	fpsgo_tracepoints[0].registered = true;
	return 0;
}

#endif  // FPSGO_DYNAMIC_WL


static void fpsgo_close_dispatch_readers(void)
{
	fpsgo_base_close_readers();
	fpsgo_fstb_close_readers();
	gbe_close_readers();
}

static void fpsgo_unpublish_ioctl_slots(void)
{
	/* Only after dispatcher SRCU drain, including PowerHAL boost-control
	 * writes. Native vendor hooks still require their own rundown before
	 * a framework exit handler may be enabled.
	 */
	fpsgo_composer_close_callbacks();
	WRITE_ONCE(register_get_fpsgo_is_boosting_fp, NULL);
	WRITE_ONCE(unregister_get_fpsgo_is_boosting_fp, NULL);
	WRITE_ONCE(fpsgo_notify_vsync_fp, NULL);
	WRITE_ONCE(fpsgo_notify_vsync_period_fp, NULL);
	WRITE_ONCE(fpsgo_notify_qudeq_fp, NULL);
	WRITE_ONCE(fpsgo_notify_connect_fp, NULL);
	WRITE_ONCE(fpsgo_notify_bqid_fp, NULL);
	WRITE_ONCE(fpsgo_notify_acquire_fp, NULL);
	WRITE_ONCE(fpsgo_notify_swap_buffer_fp, NULL);
	WRITE_ONCE(fpsgo_notify_sbe_rescue_fp, NULL);
	WRITE_ONCE(fpsgo_notify_frame_hint_fp, NULL);
	WRITE_ONCE(fpsgo_notify_sbe_policy_fp, NULL);
	WRITE_ONCE(fpsgo_get_fps_fp, NULL);
	WRITE_ONCE(fpsgo_get_cmd_fp, NULL);
	WRITE_ONCE(fpsgo_get_fstb_active_fp, NULL);
	WRITE_ONCE(fpsgo_wait_fstb_active_fp, NULL);
	WRITE_ONCE(fpsgo_notify_buffer_quota_fp, NULL);
	WRITE_ONCE(fpsgo_get_pid_fp, NULL);
	WRITE_ONCE(magt2fpsgo_notify_target_fps_fp, NULL);
	WRITE_ONCE(magt2fpsgo_notify_dep_list_fp, NULL);
	WRITE_ONCE(fpsgo_get_lr_pair_fp, NULL);
	WRITE_ONCE(fpsgo_set_rl_l2q_enable_fp, NULL);
	WRITE_ONCE(fpsgo_set_rl_expected_l2q_us_fp, NULL);
	WRITE_ONCE(xgff_frame_min_cap_fp, NULL);
	WRITE_ONCE(xgff_frame_startend_fp, NULL);
	WRITE_ONCE(xgff_frame_getdeplist_maxsize_fp, NULL);
	WRITE_ONCE(gbe_get_cmd_fp, NULL);
}

static int __init fpsgo_init(void)
{
	int ret;
	struct FPSGO_NOTIFIER_PUSH_TAG *initial_enable = NULL;
	bool common = false, fstb = false, composer = false;
	bool fbt = false, gbe = false, dispatch = false;
#if !FPSGO_DYNAMIC_WL
	bool hook = false;
#endif
#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
	bool drm = false;
#endif

	FPSGO_LOGI("[FPSGO_CTRL] init\n");
	smp_store_release(&fpsgo_runtime, FPSGO_PREPARING);
	ret = fpsgo_cpu_policy_init();
	if (ret)
		goto fail_state;
	ret = fpsgo_sysfs_init();
	if (ret)
		goto fail_policy;

	mutex_init(&notify_lock);
	kfpsgo_tsk = kthread_create(kfpsgo, NULL, "kfps");
	if (IS_ERR(kfpsgo_tsk)) {
		ret = PTR_ERR(kfpsgo_tsk);
		kfpsgo_tsk = NULL;
		goto fail_sysfs;
	}

	fpsgo_force_onoff = FPSGO_FREE;
	/* Each constructor owns its own failed stage. A completed child is
	 * unwound once, in reverse order, before any public frame dispatch.
	 */
#define FPSGO_INIT_CHILD(owned, expression) do { \
	ret = (expression); \
	if (ret) \
		goto rollback; \
	(owned) = true; \
	ret = fpsgo_sysfs_error(); \
	if (ret) \
		goto rollback; \
} while (0)
	FPSGO_INIT_CHILD(common, init_fpsgo_common());
	FPSGO_INIT_CHILD(fstb, mtk_fstb_init());
	FPSGO_INIT_CHILD(composer, fpsgo_composer_init());
	FPSGO_INIT_CHILD(fbt, fbt_cpu_init());
	FPSGO_INIT_CHILD(gbe, init_gbe_common());
#undef FPSGO_INIT_CHILD

	/* Do not allocate the initial enable event after a permanent hook has
	 * committed. The worker takes ownership only at the publication below.
	 */
	initial_enable = kzalloc(sizeof(*initial_enable), GFP_KERNEL);
	if (!initial_enable) {
		ret = -ENOMEM;
		goto rollback;
	}
	initial_enable->ePushType = FPSGO_NOTIFIER_SWITCH_FPSGO;
	initial_enable->enable = 1;

#if !FPSGO_DYNAMIC_WL
	/* This ordinary tracepoint has a real unregister operation. */
	ret = register_fpsgo_cpufreq_transition_hook();
	if (ret)
		goto rollback;
	hook = true;
#endif
#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
	ret = drm_register_fps_chg_callback(dfrc_fps_limit_cb);
	if (ret)
		goto rollback;
	drm = true;
#endif
	fpsgo_adpf_slot = adpf_register_callback(fpsgo_notify_adpf_hint);
	if (fpsgo_adpf_slot < 0) {
		ret = fpsgo_adpf_slot;
		goto rollback;
	}
	ret = perf_ioctl_dispatch_bind(fpsgo_close_dispatch_readers,
		fpsgo_unpublish_ioctl_slots);
	if (ret)
		goto rollback;
	dispatch = true;
	/* Validate/seal attributes WITHOUT admitting sysfs stores. Otherwise a
	 * store could create render state while restricted-hook rollback fails.
	 */
	ret = fpsgo_sysfs_prepare_commit();
	if (ret)
		goto rollback;

#if IS_ENABLED(CONFIG_LIEPPOS_PERFORMANCE_TARGET_C)
	/* Native scheduler is a strong, already LIVE dependency here. Its new
	 * EAS backend must not publish callbacks from native module COMING.
	 * This slot has supported SRCU unregister/join and complete reset.
	 */
	fpsgo_eas_slot = adpf_register_callback(fpsgo_notify_native_adpf);
	if (fpsgo_eas_slot < 0) {
		ret = fpsgo_eas_slot;
		goto rollback;
	}
#endif
#if FPSGO_DYNAMIC_WL
	/* FINAL fallible operation. Android RVHs cannot be unregistered. Pin
	 * our code before registering it, drop only on registration failure.
	 * A successful RVH holds this reference for the rest of the boot.
	 */
	__module_get(THIS_MODULE);
	ret = register_fpsgo_android_cpufreq_transition_hook();
	if (ret) {
		module_put(THIS_MODULE);
		goto rollback;
	}
#endif
	/* No allocation, registration, error branch or reversible exit may be
	 * added below this boundary. All referenced storage survives until boot
	 * ends; forced module unload is not a supported lifecycle.
	 */
	smp_store_release(&fpsgo_runtime, FPSGO_READY);
	fbt_cpu_publish();
	gbe_publish_callbacks();

	fpsgo_notify_vsync_fp = fpsgo_notify_vsync;
	fpsgo_notify_vsync_period_fp = fpsgo_notify_vsync_period;

	fpsgo_notify_qudeq_fp = fpsgo_notify_qudeq;
	fpsgo_notify_connect_fp = fpsgo_notify_connect;
	fpsgo_notify_bqid_fp = fpsgo_notify_bqid;
	fpsgo_notify_acquire_fp = fpsgo_notify_acquire;

	fpsgo_notify_swap_buffer_fp = fpsgo_notify_swap_buffer;
	fpsgo_notify_sbe_rescue_fp = fpsgo_notify_sbe_rescue;
	fpsgo_notify_frame_hint_fp = fpsgo_notify_frame_hint;
	fpsgo_notify_sbe_policy_fp = fpsgo_notify_sbe_policy;

	fpsgo_get_fps_fp = fpsgo_get_fps;
	fpsgo_get_cmd_fp = fpsgo_get_cmd;
	fpsgo_get_fstb_active_fp = fpsgo_get_fstb_active;
	fpsgo_wait_fstb_active_fp = fpsgo_wait_fstb_active;
	fpsgo_notify_buffer_quota_fp = fpsgo_notify_buffer_quota;
	fpsgo_get_pid_fp = fpsgo_get_pid;
	register_get_fpsgo_is_boosting_fp = register_get_fpsgo_is_boosting;
	unregister_get_fpsgo_is_boosting_fp = unregister_get_fpsgo_is_boosting;
	magt2fpsgo_notify_target_fps_fp = fpsgo_notify_magt_target_fps;
	magt2fpsgo_notify_dep_list_fp = fpsgo_notify_magt_dep_list;
	fpsgo_get_lr_pair_fp = fpsgo_get_lr_pair;
	fpsgo_set_rl_l2q_enable_fp = fpsgo_set_rl_l2q_enable;
	fpsgo_set_rl_expected_l2q_us_fp = fpsgo_set_expected_l2q_us;

	mutex_lock(&notifier_wq_lock);
	list_add_tail(&initial_enable->queue_list, &head);
	notifier_pending++;
	condition_notifier_wq = 1;
	initial_enable = NULL;
	notifier_closing = false;
	mutex_unlock(&notifier_wq_lock);
	wake_up_process(kfpsgo_tsk);
	fpsgo_composer_start();
	fpsgo_sysfs_publish();
	/* Release-publish the complete table after its queues can accept input. */
	perf_ioctl_dispatch_publish();
	return 0;
rollback:
	kfree(initial_enable);
	if (dispatch)
		perf_ioctl_dispatch_revoke();
	fpsgo_sysfs_quiesce();
#if IS_ENABLED(CONFIG_LIEPPOS_PERFORMANCE_TARGET_C)
	if (fpsgo_eas_slot >= 0) {
		adpf_unregister_callback(fpsgo_eas_slot);
		fpsgo_eas_slot = -1;
		eas_adpf_release_sessions();
	}
#endif
	if (fpsgo_adpf_slot >= 0) {
		adpf_unregister_callback(fpsgo_adpf_slot);
		fpsgo_adpf_slot = -1;
	}
#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
	if (drm)
		drm_unregister_fps_chg_callback(dfrc_fps_limit_cb);
#endif
#if !FPSGO_DYNAMIC_WL
	if (hook) {
		tracepoint_cleanup();
		tracepoint_synchronize_unregister();
	}
#endif
	fpsgo_stop_worker();
	if (gbe)
		exit_gbe_common();
	/* Dependency order is intentional. XGF (owned by FBT) first withdraws
	 * its callback from the composer registry; FBT is also an FSTB consumer.
	 * No child timer or public frame callback is started before the commit
	 * boundary, so rollback has no external producer left after the worker.
	 */
	if (fbt)
		fbt_cpu_exit();
	if (composer)
		fpsgo_composer_exit();
	if (fstb)
		mtk_fstb_exit();
	if (common)
		exit_fpsgo_common();
fail_sysfs:
	fpsgo_sysfs_exit();
fail_policy:
	fpsgo_cpu_policy_exit();
fail_state:
	smp_store_release(&fpsgo_runtime, FPSGO_FAILED);
	return ret;
}

module_init(fpsgo_init);
/* Legacy cross-module callback publications are permanent for this port.
 * The donor exit path does not unregister every tracepoint/timer/consumer.
 * Reject unload until full framework-wide rundown is implemented.
 */

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek FPSGO");
MODULE_AUTHOR("MediaTek Inc.");
MODULE_VERSION(FPSGO_VERSION_MODULE);
