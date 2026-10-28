/* SPDX-License-Identifier: GPL-2.0 */
/* Included by core.c after its scheduler/uclamp primitives. No callbacks into
 * a module, new RVH, task-layout changes or borrowed task pointers. Lock order:
 * task pi_lock -> rq lock -> registry lock. Never acquire rq under registry.
 * The registry lock protects membership only; per-task fields use pi_lock.
 */
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/sched/uclamp_lease.h>

#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
struct sched_uclamp_lease {
	struct hlist_node node;
	struct task_struct *task;
	u32 saved_min, saved_max;
	bool applied;
	bool revoked;
};
static DEFINE_HASHTABLE(sched_uclamp_leases, 6);
static DEFINE_RAW_SPINLOCK(sched_uclamp_lease_lock);

static struct sched_uclamp_lease *uclamp_lease_find(struct task_struct *task)
{
	struct sched_uclamp_lease *lease;

	lockdep_assert_held(&sched_uclamp_lease_lock);
	hash_for_each_possible(sched_uclamp_leases, lease, node,
		(unsigned long)task) {
		if (lease->task == task)
			return lease;
	}
	return NULL;
}

static void uclamp_lease_detach(struct sched_uclamp_lease *lease)
{
	unsigned long flags;

	lockdep_assert_held(&lease->task->pi_lock);
	raw_spin_lock_irqsave(&sched_uclamp_lease_lock, flags);
	if (!lease->revoked) {
		hash_del(&lease->node);
		lease->revoked = true;
	}
	raw_spin_unlock_irqrestore(&sched_uclamp_lease_lock, flags);
}

static void uclamp_lease_restore(struct sched_uclamp_lease *lease)
{
	struct sched_attr attr = {
		.sched_flags = SCHED_FLAG_UTIL_CLAMP,
		.sched_util_min = lease->saved_min,
		.sched_util_max = lease->saved_max,
	};

	/* Reset non-user requests through the actual scheduler's current defaults,
	 * including the RT default. Never copy active rq buckets or stale defaults.
	 * Caller has removed queued accounting, and holds task_rq_lock().
	 */
	__setscheduler_uclamp(lease->task, &attr);
}

/* Ordinary __sched_setscheduler reaches this only after all error/recheck
 * paths and dequeue/put_prev, BEFORE changing policy or requested clamps.
 * Same-value writes revoke ownership. A rejected request must NOT revoke.
 */
static void sched_uclamp_lease_preempt(struct task_struct *task)
{
	struct sched_uclamp_lease *lease;
	unsigned long flags;

	lockdep_assert_held(&task->pi_lock);
	raw_spin_lock_irqsave(&sched_uclamp_lease_lock, flags);
	lease = uclamp_lease_find(task);
	if (lease) {
		hash_del(&lease->node);
		lease->revoked = true;
	}
	raw_spin_unlock_irqrestore(&sched_uclamp_lease_lock, flags);
	/* Client final free also takes pi_lock, so lease remains valid here.
	 * No tracing/vendor callbacks execute under the global registry lock.
	 */
	if (lease && lease->applied)
		uclamp_lease_restore(lease);
}

/* Clamp-only transaction: do not feed zero policy/priority/deadline parameters
 * into sched_setattr_nocheck, and do not recursively acquire scheduler locks.
 * Use the same dequeue/put_prev/enqueue/set_next and balance-callback protocol
 * as the real scheduler. No policy/class/priority/affinity/PI-chain mutation.
 */
static void uclamp_lease_change(struct sched_uclamp_lease *lease,
	struct rq *rq, const struct sched_attr *attr)
{
	struct task_struct *task = lease->task;
	bool queued = task_on_rq_queued(task);
	bool running = task_current(rq, task);
	int flags = DEQUEUE_SAVE | DEQUEUE_NOCLOCK;

	if (queued)
		dequeue_task(rq, task, flags);
	if (running)
		put_prev_task(rq, task);
	if (attr)
		__setscheduler_uclamp(task, attr);
	else
		uclamp_lease_restore(lease);
	if (queued)
		enqueue_task(rq, task, flags);
	if (running)
		set_next_task(rq, task);
}

static void uclamp_lease_unlock(struct rq *rq, struct task_struct *task,
	struct rq_flags *rf)
{
	struct balance_callback *head;

	preempt_disable();
	head = splice_balance_callbacks(rq);
	task_rq_unlock(rq, task, rf);
	balance_callbacks(rq, head);
	preempt_enable();
}

