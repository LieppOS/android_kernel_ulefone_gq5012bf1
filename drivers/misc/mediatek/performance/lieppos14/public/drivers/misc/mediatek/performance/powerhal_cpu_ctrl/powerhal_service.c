// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/preempt.h>
#include <linux/trace_events.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
#include <linux/sort.h>
#include <linux/mutex.h>
#include <linux/srcu.h>
#include <linux/workqueue.h>
#include <linux/pid.h>
#include <linux/sched/signal.h>
#include <linux/jiffies.h>

#ifndef CREATE_TRACE_POINTS
#define CREATE_TRACE_POINTS
#endif
#include "powerhal_trace_event.h"
#include "powerhal_cpu_ctrl.h"
#include "adpf_session_snapshot.h"
#include "adpf_pid_identity.h"
#include "powerhal_policy.h"
#include "../perf_ioctl/powerhal_lifetime.h"
#include "../perf_ioctl/perf_ioctl_lifetime.h"
#include "../../cpufreq_lite/cpufreq-dbg-lite.h"
//#include "mtk_perfmgr_internal.h"
/* PROCFS */
#ifdef PROC_FOPS_RW
#undef PROC_FOPS_RW
#endif
#define PROC_FOPS_RW(name) \
static const struct proc_ops perfmgr_ ## name ## _proc_fops = { \
	.proc_read	= perfmgr_ ## name ## _proc_show, \
	.proc_write	= perfmgr_ ## name ## _proc_write,\
	.proc_open	= perfmgr_proc_open, \
}

#ifdef PROC_FOPS_RO
#undef PROC_FOPS_RO
#endif
#define PROC_FOPS_RO(name) \
static const struct proc_ops perfmgr_ ## name ## _proc_fops = { \
	.proc_read	= perfmgr_ ## name ## _proc_show, \
	.proc_open	= perfmgr_proc_open, \
}


#ifdef PROC_ENTRY
#undef PROC_ENTRY
#endif

#define PROC_ENTRY(name) {__stringify(name), &perfmgr_ ## name ## _proc_fops}


