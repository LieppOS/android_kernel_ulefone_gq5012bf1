// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek scheduler_ext module.
 *
 * The shipped GQ5012BF1 scheduler_ext.ko contains no code beyond an empty
 * module_init/exit pair; it has no imports, exports, parameters, DT table or
 * scheduler hook.  Reproduce that ABI-neutral identity here.  Scheduler policy
 * belongs to the FPSGO/C2PS provider, which is maintained separately.
 */

#include <linux/module.h>

static int __init scheduler_ext_init(void)
{
	return 0;
}

static void __exit scheduler_ext_exit(void)
{
}

module_init(scheduler_ext_init);
module_exit(scheduler_ext_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek scheduler");
MODULE_AUTHOR("MediaTek Inc.");