int sched_uclamp_lease_acquire(struct task_struct *task,
	struct sched_uclamp_lease **out)
{
	struct sched_uclamp_lease *lease;
	struct rq_flags rf;
	unsigned long flags;
	struct rq *rq;
	int ret = 0;

	if (!out)
		return -EINVAL;
	*out = NULL;
	if (!task || !pid_alive(task))
		return -ESRCH;
	if (READ_ONCE(task->flags) & PF_KTHREAD)
		return -EPERM;
	lease = kzalloc(sizeof(*lease), GFP_KERNEL);
	if (!lease)
		return -ENOMEM;
	get_task_struct(task);
	lease->task = task;
	/* This is a sleepable API; enable the real rq accounting path outside
	 * all scheduler locks. A failed acquire does not install policy state.
	 */
	static_branch_enable(&sched_uclamp_used);
	rq = task_rq_lock(task, &rf);
	raw_spin_lock_irqsave(&sched_uclamp_lease_lock, flags);
	if (!pid_alive(task))
		ret = -ESRCH;
	else if (task == rq->stop)
		ret = -EPERM;
	else if (uclamp_lease_find(task))
		ret = -EBUSY;
	else
		hash_add(sched_uclamp_leases, &lease->node, (unsigned long)task);
	raw_spin_unlock_irqrestore(&sched_uclamp_lease_lock, flags);
	task_rq_unlock(rq, task, &rf);
	if (ret) {
		put_task_struct(task);
		kfree(lease);
		return ret;
	}
	*out = lease;
	return 0;
}
EXPORT_SYMBOL_GPL(sched_uclamp_lease_acquire);

int sched_uclamp_lease_apply(struct sched_uclamp_lease *lease,
	unsigned int min_util, unsigned int max_util)
{
	struct sched_attr attr = {
		.sched_flags = SCHED_FLAG_UTIL_CLAMP,
		.sched_util_min = min_util,
		.sched_util_max = max_util,
	};
	struct rq_flags rf;
	struct task_struct *task;
	struct rq *rq;
	int ret;

	if (!lease || min_util > max_util || max_util > SCHED_CAPACITY_SCALE)
		return -EINVAL;
	task = lease->task;
	/* Retain vendor validation, outside rq/pi locks as in sched_setattr. */
	ret = uclamp_validate(task, &attr, false);
	if (ret)
		return ret;
	rq = task_rq_lock(task, &rf);
	if (lease->revoked || !pid_alive(task)) {
		ret = -ESTALE;
		goto unlock;
	}
	if (!lease->applied) {
		lease->saved_min = task->uclamp_req[UCLAMP_MIN].user_defined ?
			task->uclamp_req[UCLAMP_MIN].value : U32_MAX;
		lease->saved_max = task->uclamp_req[UCLAMP_MAX].user_defined ?
			task->uclamp_req[UCLAMP_MAX].value : U32_MAX;
		lease->applied = true;
	}
	update_rq_clock(rq);
	uclamp_lease_change(lease, rq, &attr);
	uclamp_lease_unlock(rq, task, &rf);
	return 0;
unlock:
	task_rq_unlock(rq, task, &rf);
	return ret;
}
EXPORT_SYMBOL_GPL(sched_uclamp_lease_apply);

bool sched_uclamp_lease_is_live(struct sched_uclamp_lease *lease)
{
	struct rq_flags rf;
	struct rq *rq;
	bool live;

	if (!lease)
		return false;
	rq = task_rq_lock(lease->task, &rf);
	live = !lease->revoked && pid_alive(lease->task);
	task_rq_unlock(rq, lease->task, &rf);
	return live;
}
EXPORT_SYMBOL_GPL(sched_uclamp_lease_is_live);

void sched_uclamp_lease_release(struct sched_uclamp_lease *lease)
{
	struct rq_flags rf;
	struct task_struct *task;
	struct rq *rq;

	if (!lease)
		return;
	task = lease->task;
	rq = task_rq_lock(task, &rf);
	if (!lease->revoked && lease->applied && pid_alive(task)) {
		update_rq_clock(rq);
		uclamp_lease_change(lease, rq, NULL);
	}
	uclamp_lease_detach(lease);
	uclamp_lease_unlock(rq, task, &rf);
	put_task_struct(task);
	kfree(lease);
}
EXPORT_SYMBOL_GPL(sched_uclamp_lease_release);
#else
/* Internal disabled-config call site only, NOT an exported fake provider. */
static void sched_uclamp_lease_preempt(struct task_struct *task) { }
#endif
