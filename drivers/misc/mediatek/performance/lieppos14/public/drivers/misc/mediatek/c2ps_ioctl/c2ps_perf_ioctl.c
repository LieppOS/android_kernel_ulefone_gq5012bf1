// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 MediaTek Inc.
 * Maintained from authenticated NothingOSS MT6878 donor; see ../c2ps/DONOR.json.
 */
#include "c2ps_perf_ioctl.h"
#include <linux/mutex.h>
#include <linux/compat.h>
#include "../c2ps/common/include/c2ps_usedext.h"

int (*c2ps_notify_init_fp)(int, int, int, int);
EXPORT_SYMBOL_GPL(c2ps_notify_init_fp);
int (*c2ps_notify_uninit_fp)(void);
EXPORT_SYMBOL_GPL(c2ps_notify_uninit_fp);
int (*c2ps_notify_add_task_fp)(u32, u32, u32, int, u32, bool, bool, const char *);
EXPORT_SYMBOL_GPL(c2ps_notify_add_task_fp);
int (*c2ps_notify_task_start_fp)(int, int);
EXPORT_SYMBOL_GPL(c2ps_notify_task_start_fp);
int (*c2ps_notify_task_end_fp)(int, int);
EXPORT_SYMBOL_GPL(c2ps_notify_task_end_fp);
int (*c2ps_notify_vsync_fp)(void);
EXPORT_SYMBOL_GPL(c2ps_notify_vsync_fp);
int (*c2ps_notify_camfps_fp)(int);
EXPORT_SYMBOL_GPL(c2ps_notify_camfps_fp);
int (*c2ps_notify_task_scene_change_fp)(int, int);
EXPORT_SYMBOL_GPL(c2ps_notify_task_scene_change_fp);
int (*c2ps_notify_task_single_shot_fp)(int *, int, int, int *, int *, int *, bool);
EXPORT_SYMBOL_GPL(c2ps_notify_task_single_shot_fp);

struct proc_dir_entry *c2ps_ioctl_root;
EXPORT_SYMBOL(c2ps_ioctl_root);
int debug_log_on;
module_param(debug_log_on, int, 0644);
static bool callbacks_ready;
static DEFINE_MUTEX(session_file_lock);
/* Protected by session_file_lock; VFS keeps the activating file alive until
 * device_release, so this is a borrowed identity, never dereferenced.
 */
static struct file *activation_file;

/* The sole provider is a boot-lifetime module: it pins itself before publishing
 * these legacy slots. No provider removal/replacement is supported. This is a
 * release/acquire publication barrier, NOT a claim of removable raw callbacks.
 */
void c2ps_publish_callbacks(void)
{
	smp_store_release(&callbacks_ready, true);
}
EXPORT_SYMBOL_GPL(c2ps_publish_callbacks);

#define C2PS_CALL(slot, ...) ({ \
	typeof(slot) fn = READ_ONCE(slot); \
	fn ? fn(__VA_ARGS__) : -ENODEV; \
})
#define C2PS_COPY(value) do { \
	if (copy_from_user(&(value), argp, sizeof(value))) \
		return -EFAULT; \
} while (0)

static int device_show(struct seq_file *m, void *v) { return 0; }
static int device_open(struct inode *inode, struct file *file)
{
	return single_open(file, device_show, inode->i_private);
}

