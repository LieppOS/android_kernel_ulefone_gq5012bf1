// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/timer.h>

#include <linux/completion.h>
#include <linux/suspend.h>
#include <linux/sched/signal.h>
#include "common/include/c2ps_common.h"
#include "monitor/include/c2ps_monitor.h"
#include "uclamp_regulator/include/c2ps_uclamp_regulator.h"
#include "common/include/c2ps_usedext.h"
#include "common/include/c2ps_sysfs.h"

enum C2PS_NOTIFIER_PUSH_TYPE {
	C2PS_NOTIFIER_INIT          = 0x00,
	C2PS_NOTIFIER_UNINIT        = 0x01,
	C2PS_NOTIFIER_ADD_TASK      = 0x02,
	C2PS_NOTIFIER_TASK_START    = 0x03,
	C2PS_NOTIFIER_TASK_END      = 0x04,
	C2PS_NOTIFIER_SCENE_CHANGE  = 0x05,
	C2PS_NOTIFIER_CAMFPS        = 0x06,
	C2PS_NOTIFIER_VSYNC         = 0x07,
	C2PS_NOTIFIER_TASK_SINGLE_SHOT = 0x08,
	C2PS_NOTIFIER_PM_STOP       = 0x09, /* internal, no userspace command */
};

struct C2PS_NOTIFIER_PUSH_TAG {
	enum C2PS_NOTIFIER_PUSH_TYPE ePushType;
	int camfps;
	int pid;
	struct task_struct *task; /* reference captured in caller PID namespace */
	struct pid *actor;
	struct completion *done;
	int *result;
	bool internal;
	int task_id;
	int group_head;
	int scene_mode;
	u32 task_target_time;
	u32 default_uclamp;
	u32 task_group_target_time;
	u64 cur_ts;
	bool is_vip_task;
	bool is_dynamic_tid;
	char task_name[MAX_TASK_NAME_SIZE];
	int overwrite_uclamp_max[MAX_NUMBER_OF_CLUSTERS];
	int idle_rate_alert;
	int timeout;
	int uclamp_max_placeholder1[MAX_NUMBER_OF_CLUSTERS];
	int uclamp_max_placeholder2[MAX_NUMBER_OF_CLUSTERS];
	int uclamp_max_placeholder3[MAX_NUMBER_OF_CLUSTERS];
	bool reset_param;
	struct list_head queue_list;
};

static void c2ps_free_push(struct C2PS_NOTIFIER_PUSH_TAG *push)
{
	if (push->task)
		put_task_struct(push->task);
	put_pid(push->actor);
	c2ps_free(push, sizeof(*push));
}

static void c2ps_complete_push(struct C2PS_NOTIFIER_PUSH_TAG *push, int status)
{
	struct completion *done = push->done;
	int *result = push->result;

	*result = status;
	c2ps_free_push(push);
	complete(done);
}

static struct task_struct *c2ps_tsk;
static LIST_HEAD(head);
static int condition_notifier_wq;
static bool notifier_closing = true;
static unsigned int notifier_pending;
static bool session_active;
static bool activation_pending;
static struct pid *session_owner;
static bool pm_blocked;
static bool background_pending;
static bool expiration_pending;
static DEFINE_MUTEX(notifier_wq_lock);
static DECLARE_WAIT_QUEUE_HEAD(notifier_wq_queue);
static void self_uninit_timer_callback(struct timer_list *t);
static int picked_wl_table = 0;
static unsigned int background_monitor_duration = BACKGROUND_MONITOR_DURATION;
unsigned int c2ps_nr_clusters = 0;

struct timer_list backgroup_info_update_timer;
struct timer_list self_uninit_timer;

module_param(picked_wl_table, int, 0644);
module_param(background_monitor_duration, uint, 0644);

static void backgroup_info_update_timer_callback(struct timer_list *t)
{
	/* Timer context must never take common/regulator sleeping locks. */
	WRITE_ONCE(background_pending, true);
	wake_up_interruptible(&notifier_wq_queue);
}

static bool c2ps_process_alive(struct pid *owner)
{
	struct task_struct *leader;
	bool alive;

	rcu_read_lock();
	leader = pid_task(owner, PIDTYPE_TGID);
	/* A group leader may exit before its workers. Conversely, a zombie
	 * still in the PID table must not keep camera requests alive.
	 */
	alive = leader && atomic_read(&leader->signal->live) > 0;
	rcu_read_unlock();
	return alive;
}

