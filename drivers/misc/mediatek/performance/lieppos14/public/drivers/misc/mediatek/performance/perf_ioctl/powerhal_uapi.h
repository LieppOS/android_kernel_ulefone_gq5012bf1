/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 MediaTek Inc. Original MediaTek wire layouts. */
#ifndef LIEPPOS_POWERHAL_UAPI_H
#define LIEPPOS_POWERHAL_UAPI_H
#include <linux/types.h>
#include <linux/ioctl.h>
enum _ADPF_CMD {
	ADPF_CREATE_HINT_SESSION = 1,
	ADPF_GET_HINT_SESSION_PREFERED_RATE,
	ADPF_UPDATE_TARGET_WORK_DURATION,
	ADPF_REPORT_ACTUAL_WORK_DURATION,
	ADPF_PAUSE, ADPF_RESUME, ADPF_CLOSE, ADPF_SENT_HINT, ADPF_SET_THREADS,
};
struct _ADPF_WORK_DURATION {
	long timeStampNanos;
	long durationNanos;
};
struct _ADPF_PACKAGE {
	__u32 cmd, sid, tgid, uid;
	__s32 *threadIds;
	__s32 threadIds_size;
	struct _ADPF_WORK_DURATION *workDuration;
	__s32 work_duration_size;
	union {
		__s32 hint;
		__s64 preferredRate;
		__s64 durationNanos;
		__s64 targetDurationNanos;
	};
};
struct _POWERHAL_PACKAGE { __u32 value; };
struct _CPU_CTRL_PACKAGE { __s32 cmd, value; };
enum { UNKNOWN = -1, FPSGO_BOOST = 0, CPUFREQ_BOOST = 1 };
#define NOTIFY_BOOST _IOW('g', 1, struct _CPU_CTRL_PACKAGE)
#define POWERHAL_SET_ADPF_DATA _IOW('g', 1, struct _ADPF_PACKAGE)
#define POWERHAL_GET_ADPF_DATA _IOW('g', 2, struct _ADPF_PACKAGE)
#define DSU_CCI_SPORT_MODE _IOW('g', 3, struct _POWERHAL_PACKAGE)
#endif
