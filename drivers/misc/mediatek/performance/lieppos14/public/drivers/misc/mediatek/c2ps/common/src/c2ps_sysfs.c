// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 MediaTek Inc. */
#include "../include/c2ps_sysfs.h"
#include "../include/c2ps_common.h"

static struct kobject *c2ps_kobj;

int c2ps_sysfs_create_dir(struct kobject *parent, const char *name,
	struct kobject **out)
{
	if (!name || !out)
		return -EINVAL;
	*out = NULL;
	parent = parent ? parent : c2ps_kobj;
	if (!parent)
		return -ENODEV;
	*out = kobject_create_and_add(name, parent);
	return *out ? 0 : -ENOMEM;
}

void c2ps_sysfs_remove_dir(struct kobject **out)
{
	if (out && *out) {
		kobject_put(*out);
		*out = NULL;
	}
}

int c2ps_sysfs_create_file(struct kobject *parent, struct kobj_attribute *attr)
{
	parent = parent ? parent : c2ps_kobj;
	if (!parent || !attr)
		return -EINVAL;
	return sysfs_create_file(parent, &attr->attr);
}

void c2ps_sysfs_remove_file(struct kobject *parent, struct kobj_attribute *attr)
{
	parent = parent ? parent : c2ps_kobj;
	if (parent && attr)
		sysfs_remove_file(parent, &attr->attr);
}

int c2ps_sysfs_init(void)
{
	c2ps_kobj = kobject_create_and_add("c2ps", kernel_kobj);
	return c2ps_kobj ? 0 : -ENOMEM;
}

void c2ps_sysfs_exit(void)
{
	c2ps_sysfs_remove_dir(&c2ps_kobj);
}
