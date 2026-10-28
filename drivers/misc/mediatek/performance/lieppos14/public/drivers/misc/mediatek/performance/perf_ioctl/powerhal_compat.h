/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LIEPPOS_POWERHAL_COMPAT_H
#define LIEPPOS_POWERHAL_COMPAT_H
#include <linux/compat.h>
/* AAPCS32 layout of the original pointer/long-based MediaTek userspace ABI.
 * Do not copy a native 64-bit package onto a compat userspace pointer.
 */
struct powerhal_compat_adpf {
	__u32 cmd, sid, tgid, uid;
	compat_uptr_t threads;
	__s32 thread_count;
	compat_uptr_t durations;
	__s32 duration_count;
	union {
		__s32 hint;
		__s64 rate;
		__s64 target;
	} value __aligned(8);
};
struct powerhal_compat_duration {
	compat_long_t timestamp;
	compat_long_t duration;
};
#define POWERHAL_COMPAT_SET _IOW('g', 1, struct powerhal_compat_adpf)
#define POWERHAL_COMPAT_GET _IOW('g', 2, struct powerhal_compat_adpf)
static_assert(sizeof(struct powerhal_compat_adpf) == 40, "AAPCS32 ADPF package layout");
static_assert(sizeof(struct powerhal_compat_duration) == 8, "AAPCS32 work-duration layout");
#endif
