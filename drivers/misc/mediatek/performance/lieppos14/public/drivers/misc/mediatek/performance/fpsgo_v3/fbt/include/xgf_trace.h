/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FPSGO_XGF_TRACE_H_
#define _FPSGO_XGF_TRACE_H_

struct pid;

/* Maintained cohort extension, NOT the stock 40-byte record ABI. The first
 * 40 bytes retain the recovered scalar layout. Rebuild producer and consumers
 * together. Ring slots own pid references, never task_struct or stack pointers.
 */
struct fpsgo_trace_event {
	int event;
	int cpu;
	int note;
	int state;
	int pid;
	unsigned long long ts;
	unsigned long long addr;
	struct pid *pid_ref;
	struct pid *note_ref;
	unsigned long long sequence;
};

/* buffer: 0 = XGF, 1 = FSTB. A successful read is a coherent scalar copy;
 * its identity pointers are NULL and need no put. get() instead acquires both
 * identities under the ring lock; put() exactly once before reusing its output.
 * expected_sequence==0 accepts the current slot; otherwise replacement returns
 * -ESTALE. Empty/stopped/invalid slots fail with a zeroed output. Fake numeric
 * events have NULL identities: consumers MUST NOT resolve them by numeric PID.
 * These APIs do not make a multi-slot scan an atomic frame snapshot.
 */
int fpsgo_xgf_trace_read(int buffer, int index, struct fpsgo_trace_event *out);
int fpsgo_xgf_trace_get(int buffer, int index,
	unsigned long long expected_sequence, struct fpsgo_trace_event *out);
void fpsgo_xgf_trace_put(struct fpsgo_trace_event *snapshot);
/* Duplicate/drop an already owned PID identity for private graph nodes. No
 * numeric lookup, no task pin and no inference that the task is still alive.
 */
struct pid *fpsgo_xgf_pid_ref_get(struct pid *identity);
void fpsgo_xgf_pid_ref_put(struct pid *identity);

/* Scalar-only legacy estimators: no borrowed reference escapes the lock.
 * An unavailable slot is the all-zero, sequence==0 sentinel. Identity-aware
 * dependency construction must use get(), not this compatibility helper.
 */
static inline struct fpsgo_trace_event fpsgo_xgf_trace_sample(int buffer, int index)
{
	struct fpsgo_trace_event sample = {0};

	fpsgo_xgf_trace_read(buffer, index, &sample);
	return sample;
}

#endif