static int c2ps_notifier_wq_cb_init(struct C2PS_NOTIFIER_PUSH_TAG *push)
{
	struct global_info *g_info;
	int i, ret, wl;

	if (!c2ps_process_alive(push->actor)) {
		ret = -ESRCH;
		goto fail_activation;
	}
	ret = init_c2ps_common();
	if (ret)
		goto fail_activation;
	g_info = get_glb_info();
	/* The provider takes a real, atomic native-state snapshot. Acquisition
	 * can fail without any policy change; do not publish the session then.
	 */
	wl = READ_ONCE(picked_wl_table);
	/* Preserve C2PS's table-zero fallback for an out-of-range module hint;
	 * the native provider still chooses auto when WL hardware is absent.
	 */
	if (wl < 0 || wl >= get_nr_wl_type())
		wl = 0;
	ret = mtk_perf_ctrl_acquire(c2ps_nr_clusters, push->overwrite_uclamp_max,
		wl, &g_info->native_ctrl);
	if (ret) {
		exit_c2ps_common();
		goto fail_activation;
	}
	set_config_camfps(push->camfps);
	/* The donor snapshots these AFTER installing the requested limits.
	 * Its former pre-allocation mutations broke that ordering on rollback.
	 * Baseline restoration now belongs to the provider, not this cache.
	 */
	for (i = 0; i < c2ps_nr_clusters; i++) {
		g_info->max_uclamp[i] = push->overwrite_uclamp_max[i];
		g_info->curr_max_uclamp[i] = push->overwrite_uclamp_max[i];
	}
	mutex_lock(&notifier_wq_lock);
	session_owner = get_pid(push->actor);
	WRITE_ONCE(session_active, true);
	activation_pending = false;
	mutex_unlock(&notifier_wq_lock);
	mod_timer(&self_uninit_timer, jiffies + C2PS_LEASE_TIMEOUT_SEC * HZ);
	mod_timer(&backgroup_info_update_timer, jiffies + 1);
	return 0;
fail_activation:
	mutex_lock(&notifier_wq_lock);
	activation_pending = false;
	mutex_unlock(&notifier_wq_lock);
	return ret;
}

static int c2ps_notifier_wq_cb_uninit(void)
{
	C2PS_LOGD("[C2PS_CB] uninit\n");
	if (!session_active)
		return -ENODEV;
	mutex_lock(&notifier_wq_lock);
	WRITE_ONCE(session_active, false);
	put_pid(session_owner);
	session_owner = NULL;
	mutex_unlock(&notifier_wq_lock);
	del_timer_sync(&backgroup_info_update_timer);
	del_timer_sync(&self_uninit_timer);
	WRITE_ONCE(background_pending, false);
	WRITE_ONCE(expiration_pending, false);
	/* No queued regulator write may run after policy state is restored. */
	c2ps_uclamp_regulator_flush();
	/* exit_c2ps_common releases the native token, restoring only a lease
	 * still owned by us. A legacy writer has already restored/preempted it.
	 */
	exit_c2ps_common();
	return 0;
}

static int c2ps_notifier_wq_cb_add_task(
	u32 task_id, u32 task_target_time, u32 default_uclamp,
	int group_head, u32 task_group_target_time, bool is_vip_task,
	bool is_dynamic_tid, const char *task_name)
{
	struct c2ps_task_info *tsk_info = NULL;
	bool created_group = false;
	int ret;

	if (c2ps_find_task_info_by_tskid(task_id))
		return -EEXIST;
	C2PS_LOGD("[C2PS_CB] add_task task_id: %d task_name: %s\n",
		task_id, task_name);

	tsk_info = kzalloc(sizeof(*tsk_info), GFP_KERNEL);

	if (unlikely(!tsk_info)) {
		C2PS_LOGE("OOM\n");
		return -ENOMEM;
	}

	tsk_info->task_id = task_id;
	tsk_info->task_target_time = task_target_time * NSEC_PER_MSEC;
	tsk_info->default_uclamp = default_uclamp;
	tsk_info->is_vip_task = is_vip_task;
	tsk_info->is_dynamic_tid = is_dynamic_tid;
	mutex_init(&tsk_info->mlock);
	strscpy(tsk_info->task_name, task_name, sizeof(tsk_info->task_name));

	/* No partially constructed task becomes hash-visible. */
	if (group_head > 0) {
		if (task_id == group_head) {
			ret = c2ps_create_task_group(group_head, task_group_target_time);
			if (ret)
				goto fail;
			created_group = true;
		}
		ret = c2ps_add_task_to_group(tsk_info, group_head);
		if (ret)
			goto fail;
	}
	ret = c2ps_add_task_info(tsk_info);
	if (!ret)
		return 0;
fail:
	if (created_group)
		c2ps_rollback_task_group(group_head);
	kfree(tsk_info);
	return ret;
}

