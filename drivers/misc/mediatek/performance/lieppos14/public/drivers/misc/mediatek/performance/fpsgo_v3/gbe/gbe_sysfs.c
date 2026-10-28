// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 MediaTek Inc. */
#include "gbe_sysfs.h"
#include "../base/include/fpsgo_sysfs.h"

static struct kobject *gbe_kobj;

void gbe_sysfs_create_file(struct kobj_attribute *attr)
{
	if (gbe_kobj)
		fpsgo_sysfs_create_file(gbe_kobj, attr);
}

void gbe_sysfs_remove_file(struct kobj_attribute *attr)
{
	if (gbe_kobj)
		fpsgo_sysfs_remove_file(gbe_kobj, attr);
}

void gbe_sysfs_init(void)
{
	/* Preserve /sys/kernel/gbe, but share the transaction/rundown barrier. */
	fpsgo_sysfs_create_dir(kernel_kobj, "gbe", &gbe_kobj);
}

void gbe_sysfs_exit(void)
{
	fpsgo_sysfs_remove_dir(&gbe_kobj);
}
