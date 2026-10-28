/*
 * Copyright (c) 2015-2018 TrustKernel Incorporated
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
#include <linux/init.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
#include <linux/notifier.h>
#endif


#define TKCORE_BL

#ifdef TKCORE_BL
#include <linux/kthread.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <asm/topology.h>
#endif

#include <linux/tee_clkmgr.h>
#include <linux/tee_tkcore.h>

#include <linux/tee_kernel_lowlevel_api.h>
#include <arm_common/teesmc.h>

#include "tee_tz_priv.h"
#include "smc_abi_helper.h"

#ifdef TKCORE_BL

static int nr_cpus __read_mostly;

/*
 * if a smc_task waits over 2ms
 * for one available core, we give up
 * and run the smc on the current core
 */
static const s64 tee_task_timeout_us = 2000LL;

struct tee_task {
	struct tee_tz *ptee;
	struct invoke_tee_response *param;
	struct invoke_tee_response *last_param;

	struct list_head entry;
	struct completion done;
};

struct tee_smc_thread {
	struct task_struct *task;
	wait_queue_head_t thread_wq;
};

#endif

struct tee_task_ctl {
	/* guarantee the mutual-exlusiveness of smc */
	struct mutex g_lock;

	/* cmds that wait for
	 * available TEE thread slots
	 */
	atomic_t nr_waiting_cmds;
	struct completion smc_comp;

#ifdef TKCORE_BL
	/* bit n is set while tkcore_smc_task.n is busy */
	uint32_t cpu_mask;
	struct list_head tasks;
	spinlock_t task_lock;

	enum cpuhp_state tkcore_cpuhp_state;

	struct tee_smc_thread *threads;
	int nr_threads;
#endif

	/* statistics information */
	s64 max_smc_time;
	s64 max_task_time;
};

static struct tee_task_ctl tee_task_ctl;

static inline void trace_tee_smc(struct tee_task_ctl *ctl, int rv,
				 s64 time_start, s64 time_end)
{
	s64 duration = time_end - time_start;

	if (duration > 1000000LL) {
		pr_warn("WARNING SMC[0x%x] %sDURATION %lld us\n", rv,
			rv == TEESMC_RPC_FUNC_IRQ ? "IRQ " : "", duration);
	}

	/* we needn't handle concurrency here. */
	if (duration > ctl->max_smc_time)
		ctl->max_smc_time = duration;
}

static inline void trace_tee_smc_done(struct tee_task_ctl *ctl,
				s64 time_start,
				s64 time_end)
{
	s64 duration = time_end - time_start;

	if (duration > ctl->max_task_time)
		ctl->max_task_time = duration;
}

static inline unsigned long rpc_ret_by_cmd_id(unsigned long cmd_id)
{
	return TEESMC_IS_FAST_CALL(cmd_id) ?
		TEE_SMC(FASTCALL_RETURN_FROM_RPC) : TEE_SMC(CALL_RETURN_FROM_RPC);
}

static void backup_call(struct invoke_tee_response *call,
						struct invoke_tee_response *last_call)
{
	last_call->a0 = call->a0;
	last_call->a1 = call->a1;
}

static void do_smc(struct tee_tz *ptee, struct invoke_tee_response *call,
				struct invoke_tee_response *last_call)
{
	struct tee_task_ctl *ctl = &tee_task_ctl;
	int r;
	unsigned long a0 = rpc_ret_by_cmd_id(call->a0);
	s64 task_start, task_end, call_start, call_end;

	mutex_lock(&ctl->g_lock);

	task_start = ktime_to_us(ktime_get());

	for (;;) {
		/* backup a0 and a1 in case TEESMC_RETURN_EAFFINITY */
		backup_call(call, last_call);

		call_start = ktime_to_us(ktime_get());
		r = ptee->invoke_tee(ptee, call->a0,
			call->a1, call->a2, call->a3, call);
		call_end = ktime_to_us(ktime_get());

		if (r != 0) {
			pr_warn("Unexpected invoke tee failed with %d\n", r);
			call->a0 = r;
			goto out;
		}

		trace_tee_smc(ctl, TEESMC_RETURN_GET_RPC_FUNC(call->a0),
			call_start, call_end);

		if (!TEESMC_RETURN_IS_RPC(call->a0))
			goto out;

		if (TEESMC_RETURN_GET_RPC_FUNC(call->a0) != TEESMC_RPC_FUNC_IRQ)
			goto out;

		call->a0 = a0;
	}