static int c2ps_notifier_wq_cb_task_start(struct task_struct *task, int task_id)
{
	return monitor_task_start(task, task_id);
}

static int c2ps_notifier_wq_cb_task_end(struct task_struct *task, int task_id)
{
	return monitor_task_end(task, task_id);
}

static int c2ps_notifier_wq_cb_vsync(u64 ts)
{
	return monitor_vsync(ts);
}

static int c2ps_notifier_wq_cb_camfps(int camfps)
{
	return monitor_camfps(camfps);
}

static int c2ps_notifier_wq_cb_scene_change(int task_id, int scene_mode)
{
	return monitor_task_scene_change(task_id, scene_mode);
}

static int c2ps_notifier_wq_cb_task_single_shot(
	int *uclamp_max, int idle_rate_alert, int timeout,
	int *uclamp_max_placeholder1, int *uclamp_max_placeholder2,
	int *uclamp_max_placeholder3, bool reset_param)
{
	struct global_info *g_info = get_glb_info();

	if (!g_info) {
		C2PS_LOGE("glb_info is null\n");
		return -ENODEV;
	}

	mutex_lock(&g_info->mlock);
	if (need_update_single_shot_uclamp_max(uclamp_max))
		memcpy(g_info->overwrite_uclamp_max, uclamp_max,
			c2ps_nr_clusters * sizeof(int));
	if (unlikely(need_update_single_shot_uclamp_max(uclamp_max_placeholder1)))
		memcpy(g_info->uclamp_max_placeholder1, uclamp_max_placeholder1,
			c2ps_nr_clusters * sizeof(int));
	if (unlikely(need_update_single_shot_uclamp_max(uclamp_max_placeholder2)))
		memcpy(g_info->uclamp_max_placeholder2, uclamp_max_placeholder2,
			c2ps_nr_clusters * sizeof(int));
	if (unlikely(need_update_single_shot_uclamp_max(uclamp_max_placeholder3)))
		memcpy(g_info->uclamp_max_placeholder3, uclamp_max_placeholder3,
			c2ps_nr_clusters * sizeof(int));
	mutex_unlock(&g_info->mlock);
	return 0;
}

/* Sleepable control API (the donor already took a mutex). Preserve the UAPI
 * but return the worker's actual transaction result, not mere queue admission.
 * Stack completion storage cannot be abandoned on a signal while the worker
 * may still write it. No IRQ/timer path calls this function.
 */
static int c2ps_queue_work(struct C2PS_NOTIFIER_PUSH_TAG *vpPush)
{
	struct completion done;
	int result, ret = 0;

	if (unlikely(current == c2ps_tsk)) {
		c2ps_free_push(vpPush);
		return -EDEADLK;
	}
	vpPush->actor = get_task_pid(current, PIDTYPE_TGID);
	if (!vpPush->actor) {
		c2ps_free_push(vpPush);
		return -ESRCH;
	}
	init_completion(&done);
	vpPush->done = &done;
	vpPush->result = &result;
	mutex_lock(&notifier_wq_lock);
	if (notifier_closing)
		ret = -ESHUTDOWN;
	else if (notifier_pending >= 256)
		ret = -EAGAIN;
	else if (!vpPush->internal && pm_blocked)
		ret = -EBUSY;
	else if (vpPush->ePushType == C2PS_NOTIFIER_INIT &&
		 (READ_ONCE(session_active) || activation_pending))
		ret = -EALREADY;
	else if (!vpPush->internal && vpPush->ePushType != C2PS_NOTIFIER_INIT &&
		 !READ_ONCE(session_active) && !activation_pending)
		ret = -ENODEV;
	else if (!vpPush->internal && READ_ONCE(session_active) &&
		 vpPush->actor != session_owner)
		ret = -EPERM;
	if (ret) {
		mutex_unlock(&notifier_wq_lock);
		c2ps_free_push(vpPush);
		return ret;
	}
	if (vpPush->ePushType == C2PS_NOTIFIER_INIT)
		activation_pending = true;
	list_add_tail(&vpPush->queue_list, &head);
	notifier_pending++;
	condition_notifier_wq = 1;
	mutex_unlock(&notifier_wq_lock);
	wake_up_interruptible(&notifier_wq_queue);
	wait_for_completion(&done);
	return result;
}

