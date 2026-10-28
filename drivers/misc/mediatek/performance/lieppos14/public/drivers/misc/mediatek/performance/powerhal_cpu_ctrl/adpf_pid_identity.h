/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LIEPPOS_ADPF_PID_IDENTITY_H
#define LIEPPOS_ADPF_PID_IDENTITY_H
#include <linux/pid.h>
#include <linux/sched/signal.h>

/* A zombie leader is not proof of process life; nor does leader exit imply
 * death when another thread still executes. Callers retain the process PID.
 */
static inline bool adpf_process_alive(struct pid *process)
{
	struct task_struct *leader, *thread;
	bool alive = false;

	rcu_read_lock();
	leader = process ? pid_task(process, PIDTYPE_TGID) : NULL;
	if (leader) {
		if (!READ_ONCE(leader->exit_state) &&
			!(READ_ONCE(leader->flags) & PF_EXITING))
			alive = true;
		for_each_thread(leader, thread) {
			if (!READ_ONCE(thread->exit_state) &&
				!(READ_ONCE(thread->flags) & PF_EXITING)) {
				alive = true;
				break;
			}
		}
	}
	rcu_read_unlock();
	return alive;
}
#endif
