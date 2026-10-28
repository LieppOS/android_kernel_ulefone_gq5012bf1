// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 MediaTek Inc.
 * LieppOS: bounded ADPF dispatch, real provider error propagation and proc lifetime.
 */
#include "ioctl_powerhal.h"
#include "../powerhal_cpu_ctrl/powerhal_cpu_ctrl.h"
#include "powerhal_compat.h"
#include <linux/sched/signal.h>
#include <linux/mutex.h>
#include <linux/srcu.h>
#include "powerhal_lifetime.h"

DEFINE_STATIC_SRCU(powerhal_provider_srcu);
static DEFINE_MUTEX(powerhal_provider_lock);
static bool powerhal_provider_bound;
static bool powerhal_provider_live;
static void (*powerhal_stop_readers)(void);
static void (*powerhal_clear_slots)(void);

int powerhal_ioctl_provider_bind(void (*stop_readers)(void), void (*clear_slots)(void))
{
	int ret = 0;

	if (!stop_readers || !clear_slots)
		return -EINVAL;
	mutex_lock(&powerhal_provider_lock);
	if (powerhal_provider_bound) {
		ret = -EBUSY;
	} else {
		powerhal_stop_readers = stop_readers;
		powerhal_clear_slots = clear_slots;
		powerhal_provider_bound = true;
	}
	mutex_unlock(&powerhal_provider_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(powerhal_ioctl_provider_bind);

void powerhal_ioctl_provider_publish(void)
{
	mutex_lock(&powerhal_provider_lock);
	if (!WARN_ON(!powerhal_provider_bound))
		smp_store_release(&powerhal_provider_live, true);
	mutex_unlock(&powerhal_provider_lock);
}
EXPORT_SYMBOL_GPL(powerhal_ioctl_provider_publish);

void powerhal_ioctl_provider_revoke(void)
{
	mutex_lock(&powerhal_provider_lock);
	if (!powerhal_provider_bound)
		goto out;
	smp_store_release(&powerhal_provider_live, false);
	/* NOTIFY_BOOST can sleep indefinitely holding a read lease. */
	powerhal_stop_readers();
	synchronize_srcu(&powerhal_provider_srcu);
	powerhal_clear_slots();
	powerhal_stop_readers = NULL;
	powerhal_clear_slots = NULL;
	powerhal_provider_bound = false;
out:
	mutex_unlock(&powerhal_provider_lock);
}
EXPORT_SYMBOL_GPL(powerhal_ioctl_provider_revoke);

static long powerhal_provider_call(
	long (*handler)(struct file *, unsigned int, unsigned long),
	struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;
	int index = srcu_read_lock(&powerhal_provider_srcu);

	if (!smp_load_acquire(&powerhal_provider_live)) {
		ret = -ESHUTDOWN;
		goto out;
	}
	ret = handler(file, cmd, arg);
	if (!smp_load_acquire(&powerhal_provider_live))
		ret = -ESHUTDOWN;
out:
	srcu_read_unlock(&powerhal_provider_srcu, index);
	return ret;
}

int (*powerhal_adpf_create_session_hint_fp)(unsigned int, unsigned int,
	unsigned int, int *, int, long);
EXPORT_SYMBOL_GPL(powerhal_adpf_create_session_hint_fp);
int (*powerhal_adpf_get_hint_session_preferred_rate_fp)(long long *);
EXPORT_SYMBOL_GPL(powerhal_adpf_get_hint_session_preferred_rate_fp);
int (*powerhal_adpf_update_work_duration_fp)(unsigned int, long);
EXPORT_SYMBOL_GPL(powerhal_adpf_update_work_duration_fp);
int (*powerhal_adpf_report_actual_work_duration_fp)(unsigned int,
	struct _ADPF_WORK_DURATION *, int);
EXPORT_SYMBOL_GPL(powerhal_adpf_report_actual_work_duration_fp);
int (*powerhal_adpf_pause_fp)(unsigned int);
EXPORT_SYMBOL_GPL(powerhal_adpf_pause_fp);
int (*powerhal_adpf_resume_fp)(unsigned int);
EXPORT_SYMBOL_GPL(powerhal_adpf_resume_fp);
int (*powerhal_adpf_close_fp)(unsigned int);
EXPORT_SYMBOL_GPL(powerhal_adpf_close_fp);
int (*powerhal_adpf_sent_hint_fp)(unsigned int, int);
EXPORT_SYMBOL_GPL(powerhal_adpf_sent_hint_fp);
int (*powerhal_adpf_set_threads_fp)(unsigned int, int *, int);
EXPORT_SYMBOL_GPL(powerhal_adpf_set_threads_fp);
void (*boost_get_cmd_fp)(int *, int *);
EXPORT_SYMBOL_GPL(boost_get_cmd_fp);
int (*powerhal_dsu_sport_mode_fp)(unsigned int);
EXPORT_SYMBOL_GPL(powerhal_dsu_sport_mode_fp);

static struct proc_dir_entry *perfmgr_root;

/* Provider slots are stable throughout the enclosing SRCU lease. Read once,
 * preserve KCFI types, and never turn an absent/failed provider into success.
 */
#define POWERHAL_CALL(slot, ...) do { \
	typeof(slot) callback = READ_ONCE(slot); \
	ret = callback ? callback(__VA_ARGS__) : -EAGAIN; \
} while (0)

static long adpf_ioctl_common(unsigned int cmd, void __user *user, bool compat)
{
	struct _ADPF_PACKAGE msg = {};
	struct powerhal_compat_adpf cmsg;
	int *threads = NULL;
	struct _ADPF_WORK_DURATION *duration = NULL;
	int ret;

	if (compat) {
		if (cmd != POWERHAL_COMPAT_SET && cmd != POWERHAL_COMPAT_GET)
			return -ENOTTY;
		if (copy_from_user(&cmsg, user, sizeof(cmsg)))
			return -EFAULT;
		msg.cmd = cmsg.cmd;
		msg.sid = cmsg.sid;
		msg.tgid = cmsg.tgid;
		msg.uid = cmsg.uid;
		msg.threadIds = compat_ptr(cmsg.threads);
		msg.threadIds_size = cmsg.thread_count;
		msg.workDuration = compat_ptr(cmsg.durations);
		msg.work_duration_size = cmsg.duration_count;
		memcpy(&msg.durationNanos, &cmsg.value, sizeof(cmsg.value));
		cmd = cmd == POWERHAL_COMPAT_SET ? POWERHAL_SET_ADPF_DATA : POWERHAL_GET_ADPF_DATA;
	} else {
		if (cmd != POWERHAL_SET_ADPF_DATA && cmd != POWERHAL_GET_ADPF_DATA)
			return -ENOTTY;
		if (copy_from_user(&msg, user, sizeof(msg)))
			return -EFAULT;
	}
	if (cmd == POWERHAL_GET_ADPF_DATA) {
		if (msg.cmd != ADPF_GET_HINT_SESSION_PREFERED_RATE)
			return -EINVAL;
		msg.preferredRate = 0;
		POWERHAL_CALL(powerhal_adpf_get_hint_session_preferred_rate_fp, &msg.preferredRate);
		if (ret)
			return ret;
		if (compat) {
			cmsg.value.rate = msg.preferredRate;
			return copy_to_user(user, &cmsg, sizeof(cmsg)) ? -EFAULT : 0;
		}
		return copy_to_user(user, &msg, sizeof(msg)) ? -EFAULT : 0;
	}

	switch (msg.cmd) {
	case ADPF_CREATE_HINT_SESSION:
	case ADPF_SET_THREADS:
		if (msg.threadIds_size <= 0 || msg.threadIds_size > ADPF_MAX_THREAD)
			return -EINVAL;
		threads = memdup_user(msg.threadIds, array_size(msg.threadIds_size, sizeof(*threads)));
		if (IS_ERR(threads))
			return PTR_ERR(threads);
		if (msg.cmd == ADPF_CREATE_HINT_SESSION)
			POWERHAL_CALL(powerhal_adpf_create_session_hint_fp, msg.sid, msg.tgid,
				msg.uid, threads, msg.threadIds_size, msg.durationNanos);
		else
			POWERHAL_CALL(powerhal_adpf_set_threads_fp, msg.sid, threads, msg.threadIds_size);
		kfree(threads);
		return ret;
	case ADPF_REPORT_ACTUAL_WORK_DURATION:
		if (msg.work_duration_size <= 0 || msg.work_duration_size > ADPF_MAX_THREAD)
			return -EINVAL;
		if (compat) {
			struct powerhal_compat_duration *cd;
			int i;

			cd = memdup_user(msg.workDuration,
				array_size(msg.work_duration_size, sizeof(*cd)));
			if (IS_ERR(cd))
				return PTR_ERR(cd);
			duration = kcalloc(msg.work_duration_size, sizeof(*duration), GFP_KERNEL);
			if (!duration) {
				kfree(cd);
				return -ENOMEM;
			}
			for (i = 0; i < msg.work_duration_size; i++) {
				duration[i].timeStampNanos = cd[i].timestamp;
				duration[i].durationNanos = cd[i].duration;
			}
			kfree(cd);
		} else {
			duration = memdup_user(msg.workDuration,
				array_size(msg.work_duration_size, sizeof(*duration)));
			if (IS_ERR(duration))
				return PTR_ERR(duration);
		}
		POWERHAL_CALL(powerhal_adpf_report_actual_work_duration_fp, msg.sid,
			duration, msg.work_duration_size);
		kfree(duration);
		return ret;
	case ADPF_UPDATE_TARGET_WORK_DURATION:
		POWERHAL_CALL(powerhal_adpf_update_work_duration_fp, msg.sid, msg.targetDurationNanos);
		break;
	case ADPF_PAUSE:
		POWERHAL_CALL(powerhal_adpf_pause_fp, msg.sid);
		break;
	case ADPF_RESUME:
		POWERHAL_CALL(powerhal_adpf_resume_fp, msg.sid);
		break;
	case ADPF_CLOSE:
		POWERHAL_CALL(powerhal_adpf_close_fp, msg.sid);
		break;
	case ADPF_SENT_HINT:
		POWERHAL_CALL(powerhal_adpf_sent_hint_fp, msg.sid, msg.hint);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static long adpf_device_ioctl_unleased(struct file *file, unsigned int cmd, unsigned long arg)
{
	return adpf_ioctl_common(cmd, (void __user *)arg, false);
}

static long adpf_device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return powerhal_provider_call(adpf_device_ioctl_unleased, file, cmd, arg);
}

#ifdef CONFIG_COMPAT
static long adpf_compat_ioctl_unleased(struct file *file, unsigned int cmd, unsigned long arg)
{
	return adpf_ioctl_common(cmd, compat_ptr(arg), true);
}

static long adpf_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return powerhal_provider_call(adpf_compat_ioctl_unleased, file, cmd, arg);
}
#endif

static long powerhal_device_ioctl_unleased(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *user = (void __user *)arg;
	struct _CPU_CTRL_PACKAGE boost;
	struct _POWERHAL_PACKAGE msg;
	int ret;

	switch (cmd) {
	case NOTIFY_BOOST: {
		typeof(boost_get_cmd_fp) callback = READ_ONCE(boost_get_cmd_fp);

		if (!callback)
			return -EAGAIN;
		memset(&boost, 0, sizeof(boost));
		boost.cmd = boost.value = -1;
		callback(&boost.cmd, &boost.value);
		if (signal_pending(current))
			return -ERESTARTSYS;
		return copy_to_user(user, &boost, sizeof(boost)) ? -EFAULT : 0;
	}
	case DSU_CCI_SPORT_MODE:
		if (copy_from_user(&msg, user, sizeof(msg)))
			return -EFAULT;
		POWERHAL_CALL(powerhal_dsu_sport_mode_fp, msg.value);
		return ret;
	default:
		return -ENOTTY;
	}
}
static long powerhal_device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return powerhal_provider_call(powerhal_device_ioctl_unleased, file, cmd, arg);
}
#undef POWERHAL_CALL