	task_end = ktime_to_us(ktime_get());
	trace_tee_smc_done(ctl, task_start, task_end);

out:
	mutex_unlock(&ctl->g_lock);
}

#ifdef TKCORE_BL

/* callers shall hold cpu->task_lock */
static inline void lock_cpu(struct tee_task_ctl *ctl, int cpu)
{
	ctl->cpu_mask |= (1U << cpu);
	smp_mb();
}

/* callers shall hold cpu->task_lock */
static inline void unlock_cpu(struct tee_task_ctl *ctl, int cpu)
{
	smp_mb();
	ctl->cpu_mask &= ~(1U << cpu);
}

static inline bool cpu_locked(struct tee_task_ctl *ctl, int cpu)
{
	return !!(ctl->cpu_mask & (1U << cpu));
}

static int tee_cpu_online(unsigned int cpu)
{
	return 0;
}

static void rebind_smc_thread(struct tee_task_ctl *ctl, int cpu)
{
	kthread_bind(ctl->threads[cpu].task, cpu);
}

static int tee_cpu_prepare_down(unsigned int cpu)
{
	struct tee_task_ctl *ctl = &tee_task_ctl;

	if (unlikely(cpu >= nr_cpus)) {
		pr_err("Bad cpu: %d\n", cpu);
		return -1;
	}

	if (cpu_locked(ctl, cpu))
		return -1;

	rebind_smc_thread(ctl, cpu);

	return 0;
}

static int tkcore_smc_task(void *data)
{
	int cpu = (int) (unsigned long) data;
	struct tee_task_ctl *ctl = &tee_task_ctl;

	if (cpu < 0 || cpu >= nr_cpus) {
		pr_err("bad cpu: %d\n", cpu);
		return 0;
	}

	while (!kthread_should_stop()) {
		struct tee_task *task = NULL;

		spin_lock(&ctl->task_lock);
		for (;;) {
			DEFINE_WAIT(wait);

			prepare_to_wait(&ctl->threads[cpu].thread_wq,
				&wait, TASK_INTERRUPTIBLE);

			if (!list_empty(&ctl->tasks)) {
				task = list_first_entry(&ctl->tasks,
					struct tee_task, entry);
				list_del(&task->entry);
			}

			if (task) {
				finish_wait(&ctl->threads[cpu].thread_wq, &wait);
				break;
			}

			unlock_cpu(ctl, cpu);
			spin_unlock(&ctl->task_lock);

			schedule();

			spin_lock(&ctl->task_lock);
			finish_wait(&ctl->threads[cpu].thread_wq, &wait);
		}
		spin_unlock(&ctl->task_lock);

		do_smc(task->ptee, task->param, task->last_param);
		complete(&task->done);
	}

	return 0;
}

static int find_matched_cpu(struct tee_task_ctl *ctl,
				const struct tee_task_cpu_affinity *aff, int *need_wakeup)
{
	int cpu;

	for_each_online_cpu(cpu) {
		if (aff->ops->match_cpu(aff->priv, cpu)) {
			*need_wakeup = 1;
			return cpu;
		}
	}

	return -1;
}

static int find_running_cpu(struct tee_task_ctl *ctl)
{
	int cpu;

	if (ctl->cpu_mask == 0)
		return -1;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		if (cpu_locked(ctl, cpu))
			return cpu;
	}

	return -1;
}

static int post_tee_task(struct tee_tz *ptee,
			struct tee_task_ctl *ctl,
			const struct tee_task_cpu_affinity *aff,
			struct invoke_tee_response *call,
			struct invoke_tee_response *last_call)
{
	int picked_cpu = -1;
	int need_wakeup = 0;

	s64 tee_task_start, tee_task_current;

	struct tee_task task;

	task.ptee = ptee;
	task.param = call;
	task.last_param = last_call;

	init_completion(&task.done);
	INIT_LIST_HEAD(&task.entry);

	tee_task_start = ktime_to_us(ktime_get());

