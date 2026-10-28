// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 MediaTek Inc. */
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include "../include/fpsgo_sysfs.h"

/* Register names during construction, but never let users enter partially
 * initialized governors. kernfs active protection owns each wrapper's lifetime;
 * the rwsem is the framework-wide commit/quiesce barrier, not a data lock.
 */
struct fpsgo_sysfs_file {
	struct list_head link;
	struct kobject *parent;
	struct kobj_attribute attr;
	struct kobj_attribute *original;
};
struct fpsgo_sysfs_dir {
	struct list_head link;
	struct kobject *kobj;
};
static LIST_HEAD(files);
static LIST_HEAD(dirs);
static DEFINE_MUTEX(registry_lock);
static DECLARE_RWSEM(access_lock);
static struct kobject *fpsgo_kobj;
static bool ready;
/* Protected by registry_lock. Freeze the constructor registry before the
 * final, non-removable native hook is registered; publication cannot fail.
 */
static bool sealed;
static int construction_error;

static void remember_error(int error)
{
	if (error)
		cmpxchg(&construction_error, 0, error);
}

static ssize_t guarded_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct fpsgo_sysfs_file *file = container_of(attr, struct fpsgo_sysfs_file, attr);
	ssize_t ret = -EAGAIN;

	down_read(&access_lock);
	if (ready)
		ret = file->original->show(kobj, file->original, buf);
	up_read(&access_lock);
	return ret;
}

static ssize_t guarded_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct fpsgo_sysfs_file *file = container_of(attr, struct fpsgo_sysfs_file, attr);
	ssize_t ret = -EAGAIN;

	down_read(&access_lock);
	if (ready)
		ret = file->original->store(kobj, file->original, buf, count);
	up_read(&access_lock);
	return ret;
}

int fpsgo_sysfs_error(void)
{
	return READ_ONCE(construction_error);
}

int fpsgo_sysfs_prepare_commit(void)
{
	int ret;

	mutex_lock(&registry_lock);
	ret = READ_ONCE(construction_error);
	if (!ret)
		sealed = true;
	mutex_unlock(&registry_lock);
	return ret;
}

void fpsgo_sysfs_publish(void)
{
	/* Called only after prepare_commit and final native registration. No
	 * allocation, kernfs registration or failure is allowed in this step.
	 */
	down_write(&access_lock);
	ready = true;
	up_write(&access_lock);
}

void fpsgo_sysfs_quiesce(void)
{
	/* Wait for every original show/store method, including a sleeping one. */
	down_write(&access_lock);
	ready = false;
	up_write(&access_lock);
}

int fpsgo_sysfs_create_dir(struct kobject *parent, const char *name, struct kobject **out)
{
	struct fpsgo_sysfs_dir *dir;
	int ret = -ENOMEM;

	if (!name || !out) {
		ret = -EINVAL;
		goto fail;
	}
	*out = NULL;
	mutex_lock(&registry_lock);
	if (sealed) {
		ret = -EBUSY;
		goto unlock;
	}
	parent = parent ? parent : fpsgo_kobj;
	if (!parent) {
		ret = -ENODEV;
		goto unlock;
	}
	dir = kzalloc(sizeof(*dir), GFP_KERNEL);
	if (!dir)
		goto unlock;
	dir->kobj = kobject_create_and_add(name, parent);
	if (!dir->kobj) {
		kfree(dir);
		goto unlock;
	}
	list_add_tail(&dir->link, &dirs);
	*out = dir->kobj;
	mutex_unlock(&registry_lock);
	return 0;
unlock:
	remember_error(ret);
	mutex_unlock(&registry_lock);
	return ret;
fail:
	remember_error(ret);
	return ret;
}

void fpsgo_sysfs_create_file(struct kobject *parent, struct kobj_attribute *attr)
{
	struct fpsgo_sysfs_file *file;
	int ret = -ENOMEM;

	mutex_lock(&registry_lock);
	if (sealed) {
		ret = -EBUSY;
		goto fail;
	}
	parent = parent ? parent : fpsgo_kobj;
	if (!parent || !attr) {
		ret = -EINVAL;
		goto fail;
	}
	file = kzalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		goto fail;
	file->parent = parent;
	file->original = attr;
	file->attr = *attr;
	sysfs_attr_init(&file->attr.attr);
	file->attr.show = attr->show ? guarded_show : NULL;
	file->attr.store = attr->store ? guarded_store : NULL;
	ret = sysfs_create_file(parent, &file->attr.attr);
	if (ret) {
		kfree(file);
		goto fail;
	}
	list_add_tail(&file->link, &files);
	mutex_unlock(&registry_lock);
	return;
fail:
	remember_error(ret);
	mutex_unlock(&registry_lock);
}

void fpsgo_sysfs_remove_file(struct kobject *parent, struct kobj_attribute *attr)
{
	struct fpsgo_sysfs_file *file, *found = NULL;

	parent = parent ? parent : fpsgo_kobj;
	mutex_lock(&registry_lock);
	list_for_each_entry(file, &files, link) {
		if (file->parent == parent && file->original == attr) {
			list_del(&file->link);
			found = file;
			break;
		}
	}
	mutex_unlock(&registry_lock);
	if (found) {
		/* Never hold registry_lock while kernfs drains active methods. */
		sysfs_remove_file(parent, &found->attr.attr);
		kfree(found);
	}
}

void fpsgo_sysfs_remove_dir(struct kobject **out)
{
	struct fpsgo_sysfs_dir *dir, *tmp;
	struct fpsgo_sysfs_file *file;
	struct kobj_attribute *attr;
	struct kobject *kobj;

	if (!out || !*out)
		return;
	kobj = *out;
	/* Remove leftover attributes too; no static callback may outlive code. */
	for (;;) {
		attr = NULL;
		mutex_lock(&registry_lock);
		list_for_each_entry(file, &files, link)
			if (file->parent == kobj) {
				attr = file->original;
				break;
			}
		mutex_unlock(&registry_lock);
		if (!attr)
			break;
		fpsgo_sysfs_remove_file(kobj, attr);
	}
	mutex_lock(&registry_lock);
	list_for_each_entry_safe(dir, tmp, &dirs, link)
		if (dir->kobj == kobj) {
			list_del(&dir->link);
			kfree(dir);
			break;
		}
	mutex_unlock(&registry_lock);
	kobject_put(kobj);
	*out = NULL;
}

int fpsgo_sysfs_init(void)
{
	construction_error = 0;
	sealed = false;
	ready = false;
	fpsgo_kobj = kobject_create_and_add("fpsgo", kernel_kobj);
	return fpsgo_kobj ? 0 : -ENOMEM;
}

void fpsgo_sysfs_exit(void)
{
	struct fpsgo_sysfs_dir *dir;
	struct kobject *kobj;

	fpsgo_sysfs_quiesce();
	/* Reverse creation order: children before parents. Also owns rollback of
	 * any directory left by a constructor which failed halfway through.
	 */
	for (;;) {
		mutex_lock(&registry_lock);
		if (list_empty(&dirs)) {
			mutex_unlock(&registry_lock);
			break;
		}
		dir = list_last_entry(&dirs, struct fpsgo_sysfs_dir, link);
		kobj = dir->kobj;
		mutex_unlock(&registry_lock);
		fpsgo_sysfs_remove_dir(&kobj);
	}
	fpsgo_sysfs_remove_dir(&fpsgo_kobj);
}
