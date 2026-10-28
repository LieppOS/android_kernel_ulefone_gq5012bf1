/* SPDX-License-Identifier: GPL-2.0 */
/* Included by xgf.c: the ring pointer, index and readiness have ONE lock.
 * Trace callbacks can hold rq/IRQ locks; never take a mutex or sleep here.
 * The raw lock is held only for a single record copy/publication or detachment.
 * All reference releases are outside it, including overwrite and teardown.
 */
static DEFINE_RAW_SPINLOCK(xgf_trace_lock);
static unsigned long long xgf_trace_sequence;

static struct fpsgo_trace_event *xgf_trace_ring(int buffer, int *count,
	atomic_t **index)
{
	if (buffer == 0) {
		*count = xgf_event_buffer_size;
		*index = &xgf_event_buffer_idx;
		return xgf_event_buffer;
	}
	if (buffer == 1) {
		*count = fstb_event_buffer_size;
		*index = &fstb_event_buffer_idx;
		return fstb_event_buffer;
	}
	*count = 0;
	*index = NULL;
	return NULL;
}

struct pid *fpsgo_xgf_pid_ref_get(struct pid *identity)
{
	return get_pid(identity);
}
EXPORT_SYMBOL(fpsgo_xgf_pid_ref_get);

void fpsgo_xgf_pid_ref_put(struct pid *identity)
{
	put_pid(identity);
}
EXPORT_SYMBOL(fpsgo_xgf_pid_ref_put);

void fpsgo_xgf_trace_put(struct fpsgo_trace_event *snapshot)
{
	if (!snapshot)
		return;
	put_pid(snapshot->pid_ref);
	put_pid(snapshot->note_ref);
	snapshot->pid_ref = NULL;
	snapshot->note_ref = NULL;
}
EXPORT_SYMBOL(fpsgo_xgf_trace_put);

static int xgf_trace_copy(int buffer, int index, unsigned long long expected,
	struct fpsgo_trace_event *out, int identities)
{
	struct fpsgo_trace_event *ring;
	atomic_t *cursor;
	unsigned long flags;
	int count, ret = -ENOENT;

	if (!out)
		return -EINVAL;
	/* Caller must have put any previous owned snapshot first. */
	memset(out, 0, sizeof(*out));
	if (in_nmi())
		return -EWOULDBLOCK;
	raw_spin_lock_irqsave(&xgf_trace_lock, flags);
	if (xgf_ko_ready <= 0)
		goto unlock;
	ring = xgf_trace_ring(buffer, &count, &cursor);
	if (!ring || index < 0 || index >= count)
		goto unlock;
	if (!ring[index].sequence)
		goto unlock;
	if (expected && expected != ring[index].sequence) {
		ret = -ESTALE;
		goto unlock;
	}
	*out = ring[index];
	if (identities) {
		out->pid_ref = get_pid(out->pid_ref);
		out->note_ref = get_pid(out->note_ref);
	} else {
		out->pid_ref = NULL;
		out->note_ref = NULL;
	}
	ret = 0;
unlock:
	raw_spin_unlock_irqrestore(&xgf_trace_lock, flags);
	return ret;
}

int fpsgo_xgf_trace_read(int buffer, int index, struct fpsgo_trace_event *out)
{
	return xgf_trace_copy(buffer, index, 0, out, 0);
}
EXPORT_SYMBOL(fpsgo_xgf_trace_read);

int fpsgo_xgf_trace_get(int buffer, int index,
	unsigned long long expected_sequence, struct fpsgo_trace_event *out)
{
	return xgf_trace_copy(buffer, index, expected_sequence, out, 1);
}
EXPORT_SYMBOL(fpsgo_xgf_trace_get);

static void xgf_trace_publish(int buffer, const struct fpsgo_trace_event *event,
	struct task_struct *pid_task, struct task_struct *note_task)
{
	struct fpsgo_trace_event *ring, old = {0}, next = *event;
	atomic_t *cursor;
	unsigned long flags;
	int count, index;

	/* Normal IRQ/scheduler tracepoints only. An exported synthetic call
	 * from NMI must not recursively acquire a lock held by its victim.
	 */
	if (in_nmi())
		return;
	/* Capture the tracepoint's EXACT task objects, not a later PID lookup.
	 * get_task_pid()/get_pid() are RCU/refcount operations, not allocations.
	 * Unknown synthetic IDs stay unknown. Idle task PID zero has no identity.
	 */
	next.pid_ref = pid_task && event->pid > 0 ?
		get_task_pid(pid_task, PIDTYPE_PID) : NULL;
	next.note_ref = note_task && event->note > 0 ?
		get_task_pid(note_task, PIDTYPE_PID) : NULL;
	raw_spin_lock_irqsave(&xgf_trace_lock, flags);
	if (xgf_ko_ready <= 0)
		goto unlock;
	ring = xgf_trace_ring(buffer, &count, &cursor);
	if (!ring || count <= 0)
		goto unlock;
	index = atomic_read(cursor);
	if (index < 0 || index >= count)
		index = 0;
	old = ring[index];
	/* Zero is reserved for an unwritten slot; reset does not recycle IDs. */
	if (!++xgf_trace_sequence)
		++xgf_trace_sequence;
	next.sequence = xgf_trace_sequence;
	ring[index] = next;
	next.pid_ref = NULL; /* ownership transferred to the slot */
	next.note_ref = NULL;
	/* Publish the next-free cursor AFTER the entire slot is committed. */
	atomic_set(cursor, index + 1 == count ? 0 : index + 1);
unlock:
	raw_spin_unlock_irqrestore(&xgf_trace_lock, flags);
	fpsgo_xgf_trace_put(&old);
	fpsgo_xgf_trace_put(&next); /* failed publication unwind */
}

static void xgf_trace_reset_indices(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&xgf_trace_lock, flags);
	atomic_set(&xgf_event_buffer_idx, 0);
	atomic_set(&fstb_event_buffer_idx, 0);
	raw_spin_unlock_irqrestore(&xgf_trace_lock, flags);
}

static void xgf_trace_disable(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&xgf_trace_lock, flags);
	xgf_ko_ready = -1;
	atomic_set(&xgf_event_buffer_idx, 0);
	atomic_set(&fstb_event_buffer_idx, 0);
	raw_spin_unlock_irqrestore(&xgf_trace_lock, flags);
}

static void xgf_trace_detach(void)
{
	struct fpsgo_trace_event *xgf, *fstb;
	unsigned long flags;
	int nx, nf, i;

	/* The owner stops/synchronizes probes first. Locked detachment also
	 * rejects an already-entered synthetic writer or a snapshot request.
	 * The private arrays remain alive until this synchronous drain returns.
	 */
	raw_spin_lock_irqsave(&xgf_trace_lock, flags);
	xgf_ko_ready = -1;
	xgf = xgf_event_buffer;
	fstb = fstb_event_buffer;
	nx = xgf_event_buffer_size;
	nf = fstb_event_buffer_size;
	xgf_event_buffer = NULL;
	fstb_event_buffer = NULL;
	xgf_event_buffer_size = 0;
	fstb_event_buffer_size = 0;
	atomic_set(&xgf_event_buffer_idx, 0);
	atomic_set(&fstb_event_buffer_idx, 0);
	raw_spin_unlock_irqrestore(&xgf_trace_lock, flags);
	for (i = 0; xgf && i < nx; i++) {
		fpsgo_xgf_trace_put(&xgf[i]);
		xgf[i].sequence = 0;
	}
	for (i = 0; fstb && i < nf; i++) {
		fpsgo_xgf_trace_put(&fstb[i]);
		fstb[i].sequence = 0;
	}
}