	while (picked_cpu < 0) {
		int r, candidate_cpu;

		cpus_read_lock();
		spin_lock(&ctl->task_lock);

		picked_cpu = find_running_cpu(ctl);
		if (picked_cpu >= 0) {
			need_wakeup = 0;
		} else {
			picked_cpu = find_matched_cpu(ctl, aff, &need_wakeup);
			if (picked_cpu >= 0)
				lock_cpu(ctl, picked_cpu);
		}

		cpus_read_unlock();

		if (picked_cpu >= 0)
			list_add_tail(&task.entry, &ctl->tasks);

		spin_unlock(&ctl->task_lock);

		if (picked_cpu < 0) {
			tee_task_current = ktime_to_us(ktime_get());
			if (tee_task_current - tee_task_start >= tee_task_timeout_us)
				return -1;

			candidate_cpu = aff->ops->get_candidate_cpu(aff->priv);
			r = add_cpu(candidate_cpu);
			if (r != 0) {
				pr_warn("tkcore: cpu_on(%d) failed with ret: %d\n",
					candidate_cpu, r);
			}
		}
	}

	if (need_wakeup)
		wake_up(&ctl->threads[picked_cpu].thread_wq);

	wait_for_completion(&task.done);

	return 0;
}

static bool tee_bind_cpu_request_need_bind_cpu(void *priv)
{
	(void) priv;
	return true;
}

static bool tee_bind_cpu_request_match_cpu(void *priv, int cpu)
{
	uint32_t cpumask = ((uint32_t) (unsigned long) priv);

	return !!(cpumask & (1U << cpu));
}

static int tee_bind_cpu_request_get_candidate_cpu(void *priv)
{
	uint32_t cpumask = ((uint32_t) (unsigned long) priv);

	if (cpumask == 0)
		return -1;

	return __builtin_ffs(cpumask) - 1;
}

const struct tee_task_cpu_affinity_operations tee_bind_cpu_request_operations = {
	.need_bind_cpu = tee_bind_cpu_request_need_bind_cpu,
	.match_cpu = tee_bind_cpu_request_match_cpu,
	.get_candidate_cpu = tee_bind_cpu_request_get_candidate_cpu,
};

static void update_tee_task_affinity(struct tee_task_cpu_affinity *aff, uint32_t cpu_mask)
{
	aff->ops = &tee_bind_cpu_request_operations;
	aff->priv = (void *) ((unsigned long) cpu_mask);
}

static void restore_call(struct invoke_tee_response *call,
						struct invoke_tee_response *last_call)
{
	call->a0 = last_call->a0;
	call->a1 = last_call->a1;
}

static void submit_tee_task(struct tee_tz *ptee, struct tee_task_ctl *ctl,
			struct tee_task_cpu_affinity *aff,
			struct invoke_tee_response *call)
{
	int r;
	struct invoke_tee_response last_call = { 0 };
	unsigned long cmd_id = call->a0;

	for (;;) {
		r = aff->ops->need_bind_cpu(aff->priv) ?
			post_tee_task(ptee, ctl, aff, call, &last_call) : -1;
		if (r < 0)
			do_smc(ptee, call, &last_call);

		if (call->a0 == TEESMC_RETURN_TKCORE_RPC_BIND_CPU) {
			update_tee_task_affinity(aff, (uint32_t) call->a1);
			call->a0 = rpc_ret_by_cmd_id(cmd_id);
			call->a1 = 0;
		} else if (call->a0 == TEESMC_RETURN_EAFFINITY) {
			update_tee_task_affinity(aff, (uint32_t) call->a1);
			restore_call(call, &last_call);
		} else {
			break;
		}
	}
}