/* These are ioctl endpoints, not seq_file streams. proc_remove performs the
 * procfs operation rundown before code can be unloaded. The endpoints share
 * the original native layout and explicitly translate the AAPCS32 pointer/
 * long fields rather than assuming 32-bit and 64-bit packages match.
 */
static const struct proc_ops adpf_ops = {
	.proc_ioctl = adpf_device_ioctl,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl = adpf_compat_ioctl,
#endif
};
static const struct proc_ops powerhal_ops = {
	.proc_ioctl = powerhal_device_ioctl,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl = powerhal_device_ioctl,
#endif
};

static void __exit powerhal_ioctl_exit(void)
{
	proc_remove(perfmgr_root);
}

static int __init powerhal_ioctl_init(void)
{
	perfmgr_root = proc_mkdir("perfmgr_powerhal", NULL);
	if (!perfmgr_root)
		return -ENOMEM;
	if (!proc_create("ioctl_powerhal_adpf", 0440, perfmgr_root, &adpf_ops) ||
	    !proc_create("ioctl_powerhal", 0440, perfmgr_root, &powerhal_ops)) {
		proc_remove(perfmgr_root);
		perfmgr_root = NULL;
		return -ENOMEM;
	}
	return 0;
}
module_init(powerhal_ioctl_init);
module_exit(powerhal_ioctl_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek PowerHAL ioctl (bounded LieppOS integration)");
MODULE_AUTHOR("MediaTek Inc.; LieppOS contributors");