static void c2ps_notifier_wq_cb(void)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush;
	int ret;

	wait_event_interruptible(notifier_wq_queue,
		READ_ONCE(condition_notifier_wq) || READ_ONCE(background_pending) ||
		READ_ONCE(expiration_pending) || kthread_should_stop());

	if (kthread_should_stop())
		return;
	if (xchg(&expiration_pending, false))
		c2ps_notifier_wq_cb_uninit();
	/* Native scalar clients can preempt the entire temporary overlay.
	 * Stop gracefully; never reassert a revoked token's boost request.
	 */
	if (session_active &&
	    (!c2ps_process_alive(session_owner) ||
	     READ_ONCE(get_glb_info()->native_ctrl_error) ||
	     !mtk_perf_ctrl_is_live(get_glb_info()->native_ctrl)))
		c2ps_notifier_wq_cb_uninit();
	if (session_active)
		c2ps_reap_dead_tasks();
	if (xchg(&background_pending, false) && session_active) {
		mutex_lock(&get_glb_info()->mlock);
		update_cpu_idle_rate();
		mutex_unlock(&get_glb_info()->mlock);
		mod_timer(&backgroup_info_update_timer, jiffies +
			msecs_to_jiffies(max(background_monitor_duration, 1U)));
	}
	mutex_lock(&notifier_wq_lock);

	if (!list_empty(&head)) {
		vpPush = list_first_entry(&head,
			struct C2PS_NOTIFIER_PUSH_TAG, queue_list);
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

	if (!vpPush->internal && vpPush->ePushType != C2PS_NOTIFIER_INIT) {
		if (!session_active) {
			ret = -ENODEV;
			goto finish;
		}
		if (vpPush->actor != session_owner) {
			ret = -EPERM;
			goto finish;
		}
		if (!c2ps_process_alive(vpPush->actor)) {
			ret = -ESRCH;
			goto finish;
		}
	}
	switch (vpPush->ePushType) {
	case C2PS_NOTIFIER_INIT:
		ret = c2ps_notifier_wq_cb_init(vpPush);
		break;
	case C2PS_NOTIFIER_PM_STOP:
		ret = session_active ? c2ps_notifier_wq_cb_uninit() : 0;
		break;
	case C2PS_NOTIFIER_UNINIT:
		ret = c2ps_notifier_wq_cb_uninit();
		break;
	case C2PS_NOTIFIER_ADD_TASK:
		ret = c2ps_notifier_wq_cb_add_task(
			vpPush->task_id,
			vpPush->task_target_time, vpPush->default_uclamp,
			vpPush->group_head, vpPush->task_group_target_time,
			vpPush->is_vip_task, vpPush->is_dynamic_tid,
			vpPush->task_name);
		break;
	case C2PS_NOTIFIER_TASK_START:
		ret = c2ps_notifier_wq_cb_task_start(vpPush->task, vpPush->task_id);
		break;
	case C2PS_NOTIFIER_TASK_END:
		ret = c2ps_notifier_wq_cb_task_end(vpPush->task, vpPush->task_id);
		break;
	case C2PS_NOTIFIER_SCENE_CHANGE:
		ret = c2ps_notifier_wq_cb_scene_change(vpPush->task_id, vpPush->scene_mode);
		break;
	case C2PS_NOTIFIER_CAMFPS:
		ret = c2ps_notifier_wq_cb_camfps(vpPush->camfps);
		break;
	case C2PS_NOTIFIER_VSYNC:
		ret = c2ps_notifier_wq_cb_vsync(vpPush->cur_ts);
		break;
	case C2PS_NOTIFIER_TASK_SINGLE_SHOT:
		ret = c2ps_notifier_wq_cb_task_single_shot(
			vpPush->overwrite_uclamp_max, vpPush->idle_rate_alert,
			vpPush->timeout, vpPush->uclamp_max_placeholder1,
			vpPush->uclamp_max_placeholder2, vpPush->uclamp_max_placeholder3,
			vpPush->reset_param);
		break;
	default:
		C2PS_LOGE("unhandled push type = %d\n",
				vpPush->ePushType);
		ret = -EINVAL;
		break;
	}
	if (!ret && session_active)
		mod_timer(&self_uninit_timer, jiffies + C2PS_LEASE_TIMEOUT_SEC * HZ);

	if (session_active && need_update_background()) {
		struct regulator_req *req = get_regulator_req();

		if (req != NULL) {
			req->glb_info = get_glb_info();
			send_regulator_req(req);
			reset_need_update_status();
		}
	}
finish:
	c2ps_complete_push(vpPush, ret);
}