static int platform_bl_init(struct tee_task_ctl *ctl)
{
	int r, cpu;

	nr_cpus = num_possible_cpus();
	if (nr_cpus > NR_CPUS) {
		pr_err("nr_cpus %d exceeds NR_CPUS %d\n", nr_cpus, NR_CPUS);
		return -1;
	}

	ctl->cpu_mask = 0;
	INIT_LIST_HEAD(&ctl->tasks);
	spin_lock_init(&ctl->task_lock);

	ctl->nr_threads = nr_cpus;
	ctl->threads = kzalloc(nr_cpus * sizeof(struct tee_smc_thread),
		GFP_KERNEL);
	if (ctl->threads == NULL) {
		pr_err("bad kmalloc %zu bytes for threads\n",
			nr_cpus * sizeof(struct tee_smc_thread));
		r = -ENOMEM;
		goto err;
	}

	for_each_possible_cpu(cpu) {
		struct task_struct *task;

		init_waitqueue_head(&ctl->threads[cpu].thread_wq);

		task = kthread_create(tkcore_smc_task,
			(void *) (unsigned long) cpu, "tkcore_smc_task.%u", cpu);
		if (IS_ERR(task)) {
			pr_err("failed to start tkcore_smc_task.%d: %p\n",
				cpu, ctl->threads[cpu].task);
			r = -1;
			goto err;
		}

		ctl->threads[cpu].task = task;
		kthread_bind(task, cpu);
	}

	for (cpu = 0; cpu < ctl->nr_threads; cpu++)
		wake_up_process(ctl->threads[cpu].task);

	r = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
		"tee/tkcore_tzdrv:cpu_listener",
		tee_cpu_online, tee_cpu_prepare_down);
	if (r < 0)
		goto err;

	ctl->tkcore_cpuhp_state = r;

	return 0;

err:
	if (ctl->threads) {
		for (cpu = 0; cpu < ctl->nr_threads; cpu++) {
			if (IS_ERR(ctl->threads[cpu].task))
				break;
			kthread_stop(ctl->threads[cpu].task);
		}

		kfree(ctl->threads);
		ctl->threads = NULL;
	}

	return r;
}

static void platform_bl_deinit(struct tee_task_ctl *ctl)
{
	int cpu;

	cpuhp_remove_state(ctl->tkcore_cpuhp_state);

	for (cpu = 0; cpu < ctl->nr_threads; cpu++)
		kthread_stop(ctl->threads[cpu].task);

	kfree(ctl->threads);
	ctl->threads = NULL;
}

#else

static inline void submit_tee_task(struct tee_tz *ptee,
			struct tee_task_ctl *ctl,
			const struct tee_task_cpu_affinity *aff,
			struct invoke_tee_response *p)
{
	struct invoke_tee_response last_call;

	(void) aff;
	do_smc(ptee, p, &last_call);
}

static int platform_bl_init(struct tee_task_ctl *ctl) { return 0; }

static void platform_bl_deinit(struct tee_task_ctl *ctl) { }

#endif

static void waiters_enqueue(struct tee_task_ctl *ctl)
{
	/*TODO handle too long time of waiting */
	atomic_inc(&ctl->nr_waiting_cmds);
	wait_for_completion(&ctl->smc_comp);
}

static void waiters_dequeue(struct tee_task_ctl *ctl)
{
	if (atomic_dec_if_positive(&ctl->nr_waiting_cmds) >= 0)
		complete(&ctl->smc_comp);
}

void run_tee_task(struct tee_tz *ptee,
				struct invoke_tee_response *tee_req_resp,
				struct tee_task_cpu_affinity *aff)
{
	/* NOTE!!! we remove the e_lock_teez(ptee) here !!!! */
	unsigned long orig_a0 = (unsigned long) tee_req_resp->a0;

	for (;;) {
		submit_tee_task(ptee, &tee_task_ctl, aff, tee_req_resp);
		if (tee_req_resp->a0 == TEESMC_RETURN_ETHREAD_LIMIT) {
			waiters_enqueue(&tee_task_ctl);
			tee_req_resp->a0 = orig_a0;
		} else {
			if (!TEESMC_RETURN_IS_RPC(tee_req_resp->a0))
				waiters_dequeue(&tee_task_ctl);
			break;
		}
	}
}

int tee_init_task(void)
{
	struct tee_task_ctl *ctl = &tee_task_ctl;

	mutex_init(&ctl->g_lock);

	atomic_set(&ctl->nr_waiting_cmds, 0);
	init_completion(&ctl->smc_comp);

	ctl->max_smc_time = 0LL;
	ctl->max_task_time = 0LL;

	return platform_bl_init(ctl);
}

void tee_exit_task(void)
{
	struct tee_task_ctl *ctl = &tee_task_ctl;

	platform_bl_deinit(ctl);
}