static long device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	C2PS_PACKAGE pkg;
	C2PS_INIT_PARAM init;
	C2PS_UNINIT_PARAM uninit;
	C2PS_TASK_INIT_PARAMS task;
	C2PS_INFO_NOTIFY info;
	C2PS_SINGLE_SHOT_PARAM shot;
	long ret;
	int i;

	if (!IS_ENABLED(CONFIG_MTK_C2PS))
		return -EOPNOTSUPP;
	if (!smp_load_acquire(&callbacks_ready))
		return -ENODEV;
	switch (cmd) {
	case C2PS_ACTIVATE:
		C2PS_COPY(init);
		if (init.max_uclamp_cluster0 > 1024 ||
		    init.max_uclamp_cluster1 > 1024 ||
		    init.max_uclamp_cluster2 > 1024)
			return -EINVAL;
		mutex_lock(&session_file_lock);
		ret = C2PS_CALL(c2ps_notify_init_fp, init.camfps,
			init.max_uclamp_cluster0, init.max_uclamp_cluster1,
			init.max_uclamp_cluster2);
		if (!ret)
			activation_file = filp;
		mutex_unlock(&session_file_lock);
		return ret;
	case C2PS_DESTROY:
		C2PS_COPY(uninit);
		mutex_lock(&session_file_lock);
		ret = C2PS_CALL(c2ps_notify_uninit_fp);
		if (!ret)
			activation_file = NULL;
		mutex_unlock(&session_file_lock);
		return ret;
	case C2PS_ADD_TASK:
		C2PS_COPY(task);
		if (task.default_uclamp > 1024)
			return -EINVAL;
		/* Never let a non-terminated fixed-width userspace name escape. */
		task.task_name[sizeof(task.task_name) - 1] = '\0';
		return C2PS_CALL(c2ps_notify_add_task_fp, task.task_id,
			task.task_target_time, task.default_uclamp, task.task_group_head,
			task.task_group_target_time, task.is_vip_task,
			task.is_dynamic_tid, task.task_name);
	case C2PS_TASK_START:
		C2PS_COPY(pkg);
		return C2PS_CALL(c2ps_notify_task_start_fp, pkg.tid, pkg.task_id);
	case C2PS_TASK_END:
		C2PS_COPY(pkg);
		return C2PS_CALL(c2ps_notify_task_end_fp, pkg.tid, pkg.task_id);
	case C2PS_TASK_CHANGE:
		C2PS_COPY(pkg);
		return C2PS_CALL(c2ps_notify_task_scene_change_fp,
			pkg.task_id, pkg.mode_change_hint);
	case C2PS_NOTIFY_VSYNC:
		return C2PS_CALL(c2ps_notify_vsync_fp);
	case C2PS_NOTIFY_CAMFPS:
		C2PS_COPY(info);
		return C2PS_CALL(c2ps_notify_camfps_fp, info.cur_camfps);
	case C2PS_TASK_SINGLE_SHOT:
		C2PS_COPY(shot);
		/* Nonpositive arrays retain the donor's 'no override' meaning. */
		for (i = 0; i < MAX_CPU_NUM; i++)
			if (shot.uclamp_max[i] > 1024 ||
			    shot.uclamp_max_placeholder1[i] > 1024 ||
			    shot.uclamp_max_placeholder2[i] > 1024 ||
			    shot.uclamp_max_placeholder3[i] > 1024)
				return -EINVAL;
		return C2PS_CALL(c2ps_notify_task_single_shot_fp, shot.uclamp_max,
			shot.idle_rate_alert, shot.timeout, shot.uclamp_max_placeholder1,
			shot.uclamp_max_placeholder2, shot.uclamp_max_placeholder3,
			shot.reset_param);
	default:
		return -ENOTTY;
	}
}

static int device_release(struct inode *inode, struct file *file)
{
	mutex_lock(&session_file_lock);
	if (activation_file && activation_file == file) {
		/* Synchronous close drains requests/tasks before the normal owner
		 * descriptor disappears. A foreign TGID cannot acquire this lease
		 * by inheriting an fd; its requests fail and the 5s lease expires.
		 */
		if (smp_load_acquire(&callbacks_ready))
			C2PS_CALL(c2ps_notify_uninit_fp);
		activation_file = NULL;
	}
	mutex_unlock(&session_file_lock);
	return single_release(inode, file);
}

#ifdef CONFIG_COMPAT
static long device_compat_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	/* All UAPI structures contain fixed-width data, not embedded pointers. */
	return device_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct proc_ops Fops = {
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl = device_compat_ioctl,
#endif
	.proc_ioctl = device_ioctl,
	.proc_open = device_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = device_release,
};

static void __exit exit_c2ps_perf_ioctl(void)
{
	/* A published permanent provider holds our ordinary module dependency.
	 * Thus normal removal is possible only before provider publication.
	 * proc_remove drains active methods before module text can disappear.
	 */
	proc_remove(c2ps_ioctl_root);
	c2ps_ioctl_root = NULL;
}

static int __init init_c2ps_perf_ioctl(void)
{
	c2ps_ioctl_root = proc_mkdir("c2ps", NULL);
	if (!c2ps_ioctl_root)
		return -ENOMEM;
	if (!proc_create("c2ps_ioctl", 0660, c2ps_ioctl_root, &Fops)) {
		proc_remove(c2ps_ioctl_root);
		c2ps_ioctl_root = NULL;
		return -ENOMEM;
	}
	return 0;
}
module_init(init_c2ps_perf_ioctl);
module_exit(exit_c2ps_perf_ioctl);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek C2PS c2ps_perf_ioctl");
MODULE_AUTHOR("MediaTek Inc.");