#define show_debug(fmt, x...) \
	do { \
		if (debug_enable) \
			pr_debug(fmt, ##x); \
	} while (0)

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define CLUSTER_MAX 10

typedef void (*fpsgo_notify_is_boost_cb)(int fpsgo_is_boosting);
int (*register_get_fpsgo_is_boosting_fp)(fpsgo_notify_is_boost_cb func_cb);
EXPORT_SYMBOL_GPL(register_get_fpsgo_is_boosting_fp);
int (*unregister_get_fpsgo_is_boosting_fp)(fpsgo_notify_is_boost_cb func_cb);
EXPORT_SYMBOL_GPL(unregister_get_fpsgo_is_boosting_fp);

struct k_list {
	struct list_head queue_list;
	int _cmd;
	int _value;
};

static LIST_HEAD(head);
static int condition_get_cmd;
static DECLARE_WAIT_QUEUE_HEAD(boost_queue);
static DEFINE_MUTEX(boost_lock);
static DEFINE_MUTEX(cpu_boost_lock);
static DEFINE_MUTEX(cpu_ctrl_lock);
static DEFINE_MUTEX(sf_hint_lock);
static bool boost_readers_closing = true;
static struct proc_dir_entry *powerhal_proc_root;

// ADPF
#define ADPF_MAX_SESSION 64
#define ADPF_MAX_CALLBACK 64
#define ADPF_MAX_PENDING_EVENTS 256
static int adpf_enable = 1;
static struct _SESSION *sessionList[ADPF_MAX_SESSION];
static adpfCallback adpfCallbackList[ADPF_MAX_CALLBACK];
static DEFINE_MUTEX(adpf_mutex);
DEFINE_STATIC_SRCU(adpf_callback_srcu);
static struct workqueue_struct *adpf_events;
static atomic_t adpf_pending = ATOMIC_INIT(0);
static struct _SESSION adpf_sessions[ADPF_MAX_SESSION];
static struct _SESSION_WORK_DURATION adpf_durations[ADPF_MAX_SESSION][ADPF_MAX_THREAD];
static int sf_hint_low_power_enabled;
/* Sessions belong to the creating process, not an ioctl file description.
 * The HAL may open a new descriptor for every command. Retaining struct pid
 * also prevents a recycled numeric PID from inheriting the old sessions.
 */
static struct pid *adpf_owners[ADPF_MAX_SESSION];
static struct pid *adpf_targets[ADPF_MAX_SESSION];
static bool adpf_stopping = true;
static void adpf_reap_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(adpf_reaper, adpf_reap_work);
static int adpf_close_all_locked(void);

/* Callbacks receive an owned snapshot, never pointers into a session that
 * another ioctl can close or reuse. The ordered queue preserves mutation order.
 */
struct adpf_event {
	struct work_struct work;
	struct adpf_session_snapshot snapshot;
};

static void adpf_put_thread_refs(struct _SESSION *session)
{
	int i;

	for (i = 0; i < ADPF_MAX_THREAD; i++) {
		put_pid(session->thread_owners[i]);
		session->thread_owners[i] = NULL;
	}
}

static void adpf_dispatch_event(struct work_struct *work)
{
	struct adpf_event *event = container_of(work, struct adpf_event, work);
	int i, idx = srcu_read_lock(&adpf_callback_srcu);

	for (i = 0; i < ADPF_MAX_CALLBACK; i++) {
		adpfCallback callback = READ_ONCE(adpfCallbackList[i]);

		if (callback) {
			int ret = callback(&event->snapshot.session);

			if (ret)
				pr_warn_ratelimited("ADPF consumer %d cmd %u failed: %d\n",
					i, event->snapshot.session.cmd, ret);
		}
	}
	srcu_read_unlock(&adpf_callback_srcu, idx);
	adpf_put_thread_refs(&event->snapshot.session);
	put_pid(event->snapshot.session.owner);
	put_pid(event->snapshot.session.target);
	atomic_dec(&adpf_pending);
	kfree(event);
}

void send_boost_cmd(int cmd, int value)
{
	struct k_list *node;

	mutex_lock(&boost_lock);
	if (READ_ONCE(boost_readers_closing))
		goto out;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (node == NULL)
		goto out;

	node->_value = value;
	node->_cmd = cmd;

	list_add_tail(&node->queue_list, &head);
	WRITE_ONCE(condition_get_cmd, 1);

out:
	mutex_unlock(&boost_lock);

	wake_up_interruptible(&boost_queue);
}

void _boost_get_cmd(int *cmd, int *value)
{
	struct k_list *node;

	if (!cmd || !value)
		return;
	*cmd = *value = -1;
	if (wait_event_interruptible(boost_queue,
		READ_ONCE(boost_readers_closing) || READ_ONCE(condition_get_cmd)))
		return;

	mutex_lock(&boost_lock);

	if (!READ_ONCE(boost_readers_closing) && !list_empty(&head)) {
		node = list_first_entry(&head, struct k_list, queue_list);

		*cmd = node->_cmd;
		*value = node->_value;

		list_del(&node->queue_list);
		kfree(node);
	}

	WRITE_ONCE(condition_get_cmd, !list_empty(&head));

	mutex_unlock(&boost_lock);
}

void notify_fpsgo_is_boost(int is_boosting)
{
	send_boost_cmd(FPSGO_BOOST, is_boosting);
}

void notify_cpu_is_boost(int is_boosting)
{
	send_boost_cmd(CPUFREQ_BOOST, is_boosting);
}

static void _adpf_systrace(int tgid, long val, const char *fmt, ...)
{
	char log[256];
	va_list args;
	int len;
	char buf[256];

	memset(log, ' ', sizeof(log));
	va_start(args, fmt);
	len = vsnprintf(log, sizeof(log), fmt, args);
	va_end(args);

	if (unlikely(len < 0))
		return;
	else if (unlikely(len == 256))
		log[255] = '\0';

	len = snprintf(buf, sizeof(buf), "C|%d|%s|%ld\n", tgid, log, val);
	if (unlikely(len < 0))
		return;
	else if (unlikely(len == 256))
		buf[255] = '\0';

	trace_powerhal_adpf(buf);
}

static void _cpu_ctrl_systrace(int val, const char *fmt, ...)
{
	char log[256];
	va_list args;
	int len;
	char buf[256];

	memset(log, ' ', sizeof(log));
	va_start(args, fmt);
	len = vsnprintf(log, sizeof(log), fmt, args);
	va_end(args);

	if (unlikely(len < 0))
		return;
	else if (unlikely(len == 256))
		log[255] = '\0';

	len = snprintf(buf, sizeof(buf), "C|%d|%s|%d\n", current->pid, log, val);
	if (unlikely(len < 0))
		return;
	else if (unlikely(len == 256))
		buf[255] = '\0';

	trace_powerhal_cpu_freq_user_setting(buf);
}

char *perfmgr_copy_from_user_for_proc(const char __user *buffer, size_t count)
{
	char *buf = (char *)__get_free_page(GFP_USER);

	if (!buf)
		return NULL;

	if (count >= PAGE_SIZE)
		goto out;

	if (copy_from_user(buf, buffer, count))
		goto out;

	buf[count] = '\0';

	return buf;

out:
	free_page((unsigned long)buf);

	return NULL;
}

static int perfmgr_proc_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t perfmgr_perfserv_freq_proc_write(struct file *filp,
		const char *ubuf, size_t cnt, loff_t *data)
{
	struct powerhal_policy_state old[POWERHAL_MAX_POLICIES], state;
	int values[POWERHAL_MAX_POLICIES * 2];
	char *buf, *cursor, *token;
	int count = powerhal_policy_count(), n = 0, i, ret = -EINVAL, boosting = 0;

	if (!cnt || cnt >= PAGE_SIZE || count <= 0 || count > POWERHAL_MAX_POLICIES)
		return -EINVAL;
	buf = memdup_user_nul(ubuf, cnt);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	cursor = buf;
	while ((token = strsep(&cursor, " \t\n"))) {
		if (!*token)
			continue;
		if (n >= count * 2 || kstrtoint(token, 10, &values[n]) || values[n] < -1)
			goto free;
		n++;
	}
	if (n != count * 2)
		goto free;
	/* No global desired-state mutation occurs on a parse failure. Serialize
	 * complete writes, retain per-policy ownership and roll back earlier
	 * domains if a later policy disappears or rejects its request.
	 */
	mutex_lock(&cpu_boost_lock);
	for (i = 0; i < count; i++) {
		ret = powerhal_policy_read(i, &old[i]);
		if (ret)
			goto unlock;
	}
	for (i = 0; i < count; i++) {
		ret = powerhal_policy_set(i, values[i * 2], values[i * 2 + 1]);
		if (ret) {
			while (--i >= 0) {
				int rollback = powerhal_policy_set(i, old[i].min, old[i].max);

				if (rollback)
					pr_warn("powerhal: policy %d rollback failed: %d\n", i, rollback);
			}
			goto unlock;
		}
	}
	for (i = 0; i < count; i++) {
		if (!powerhal_policy_read(i, &state)) {
			boosting |= state.min > state.hardware_min;
			_cpu_ctrl_systrace(state.min, "powerhal_cpu_ctrl c%d min", i);
			_cpu_ctrl_systrace(state.max, "powerhal_cpu_ctrl c%d max", i);
		}
	}
	if (READ_ONCE(sf_hint_low_power_enabled))
		notify_cpu_is_boost(boosting);
	ret = 0;
unlock:
	mutex_unlock(&cpu_boost_lock);
free:
	kfree(buf);
	return ret ? ret : cnt;
}

static ssize_t perfmgr_perfserv_freq_proc_show(struct file *file,
		char __user *ubuf, size_t count, loff_t *ppos)
{
	int i, n = 0, policies = powerhal_policy_count();
	char buffer[512];
	struct powerhal_policy_state state;

	for (i = 0; i < policies && n < sizeof(buffer) - 1; i++) {
		int ret = powerhal_policy_read(i, &state);

		if (ret)
			return ret;
		n += scnprintf(buffer + n, sizeof(buffer) - n, "%d %d ", state.min, state.max);
	}
	n += scnprintf(buffer + n, sizeof(buffer) - n, "\n");
	return simple_read_from_buffer(ubuf, count, ppos, buffer, n);
}


static ssize_t perfmgr_sf_hint_low_power_enabled_proc_write(
		struct file *filp, const char *ubuf,
		size_t cnt, loff_t *_data)
{
	char buf[64];
	int value, index;
	int ret;

	if (cnt >= sizeof(buf) || copy_from_user(buf, ubuf, cnt))
		return -EINVAL;

	buf[cnt] = 0;
	ret = kstrtoint(buf, 10, &value);
	if (ret < 0)
		return ret;

	if (value != 0 && value != 1)
		return -EINVAL;
	index = perf_ioctl_dispatch_enter();
	if (index < 0)
		return index;
	mutex_lock(&sf_hint_lock);
	if (value) {
		typeof(register_get_fpsgo_is_boosting_fp) cb =
			READ_ONCE(register_get_fpsgo_is_boosting_fp);

		ret = cb ? cb(notify_fpsgo_is_boost) : -ENODEV;
		if (ret == -EEXIST)
			ret = 0;
	} else {
		typeof(unregister_get_fpsgo_is_boosting_fp) cb =
			READ_ONCE(unregister_get_fpsgo_is_boosting_fp);

		ret = cb ? cb(notify_fpsgo_is_boost) : -ENODEV;
		if (ret == -ENOENT)
			ret = 0;
	}
	if (ret > 0)
		ret = -EIO;
	if (!ret)
		WRITE_ONCE(sf_hint_low_power_enabled, value);
	mutex_unlock(&sf_hint_lock);
	ret = perf_ioctl_dispatch_leave(index, ret);
	return ret ? ret : cnt;
}

static ssize_t perfmgr_sf_hint_low_power_enabled_proc_show(struct file *file,
		char __user *ubuf, size_t count, loff_t *ppos)
{
	int n = 0;
	char buffer[64];

	if (*ppos != 0)
		goto out;
	n = scnprintf(buffer, 64, "%d\n", READ_ONCE(sf_hint_low_power_enabled));
out:
	if (n < 0)
		return -EINVAL;

	return simple_read_from_buffer(ubuf, count, ppos, buffer, n);
}


static ssize_t perfmgr_adpf_enable_proc_write(struct file *filp,
		const char *ubuf, size_t cnt, loff_t *data)
{
	char buf[64];
	int value;
	int ret;

	if (cnt >= sizeof(buf) || copy_from_user(buf, ubuf, cnt))
		return -EINVAL;

	buf[cnt] = 0;
	ret = kstrtoint(buf, 10, &value);
	if (ret < 0)
		return ret;

	if (value != 0 && value != 1)
		return -EINVAL;
	mutex_lock(&adpf_mutex);
	if (!value && adpf_enable) {
		/* Disabled sessions must release their consumers' performance
		 * requests, not remain silently live with all future ioctls ignored.
		 */
		ret = adpf_close_all_locked();
		if (ret) {
			mutex_unlock(&adpf_mutex);
			return ret;
		}
	}
	WRITE_ONCE(adpf_enable, value);
	mutex_unlock(&adpf_mutex);
	return cnt;
}

static ssize_t perfmgr_adpf_enable_proc_show(struct file *file,
		char __user *ubuf, size_t count, loff_t *ppos)
{
	int n = 0;
	char buffer[64];

	if (*ppos != 0)
		goto out;
	n = scnprintf(buffer, 64, "%d\n", READ_ONCE(adpf_enable));
out:
	if (n < 0)
		return -EINVAL;

	return simple_read_from_buffer(ubuf, count, ppos, buffer, n);
}

PROC_FOPS_RW(sf_hint_low_power_enabled);
PROC_FOPS_RW(perfserv_freq);
PROC_FOPS_RW(adpf_enable);

int adpf_register_callback(adpfCallback callback)
{
	int i, ret = -ENOSPC;

	if (!callback)
		return -EINVAL;
	mutex_lock(&adpf_mutex);
	if (!adpf_events) {
		ret = -ENODEV;
		goto out;
	}
	for (i = 0; i < ADPF_MAX_CALLBACK; i++) {
		if (adpfCallbackList[i] == callback) {
			ret = -EEXIST;
			goto out;
		}
	}
	for (i = 0; i < ADPF_MAX_CALLBACK; i++) {
		if (!adpfCallbackList[i]) {
			WRITE_ONCE(adpfCallbackList[i], callback);
			ret = i; /* This is the handle required by unregister. */
			break;
		}
	}
out:
	mutex_unlock(&adpf_mutex);
	return ret;
}
EXPORT_SYMBOL(adpf_register_callback);

int adpf_unregister_callback(int idx)
{
	if (idx < 0 || idx >= ADPF_MAX_CALLBACK)
		return -EINVAL;
	mutex_lock(&adpf_mutex);
	WRITE_ONCE(adpfCallbackList[idx], NULL);
	mutex_unlock(&adpf_mutex);
	/* Must not be called by an ADPF callback itself. */
	synchronize_srcu(&adpf_callback_srcu);
	return idx;
}
EXPORT_SYMBOL(adpf_unregister_callback);

/* Prepare all fallible storage before changing a live session. Reservation
 * includes prepared (not yet queued) events, including a disable-all batch.
 * Caller holds adpf_mutex through commit/abort and ordered queue insertion.
 */
static struct adpf_event *adpf_prepare_event(unsigned int cmd, unsigned int sid)
{
	struct adpf_event *event;
	int i, ret, cleanup_reserve = 0;

	lockdep_assert_held(&adpf_mutex);
	if (adpf_stopping || !adpf_events)
		return ERR_PTR(-ESHUTDOWN);
	if (sid >= ADPF_MAX_SESSION || !sessionList[sid])
		return ERR_PTR(-EINVAL);
	/* Keep one future CLOSE slot for every live session, including a
	 * candidate CREATE. pending + live <= limit is preserved by commit:
	 * CLOSE trades one live slot for one queued event. Thus ordinary
	 * producers cannot starve a full disable/reaper cleanup batch even
	 * when session IDs are reused before older CLOSE events dispatch.
	 * Allocation failure still preserves the live session for retry.
	 */
	if (cmd != ADPF_CLOSE) {
		for (i = 0; i < ADPF_MAX_SESSION; i++)
			if (sessionList[i] && sessionList[i]->used == SESSION_USED)
				cleanup_reserve++;
		if (cmd == ADPF_CREATE_HINT_SESSION)
			cleanup_reserve++;
	}
	if (atomic_read(&adpf_pending) >= ADPF_MAX_PENDING_EVENTS - cleanup_reserve)
		return ERR_PTR(-ENOMEM);
	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return ERR_PTR(-ENOMEM);
	ret = adpf_copy_session(&event->snapshot, sessionList[sid]);
	if (ret) {
		kfree(event);
		return ERR_PTR(ret);
	}
	for (i = 0; i < ADPF_MAX_THREAD; i++)
		event->snapshot.session.thread_owners[i] =
			get_pid(event->snapshot.session.thread_owners[i]);
	event->snapshot.session.cmd = cmd;
	INIT_WORK(&event->work, adpf_dispatch_event);
	atomic_inc(&adpf_pending);
	return event;
}

static void adpf_abort_event(struct adpf_event *event)
{
	adpf_put_thread_refs(&event->snapshot.session);
	atomic_dec(&adpf_pending);
	kfree(event);
}

static void adpf_commit_event(struct adpf_event *event)
{
	unsigned int sid = event->snapshot.session.sid;
	int i;

	lockdep_assert_held(&adpf_mutex);
	/* Transfer an independent process identity with the committed event.
	 * A CLOSE may run after the process died and its numeric ID was reused.
	 * The live session only borrows adpf_owners[sid]; the event owns this ref.
	 */
	event->snapshot.session.owner = get_pid(adpf_owners[sid]);
	event->snapshot.session.target = get_pid(adpf_targets[sid]);
	adpf_put_thread_refs(sessionList[sid]);
	*sessionList[sid] = event->snapshot.session;
	for (i = 0; i < ADPF_MAX_THREAD; i++) {
		sessionList[sid]->thread_owners[i] =
			get_pid(event->snapshot.session.thread_owners[i]);
		adpf_durations[sid][i] = event->snapshot.duration[i];
		sessionList[sid]->workDuration[i] = &adpf_durations[sid][i];
	}
	/* Fresh work item, private ordered queue: insertion cannot fail. */
	queue_work(adpf_events, &event->work);
}

static bool adpf_owner_alive(unsigned int sid)
{
	return adpf_process_alive(adpf_owners[sid]) &&
		adpf_process_alive(adpf_targets[sid]);
}

static int adpf_check_owner(unsigned int sid)
{
	lockdep_assert_held(&adpf_mutex);
	if (adpf_stopping || !adpf_events)
		return -ESHUTDOWN;
	if (sid >= ADPF_MAX_SESSION || !sessionList[sid])
		return -EINVAL;
	if (sessionList[sid]->used != SESSION_USED)
		return -ENOENT;
	if (adpf_owners[sid] != task_tgid(current))
		return -EPERM;
	if (!adpf_enable)
		return -EACCES;
	return 0;
}

static void adpf_finish_close(struct adpf_event *event)
{
	unsigned int sid = event->snapshot.session.sid;

	adpf_commit_event(event);
	sessionList[sid]->used = SESSION_UNUSED;
	adpf_put_thread_refs(sessionList[sid]);
	sessionList[sid]->owner = NULL;
	sessionList[sid]->target = NULL;
	put_pid(adpf_owners[sid]);
	adpf_owners[sid] = NULL;
	put_pid(adpf_targets[sid]);
	adpf_targets[sid] = NULL;
}

static int adpf_close_locked(unsigned int sid)
{
	struct adpf_event *event = adpf_prepare_event(ADPF_CLOSE, sid);

	if (IS_ERR(event))
		return PTR_ERR(event);
	adpf_finish_close(event);
	return 0;
}

static int adpf_close_all_locked(void)
{
	struct adpf_event *events[ADPF_MAX_SESSION] = {};
	int i, ret;

	/* Do not close only a prefix if allocation/capacity fails midway. */
	for (i = 0; i < ADPF_MAX_SESSION; i++) {
		if (!sessionList[i] || sessionList[i]->used != SESSION_USED)
			continue;
		events[i] = adpf_prepare_event(ADPF_CLOSE, i);
		if (IS_ERR(events[i])) {
			ret = PTR_ERR(events[i]);
			events[i] = NULL;
			goto abort;
		}
	}
	for (i = 0; i < ADPF_MAX_SESSION; i++)
		if (events[i])
			adpf_finish_close(events[i]);
	return 0;
abort:
	for (i = 0; i < ADPF_MAX_SESSION; i++)
		if (events[i])
			adpf_abort_event(events[i]);
	return ret;
}

static void adpf_reap_work(struct work_struct *work)
{
	int i;
	bool active = false;

	mutex_lock(&adpf_mutex);
	if (adpf_stopping)
		goto out;
	for (i = 0; i < ADPF_MAX_SESSION; i++) {
		if (sessionList[i]->used != SESSION_USED)
			continue;
		/* A failed close retains ownership and is retried, never silently
		 * discarding the consumer's cleanup event under memory pressure.
		 */
		if (!adpf_owner_alive(i))
			adpf_close_locked(i);
		active |= sessionList[i]->used == SESSION_USED;
	}
	if (active)
		queue_delayed_work(system_wq, &adpf_reaper, HZ);
out:
	mutex_unlock(&adpf_mutex);
}

/* No user-visible per-fd lease is imposed: all HAL threads in the creating
 * thread group may submit transactions, even through a newly opened fd.
 */
static struct adpf_event *adpf_prepare_owned_event(unsigned int cmd, unsigned int sid)
{
	int ret = adpf_check_owner(sid);

	return ret ? ERR_PTR(ret) : adpf_prepare_event(cmd, sid);
}

/* Capture all caller-namespace thread identities before changing either state
 * or the event. Failure releases only the candidate, including partial refs.
 */
static int adpf_capture_threads(struct _SESSION *next, struct pid *target,
	int *ids, int count)
{
	struct pid *captured[ADPF_MAX_THREAD] = {};
	struct task_struct *task;
	int i, j, ret = 0;

	rcu_read_lock();
	for (i = 0; i < count; i++) {
		for (j = 0; j < i; j++)
			if (ids[i] == ids[j]) {
				ret = -EINVAL;
				goto unlock;
			}
		task = find_task_by_vpid(ids[i]);
		if (!task || !pid_alive(task) || READ_ONCE(task->exit_state) ||
			(READ_ONCE(task->flags) & PF_EXITING)) {
			ret = -ESRCH;
			goto unlock;
		}
		if (task_tgid(task) != target ||
			(READ_ONCE(task->flags) & PF_KTHREAD)) {
			ret = -EPERM;
			goto unlock;
		}
		captured[i] = get_task_pid(task, PIDTYPE_PID);
	}
unlock:
	rcu_read_unlock();
	if (ret) {
		for (i = 0; i < count; i++)
			put_pid(captured[i]);
		return ret;
	}
	adpf_put_thread_refs(next);
	memset(next->threadIds, 0, sizeof(next->threadIds));
	for (i = 0; i < count; i++) {
		next->thread_owners[i] = captured[i]; /* move candidate ownership */
		next->threadIds[i] = pid_nr(captured[i]);
	}
	next->threadIds_size = count;
	return 0;
}

int adpf_create_session_hint(unsigned int sid, unsigned int tgid,
			    unsigned int uid, int *threadIds,
			    int threadIds_size, long durationNanos)
{
	struct adpf_event *event;
	struct _SESSION *next;
	struct pid *owner, *target;
	int i, ret;

	if (!threadIds || threadIds_size <= 0 || threadIds_size > ADPF_MAX_THREAD ||
	    durationNanos <= 0 || !tgid || sid >= ADPF_MAX_SESSION)
		return -EINVAL;
	for (i = 0; i < threadIds_size; i++)
		if (threadIds[i] <= 0)
			return -EINVAL;

	mutex_lock(&adpf_mutex);
	if (!adpf_enable) {
		ret = -EACCES;
		goto out;
	}
	if (adpf_stopping || !adpf_events) {
		ret = -ESHUTDOWN;
		goto out;
	}
	if (sessionList[sid]->used == SESSION_USED) {
		if (adpf_owner_alive(sid)) {
			ret = -EEXIST;
			goto out;
		}
		/* A replacement daemon need not wait for the periodic reap, but
		 * cannot reuse the id without ordering the old CLOSE first.
		 */
		ret = adpf_close_locked(sid);
		if (ret)
			goto out;
	}
	event = adpf_prepare_event(ADPF_CREATE_HINT_SESSION, sid);
	if (IS_ERR(event)) {
		ret = PTR_ERR(event);
		goto out;
	}
	owner = get_task_pid(current, PIDTYPE_TGID);
	if (!owner) {
		adpf_abort_event(event);
		ret = -ESRCH;
		goto out;
	}
	/* Capture the app identity in the ioctl's namespace before committing.
	 * Holding its struct pid across both queues distinguishes app PID reuse;
	 * the creator's independent identity continues to authorize HAL calls.
	 */
	target = find_get_pid(tgid);
	ret = adpf_process_alive(target) ? 0 : -ESRCH;
	if (ret) {
		put_pid(target);
		put_pid(owner);
		adpf_abort_event(event);
		goto out;
	}
	adpf_put_thread_refs(&event->snapshot.session);
	memset(&event->snapshot, 0, sizeof(event->snapshot));
	next = &event->snapshot.session;
	for (i = 0; i < ADPF_MAX_THREAD; i++)
		next->workDuration[i] = &event->snapshot.duration[i];
	next->sid = sid;
	next->cmd = ADPF_CREATE_HINT_SESSION;
	next->tgid = pid_nr(target);
	next->uid = uid;
	next->threadIds_size = threadIds_size;
	next->durationNanos = durationNanos;
	next->used = SESSION_USED;
	ret = adpf_capture_threads(next, target, threadIds, threadIds_size);
	if (ret) {
		put_pid(target);
		put_pid(owner);
		adpf_abort_event(event);
		goto out;
	}
	adpf_owners[sid] = owner;
	adpf_targets[sid] = target;
	adpf_commit_event(event);
	queue_delayed_work(system_wq, &adpf_reaper, HZ);
	ret = 0;
out:
	mutex_unlock(&adpf_mutex);
	if (!ret) {
		_adpf_systrace(tgid, ADPF_CREATE_HINT_SESSION, "adpf_status");
		_adpf_systrace(tgid, durationNanos, "adpf_target");
	}
	return ret;
}

int adpf_get_hint_session_preferred_rate(long long *preferredRate)
{
	/* This ABI is implemented in the native layer; never report an
	 * uninitialized output as successful kernel telemetry.
	 */
	return preferredRate ? -EOPNOTSUPP : -EINVAL;
}

int adpf_update_work_duaration(unsigned int sid, long targetDurationNanos)
{
	struct adpf_event *event;
	int ret = 0;

	if (targetDurationNanos <= 0)
		return -EINVAL;
	mutex_lock(&adpf_mutex);
	event = adpf_prepare_owned_event(ADPF_UPDATE_TARGET_WORK_DURATION, sid);
	if (IS_ERR(event)) {
		ret = PTR_ERR(event);
		goto out;
	}
	event->snapshot.session.targetDurationNanos = targetDurationNanos;
	adpf_commit_event(event);
out:
	mutex_unlock(&adpf_mutex);
	return ret;
}

int adpf_report_actual_work_duaration(unsigned int sid,
		struct _ADPF_WORK_DURATION *workDuration, int work_duration_size)
{
	struct adpf_event *event;
	int i, ret = 0;

	if (!workDuration || work_duration_size <= 0 || work_duration_size > ADPF_MAX_THREAD)
		return -EINVAL;
	for (i = 0; i < work_duration_size; i++)
		if (workDuration[i].timeStampNanos < 0 || workDuration[i].durationNanos < 0)
			return -EINVAL;
	mutex_lock(&adpf_mutex);
	event = adpf_prepare_owned_event(ADPF_REPORT_ACTUAL_WORK_DURATION, sid);
	if (IS_ERR(event)) {
		ret = PTR_ERR(event);
		goto out;
	}
	for (i = 0; i < work_duration_size; i++) {
		event->snapshot.duration[i].timeStampNanos = workDuration[i].timeStampNanos;
		event->snapshot.duration[i].durationNanos = workDuration[i].durationNanos;
	}
	event->snapshot.session.work_duration_size = work_duration_size;
	adpf_commit_event(event);
out:
	mutex_unlock(&adpf_mutex);
	return ret;
}

static int adpf_simple_event(unsigned int sid, unsigned int cmd)
{
	struct adpf_event *event;
	int ret = 0;

	mutex_lock(&adpf_mutex);
	event = adpf_prepare_owned_event(cmd, sid);
	if (IS_ERR(event))
		ret = PTR_ERR(event);
	else
		adpf_commit_event(event);
	mutex_unlock(&adpf_mutex);
	return ret;
}

int adpf_pause(unsigned int sid)
{
	return adpf_simple_event(sid, ADPF_PAUSE);
}

int adpf_resume(unsigned int sid)
{
	return adpf_simple_event(sid, ADPF_RESUME);
}

int adpf_close(unsigned int sid)
{
	int ret;

	mutex_lock(&adpf_mutex);
	ret = adpf_check_owner(sid);
	if (!ret)
		ret = adpf_close_locked(sid);
	mutex_unlock(&adpf_mutex);
	return ret;
}

int adpf_sent_hint(unsigned int sid, int hint)
{
	struct adpf_event *event;
	int ret = 0;

	if (hint < CPU_LOAD_UP || hint > POWER_EFFICIENCY)
		return -EINVAL;
	mutex_lock(&adpf_mutex);
	event = adpf_prepare_owned_event(ADPF_SENT_HINT, sid);
	if (IS_ERR(event)) {
		ret = PTR_ERR(event);
		goto out;
	}
	event->snapshot.session.hint = hint;
	adpf_commit_event(event);
out:
	mutex_unlock(&adpf_mutex);
	return ret;
}

int adpf_set_threads(unsigned int sid, int *threadIds, int threadIds_size)
{
	struct adpf_event *event;
	int i, ret = 0;

	if (!threadIds || threadIds_size <= 0 || threadIds_size > ADPF_MAX_THREAD)
		return -EINVAL;
	for (i = 0; i < threadIds_size; i++)
		if (threadIds[i] <= 0)
			return -EINVAL;
	mutex_lock(&adpf_mutex);
	event = adpf_prepare_owned_event(ADPF_SET_THREADS, sid);
	if (IS_ERR(event)) {
		ret = PTR_ERR(event);
		goto out;
	}
	ret = adpf_capture_threads(&event->snapshot.session, adpf_targets[sid],
		threadIds, threadIds_size);
	if (ret) {
		adpf_abort_event(event);
		goto out;
	}
	adpf_commit_event(event);
out:
	mutex_unlock(&adpf_mutex);
	return ret;
}

int dsu_sport_mode(unsigned int mode)
{
	pr_debug("[%s] mode: %d",  __func__, mode);
#if IS_REACHABLE(CONFIG_MEDIATEK_CPUFREQ_DEBUG_LITE)
	return cpufreq_set_cci_mode(mode);
#else
	/* Hardware CCI debug mode has no generic-GKI equivalent. */
	return -EOPNOTSUPP;
#endif
}

static void powerhal_close_boost_readers(void)
{
	mutex_lock(&boost_lock);
	WRITE_ONCE(boost_readers_closing, true);
	mutex_unlock(&boost_lock);
	wake_up_all(&boost_queue);
}

static void powerhal_unpublish_callbacks(void)
{
	WRITE_ONCE(powerhal_adpf_create_session_hint_fp, NULL);
	WRITE_ONCE(powerhal_adpf_get_hint_session_preferred_rate_fp, NULL);
	WRITE_ONCE(powerhal_adpf_update_work_duration_fp, NULL);
	WRITE_ONCE(powerhal_adpf_report_actual_work_duration_fp, NULL);
	WRITE_ONCE(powerhal_adpf_pause_fp, NULL);
	WRITE_ONCE(powerhal_adpf_resume_fp, NULL);
	WRITE_ONCE(powerhal_adpf_close_fp, NULL);
	WRITE_ONCE(powerhal_adpf_sent_hint_fp, NULL);
	WRITE_ONCE(powerhal_adpf_set_threads_fp, NULL);
	WRITE_ONCE(boost_get_cmd_fp, NULL);
	WRITE_ONCE(powerhal_dsu_sport_mode_fp, NULL);
}

static void powerhal_destroy_events(void)
{
	struct workqueue_struct *events;
	int i;

	/* Seal admission before joining the reaper, without holding its mutex.
	 * This also prevents it from rearming itself after cancellation.
	 */
	mutex_lock(&adpf_mutex);
	adpf_stopping = true;
	WRITE_ONCE(adpf_enable, 0);
	mutex_unlock(&adpf_mutex);
	cancel_delayed_work_sync(&adpf_reaper);
	mutex_lock(&adpf_mutex);
	events = adpf_events;
	adpf_events = NULL;
	for (i = 0; i < ADPF_MAX_CALLBACK; i++)
		WRITE_ONCE(adpfCallbackList[i], NULL);
	mutex_unlock(&adpf_mutex);
	/* Never wait while holding the mutex callbacks may need. Registered
	 * kernel clients import our API and pin this module; their own teardown
	 * must unregister/release consumer-owned requests before provider exit.
	 */
	synchronize_srcu(&adpf_callback_srcu);
	if (events)
		destroy_workqueue(events);
	WARN_ON(atomic_read(&adpf_pending));
	mutex_lock(&adpf_mutex);
	for (i = 0; i < ADPF_MAX_SESSION; i++) {
		if (sessionList[i])
			adpf_put_thread_refs(sessionList[i]);
		put_pid(adpf_owners[i]);
		adpf_owners[i] = NULL;
		put_pid(adpf_targets[i]);
		adpf_targets[i] = NULL;
	}
	memset(sessionList, 0, sizeof(sessionList));
	memset(adpf_sessions, 0, sizeof(adpf_sessions));
	memset(adpf_durations, 0, sizeof(adpf_durations));
	mutex_unlock(&adpf_mutex);
}

static void __exit powerhal_cpu_ctrl_exit(void)
{
	struct k_list *cmd, *next;

	powerhal_ioctl_provider_revoke();
	proc_remove(powerhal_proc_root);
	powerhal_proc_root = NULL;
	powerhal_destroy_events();
	mutex_lock(&boost_lock);
	list_for_each_entry_safe(cmd, next, &head, queue_list) {
		list_del(&cmd->queue_list);
		kfree(cmd);
	}
	WRITE_ONCE(condition_get_cmd, 0);
	mutex_unlock(&boost_lock);
	/* Unregisters policy notifier, removes every owned QoS request and
	 * drops policy references only after all proc/ioctl users are gone.
	 */
	powerhal_policy_exit();
}

static int __init powerhal_cpu_ctrl_init(void)
{
	int i, j, ret;
	struct pentry {
		const char *name;
		const struct proc_ops *fops;
	};
	const struct pentry entries[] = {
		PROC_ENTRY(sf_hint_low_power_enabled),
		PROC_ENTRY(perfserv_freq),
		PROC_ENTRY(adpf_enable),
	};

	ret = powerhal_policy_init();
	if (ret)
		return ret;

	/* Session backing storage is bounded and cannot partially allocate. */
	for (i = 0; i < ADPF_MAX_SESSION; i++) {
		sessionList[i] = &adpf_sessions[i];
		for (j = 0; j < ADPF_MAX_THREAD; j++)
			sessionList[i]->workDuration[j] = &adpf_durations[i][j];
	}
	adpf_events = alloc_ordered_workqueue("powerhal_adpf", WQ_MEM_RECLAIM);
	if (!adpf_events) {
		ret = -ENOMEM;
		goto fail_policy;
	}
	mutex_lock(&adpf_mutex);
	adpf_stopping = false;
	mutex_unlock(&adpf_mutex);
	/* Expose proc operations only after their policy/session backing exists. */
	powerhal_proc_root = proc_mkdir("powerhal_cpu_ctrl", NULL);
	if (!powerhal_proc_root) {
		ret = -ENOMEM;
		goto fail_events;
	}
	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		if (!proc_create(entries[i].name, 0644, powerhal_proc_root, entries[i].fops)) {
			ret = -ENOMEM;
			goto fail_proc;
		}
	}
	ret = powerhal_ioctl_provider_bind(powerhal_close_boost_readers,
		powerhal_unpublish_callbacks);
	if (ret)
		goto fail_proc;

	powerhal_adpf_create_session_hint_fp = adpf_create_session_hint;
	powerhal_adpf_get_hint_session_preferred_rate_fp = adpf_get_hint_session_preferred_rate;
	powerhal_adpf_update_work_duration_fp = adpf_update_work_duaration;
	powerhal_adpf_report_actual_work_duration_fp = adpf_report_actual_work_duaration;
	powerhal_adpf_pause_fp = adpf_pause;
	powerhal_adpf_resume_fp = adpf_resume;
	powerhal_adpf_close_fp = adpf_close;
	powerhal_adpf_sent_hint_fp = adpf_sent_hint;
	powerhal_adpf_set_threads_fp = adpf_set_threads;
	boost_get_cmd_fp = _boost_get_cmd;

	sf_hint_low_power_enabled = 0;

	// DSU
	powerhal_dsu_sport_mode_fp = dsu_sport_mode;
	mutex_lock(&boost_lock);
	WRITE_ONCE(boost_readers_closing, false);
	mutex_unlock(&boost_lock);
	powerhal_ioctl_provider_publish();

	return 0;
fail_proc:
	proc_remove(powerhal_proc_root);
	powerhal_proc_root = NULL;
fail_events:
	powerhal_close_boost_readers();
	powerhal_destroy_events();
fail_policy:
	powerhal_policy_exit();
	return ret;
}

module_init(powerhal_cpu_ctrl_init);
module_exit(powerhal_cpu_ctrl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek POWERHAL_CPU_CTRL");
MODULE_AUTHOR("MediaTek Inc.");
