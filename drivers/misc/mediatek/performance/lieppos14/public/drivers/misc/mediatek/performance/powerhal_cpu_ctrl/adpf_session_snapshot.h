/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LIEPPOS_ADPF_SESSION_SNAPSHOT_H
#define LIEPPOS_ADPF_SESSION_SNAPSHOT_H
#include "powerhal_cpu_ctrl.h"

/* Embedded storage travels with its event; none of the queued pointers may
 * refer back to a mutable/reused session slot.
 */
struct adpf_session_snapshot {
	struct _SESSION session;
	struct _SESSION_WORK_DURATION duration[ADPF_MAX_THREAD];
};

static inline int adpf_copy_session(struct adpf_session_snapshot *dst,
				    const struct _SESSION *src)
{
	int i;

	if (!dst || !src || src->threadIds_size < 0 ||
	    src->threadIds_size > ADPF_MAX_THREAD || src->work_duration_size < 0 ||
	    src->work_duration_size > ADPF_MAX_THREAD)
		return -EINVAL;
	dst->session = *src;
	for (i = 0; i < ADPF_MAX_THREAD; i++) {
		if (i < src->work_duration_size) {
			if (!src->workDuration[i])
				return -EINVAL;
			dst->duration[i] = *src->workDuration[i];
		} else {
			memset(&dst->duration[i], 0, sizeof(dst->duration[i]));
		}
		dst->session.workDuration[i] = &dst->duration[i];
	}
	return 0;
}
#endif