static int c2ps_pm_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct C2PS_NOTIFIER_PUSH_TAG *push;
	int ret;

	if (event == PM_POST_SUSPEND || event == PM_POST_HIBERNATION ||
	    event == PM_POST_RESTORE) {
		mutex_lock(&notifier_wq_lock);
		pm_blocked = false;
		mutex_unlock(&notifier_wq_lock);
		return NOTIFY_OK;
	}
	if (event != PM_SUSPEND_PREPARE && event != PM_HIBERNATION_PREPARE &&
	    event != PM_RESTORE_PREPARE)
		return NOTIFY_DONE;
	mutex_lock(&notifier_wq_lock);
	pm_blocked = true;
	if (notifier_closing) {
		/* Registration can race PM before final publication, when no
		 * session can yet exist. Keep admission blocked until POST.
		 */
		mutex_unlock(&notifier_wq_lock);
		return NOTIFY_OK;
	}
	mutex_unlock(&notifier_wq_lock);
	push = kzalloc(sizeof(*push), GFP_KERNEL);
	if (!push) {
		ret = -ENOMEM;
		goto failed;
	}
	push->ePushType = C2PS_NOTIFIER_PM_STOP;
	push->internal = true;
	/* Blocking PM notifier, before freezer: neither worker is freezable.
	 * Join native requests and timers before allowing suspend to proceed.
	 */
	ret = c2ps_queue_work(push);
	if (!ret)
		return NOTIFY_OK;
failed:
	mutex_lock(&notifier_wq_lock);
	pm_blocked = false;
	mutex_unlock(&notifier_wq_lock);
	return notifier_from_errno(ret);
}

static struct notifier_block c2ps_pm_notifier = {
	.notifier_call = c2ps_pm_event,
};

static int c2ps_thread_loop(void *arg)
{
	set_user_nice(current, -20);

	while (!kthread_should_stop())
		c2ps_notifier_wq_cb();

	return 0;
}

int c2ps_notify_init(
	int cfg_camfps, int max_uclamp_cluster0, int max_uclamp_cluster1,
	int max_uclamp_cluster2)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int ret = 0;

	C2PS_LOGD(
		"config camfps (frames per 1000 seconds): %d, "
		"max_uclamp_cluster0: %d, max_uclamp_cluster1: %d, "
		"max_uclamp_cluster2: %d",
		cfg_camfps, max_uclamp_cluster0, max_uclamp_cluster1,
		max_uclamp_cluster2);

	/* Do not translate an all-zero hint into a global heavyloading override.
	 * That donor shortcut bypassed normal session allocation/ownership.
	 */
	if (cfg_camfps <= 0 || max_uclamp_cluster0 < 0 ||
	    max_uclamp_cluster1 < 0 || max_uclamp_cluster2 < 0 ||
	    max_uclamp_cluster0 > MAX_UCLAMP ||
	    max_uclamp_cluster1 > MAX_UCLAMP || max_uclamp_cluster2 > MAX_UCLAMP)
		return -EINVAL;

	vpPush = (struct C2PS_NOTIFIER_PUSH_TAG *)
		c2ps_alloc_atomic(sizeof(*vpPush));

	if (!vpPush) {
		C2PS_LOGE("OOM\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!c2ps_tsk) {
		C2PS_LOGE("NULL WorkQueue\n");
		c2ps_free(vpPush, sizeof(*vpPush));
		ret = -EINVAL;
		goto out;
	}

	vpPush->ePushType = C2PS_NOTIFIER_INIT;
	vpPush->camfps = cfg_camfps;
	vpPush->overwrite_uclamp_max[0] = max_uclamp_cluster0;
	vpPush->overwrite_uclamp_max[1] = max_uclamp_cluster1;
	vpPush->overwrite_uclamp_max[2] = max_uclamp_cluster2;
	ret = c2ps_queue_work(vpPush);

out:
	return ret;
}

int c2ps_notify_uninit(void)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int ret = 0;

	C2PS_LOGD("+\n");

	vpPush = (struct C2PS_NOTIFIER_PUSH_TAG *)
		c2ps_alloc_atomic(sizeof(*vpPush));

	if (!vpPush) {
		C2PS_LOGE("OOM\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!c2ps_tsk) {
		C2PS_LOGE("NULL WorkQueue\n");
		c2ps_free(vpPush, sizeof(*vpPush));
		ret = -EINVAL;
		goto out;
	}

	vpPush->ePushType = C2PS_NOTIFIER_UNINIT;
	ret = c2ps_queue_work(vpPush);

out:
	return ret;
}

int c2ps_notify_add_task(
	u32 task_id, u32 task_target_time, u32 default_uclamp,
	int group_head, u32 task_group_target_time,
	bool is_vip_task, bool is_dynamic_tid,
	const char *task_name)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int ret = 0;

	if (is_vip_task && !IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK))
		return -EOPNOTSUPP;
	if (!task_name || !task_target_time || default_uclamp > MAX_UCLAMP ||
	    (group_head > 0 && !task_group_target_time))
		return -EINVAL;
	C2PS_LOGD("task_id: %d\n", task_id);

	vpPush = (struct C2PS_NOTIFIER_PUSH_TAG *)
		c2ps_alloc_atomic(sizeof(*vpPush));

	if (!vpPush) {
		C2PS_LOGE("OOM\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!c2ps_tsk) {
		C2PS_LOGE("NULL WorkQueue\n");
		c2ps_free(vpPush, sizeof(*vpPush));
		ret = -EINVAL;
		goto out;
	}

	vpPush->ePushType = C2PS_NOTIFIER_ADD_TASK;
	vpPush->task_id = task_id;
	vpPush->task_target_time = task_target_time;
	vpPush->default_uclamp = default_uclamp;
	vpPush->group_head = group_head;
	vpPush->task_group_target_time = task_group_target_time;
	vpPush->is_vip_task = is_vip_task;
	vpPush->is_dynamic_tid = is_dynamic_tid;
	strscpy(vpPush->task_name, task_name, sizeof(vpPush->task_name));

	ret = c2ps_queue_work(vpPush);

out:
	return ret;
}

int c2ps_notify_task_start(int pid, int task_id)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int ret = 0;

	C2PS_LOGD("task_id: %d\n", task_id);

	vpPush = (struct C2PS_NOTIFIER_PUSH_TAG *)
		c2ps_alloc_atomic(sizeof(*vpPush));

	if (!vpPush) {
		C2PS_LOGE("OOM\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!c2ps_tsk) {
		C2PS_LOGE("NULL WorkQueue\n");
		c2ps_free(vpPush, sizeof(*vpPush));
		ret = -EINVAL;
		goto out;
	}

	vpPush->task = c2ps_get_task(pid);
	if (!vpPush->task) {
		c2ps_free_push(vpPush);
		return -ESRCH;
	}
	vpPush->ePushType = C2PS_NOTIFIER_TASK_START;
	vpPush->pid = pid;
	vpPush->task_id = task_id;

	ret = c2ps_queue_work(vpPush);

out:
	return ret;
}

int c2ps_notify_task_end(int pid, int task_id)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int ret = 0;

	C2PS_LOGD("task_id: %d\n", task_id);

	vpPush = (struct C2PS_NOTIFIER_PUSH_TAG *)
		c2ps_alloc_atomic(sizeof(*vpPush));

	if (!vpPush) {
		C2PS_LOGE("OOM\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!c2ps_tsk) {
		C2PS_LOGE("NULL WorkQueue\n");
		c2ps_free(vpPush, sizeof(*vpPush));
		ret = -EINVAL;
		goto out;
	}

	vpPush->task = c2ps_get_task(pid);
	if (!vpPush->task) {
		c2ps_free_push(vpPush);
		return -ESRCH;
	}
	vpPush->ePushType = C2PS_NOTIFIER_TASK_END;
	vpPush->pid = pid;
	vpPush->task_id = task_id;

	ret = c2ps_queue_work(vpPush);

out:
	return ret;
}

int c2ps_notify_task_scene_change(int task_id, int scene_mode)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int ret = 0;

	C2PS_LOGD("task_id: %d\n", task_id);

	vpPush = (struct C2PS_NOTIFIER_PUSH_TAG *)
		c2ps_alloc_atomic(sizeof(*vpPush));

	if (!vpPush) {
		C2PS_LOGE("OOM\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!c2ps_tsk) {
		C2PS_LOGE("NULL WorkQueue\n");
		c2ps_free(vpPush, sizeof(*vpPush));
		ret = -EINVAL;
		goto out;
	}

	vpPush->ePushType = C2PS_NOTIFIER_SCENE_CHANGE;
	vpPush->task_id = task_id;
	vpPush->scene_mode = scene_mode;

	ret = c2ps_queue_work(vpPush);

out:
	return ret;
}

int c2ps_notify_vsync(void)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int ret = 0;

	C2PS_LOGD("+\n");

	vpPush = (struct C2PS_NOTIFIER_PUSH_TAG *)
		c2ps_alloc_atomic(sizeof(*vpPush));

	if (!vpPush) {
		C2PS_LOGE("OOM\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!c2ps_tsk) {
		C2PS_LOGE("NULL WorkQueue\n");
		c2ps_free(vpPush, sizeof(*vpPush));
		ret = -EINVAL;
		goto out;
	}

	vpPush->ePushType = C2PS_NOTIFIER_VSYNC;
	vpPush->cur_ts = c2ps_get_time();

	ret = c2ps_queue_work(vpPush);

out:
	return ret;
}

int c2ps_notify_camfps(int camfps)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int ret = 0;

	C2PS_LOGD("camfps: %d\n", camfps);

	vpPush = (struct C2PS_NOTIFIER_PUSH_TAG *)
		c2ps_alloc_atomic(sizeof(*vpPush));

	if (!vpPush) {
		C2PS_LOGE("OOM\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!c2ps_tsk) {
		C2PS_LOGE("NULL WorkQueue\n");
		c2ps_free(vpPush, sizeof(*vpPush));
		ret = -EINVAL;
		goto out;
	}

	vpPush->ePushType = C2PS_NOTIFIER_CAMFPS;
	vpPush->camfps = camfps;

	ret = c2ps_queue_work(vpPush);

out:
	return ret;
}

int c2ps_notify_task_single_shot(
	int *uclamp_max, int idle_rate_alert, int timeout,
	int *uclamp_max_placeholder1, int *uclamp_max_placeholder2,
	int *uclamp_max_placeholder3, bool reset_param)
{
	struct C2PS_NOTIFIER_PUSH_TAG *vpPush = NULL;
	int i, ret = 0;

	/* Extra policy hints have no implementation in this authenticated
	 * regulator. Reject unsupported behavior rather than fake success.
	 */
	if (idle_rate_alert || timeout || reset_param)
		return -EOPNOTSUPP;
	if (!uclamp_max || !uclamp_max_placeholder1 ||
		!uclamp_max_placeholder2 || !uclamp_max_placeholder3) {
		C2PS_LOGE("null uclamp max pointer\n");
		ret = -EINVAL;
		goto out;
	}
	for (i = 0; i < MAX_NUMBER_OF_CLUSTERS; i++)
		if (uclamp_max[i] > MAX_UCLAMP ||
		    uclamp_max_placeholder1[i] > MAX_UCLAMP ||
		    uclamp_max_placeholder2[i] > MAX_UCLAMP ||
		    uclamp_max_placeholder3[i] > MAX_UCLAMP)
			return -EINVAL;

	vpPush = (struct C2PS_NOTIFIER_PUSH_TAG *)
		c2ps_alloc_atomic(sizeof(*vpPush));

	if (!vpPush) {
		C2PS_LOGE("OOM\n");
		ret = -ENOMEM;
		goto out;
	}

	if (!c2ps_tsk) {
		C2PS_LOGE("NULL WorkQueue\n");
		c2ps_free(vpPush, sizeof(*vpPush));
		ret = -EINVAL;
		goto out;
	}

	memset(vpPush->overwrite_uclamp_max, 0,
		MAX_NUMBER_OF_CLUSTERS * sizeof(int));
	memcpy(vpPush->overwrite_uclamp_max, uclamp_max,
		MAX_NUMBER_OF_CLUSTERS * sizeof(int));
	memset(vpPush->uclamp_max_placeholder1, 0,
		MAX_NUMBER_OF_CLUSTERS * sizeof(int));
	memcpy(vpPush->uclamp_max_placeholder1, uclamp_max_placeholder1,
		MAX_NUMBER_OF_CLUSTERS * sizeof(int));
	memset(vpPush->uclamp_max_placeholder2, 0,
		MAX_NUMBER_OF_CLUSTERS * sizeof(int));
	memcpy(vpPush->uclamp_max_placeholder2, uclamp_max_placeholder2,
		MAX_NUMBER_OF_CLUSTERS * sizeof(int));
	memset(vpPush->uclamp_max_placeholder3, 0,
		MAX_NUMBER_OF_CLUSTERS * sizeof(int));
	memcpy(vpPush->uclamp_max_placeholder3, uclamp_max_placeholder3,
		MAX_NUMBER_OF_CLUSTERS * sizeof(int));
	vpPush->idle_rate_alert = idle_rate_alert;
	vpPush->timeout = timeout;
	vpPush->reset_param = reset_param;
	vpPush->ePushType = C2PS_NOTIFIER_TASK_SINGLE_SHOT;

	ret = c2ps_queue_work(vpPush);

out:
	return ret;
}

static void self_uninit_timer_callback(struct timer_list *t)
{
	WRITE_ONCE(expiration_pending, true);
	wake_up_interruptible(&notifier_wq_queue);
}

static int __init c2ps_init(void)
{
	int ret;

	c2ps_nr_clusters = get_nr_gears();
	if (!c2ps_nr_clusters || c2ps_nr_clusters > 3 ||
	    c2ps_nr_clusters > MAX_NUMBER_OF_CLUSTERS)
		return -ENODEV;
	timer_setup(&self_uninit_timer, self_uninit_timer_callback, 0);
	timer_setup(&backgroup_info_update_timer,
		backgroup_info_update_timer_callback, 0);
	ret = c2ps_sysfs_init();
	if (ret)
		return ret;
	ret = uclamp_regulator_init();
	if (ret)
		goto fail_sysfs;
	c2ps_tsk = kthread_create(c2ps_thread_loop, NULL, "c2ps_thread_loop");
	if (IS_ERR(c2ps_tsk)) {
		ret = PTR_ERR(c2ps_tsk);
		c2ps_tsk = NULL;
		goto fail_regulator;
	}

	ret = register_pm_notifier(&c2ps_pm_notifier);
	if (ret) {
		if (ret > 0)
			ret = -ret;
		kthread_stop(c2ps_tsk);
		c2ps_tsk = NULL;
		goto fail_regulator;
	}

	/* Raw donor callback slots cannot be safely unregistered. Keep their
	 * text, worker, request cache and root alive for the rest of this boot.
	 * All fallible setup is complete; forced unload is unsupported.
	 */
	__module_get(THIS_MODULE);
	mutex_lock(&notifier_wq_lock);
	notifier_closing = false;
	mutex_unlock(&notifier_wq_lock);
	wake_up_process(c2ps_tsk);

	c2ps_notify_init_fp = c2ps_notify_init;
	c2ps_notify_uninit_fp = c2ps_notify_uninit;
	c2ps_notify_add_task_fp = c2ps_notify_add_task;
	c2ps_notify_task_start_fp = c2ps_notify_task_start;
	c2ps_notify_task_end_fp = c2ps_notify_task_end;
	c2ps_notify_vsync_fp = c2ps_notify_vsync;
	c2ps_notify_camfps_fp = c2ps_notify_camfps;
	c2ps_notify_task_scene_change_fp = c2ps_notify_task_scene_change;
	c2ps_notify_task_single_shot_fp = c2ps_notify_task_single_shot;

	c2ps_publish_callbacks();
	return 0;
fail_regulator:
	uclamp_regulator_exit();
fail_sysfs:
	c2ps_sysfs_exit();
	return ret;
}

module_init(c2ps_init);
/* No module_exit: lifetime is explicitly permanent after raw-slot publish. */

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek C2PS");
MODULE_AUTHOR("MediaTek Inc.");
