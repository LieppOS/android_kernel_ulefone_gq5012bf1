/* SPDX-License-Identifier: GPL-2.0 */
/* Native EAS consumer of committed PowerHAL identities and genuine providers.
 * One sleepable mutex owns sessions/member refcounts. Native provider locks are
 * never held over allocations, task puts or CPUQoS notification IPIs.
 */
#define EAS_OWNED_MAX_SESSIONS 64
struct eas_owned_member {
	struct list_head node;
	struct task_struct *task;
	struct mtk_group_task_lease *group;
	struct mtk_perf_task_lease *vip;
	unsigned int users;
};
struct eas_owned_session {
	bool used, paused, quiesced, use_group, use_vip;
	unsigned int sid, count, cap;
	u64 target_ns;
	struct pid *owner, *target;
	struct pid *threads[ADPF_MAX_THREAD];
	struct eas_owned_member *members[ADPF_MAX_THREAD];
	struct mtk_group_ratio_lease *ratio;
};
static DEFINE_MUTEX(adpf_mutex);
static LIST_HEAD(eas_owned_members);
static struct eas_owned_session eas_owned_sessions[EAS_OWNED_MAX_SESSIONS];
static int eas_adpf_enable = 1;
static bool eas_owned_pinned;
static void eas_owned_reap(struct work_struct *work);
static DECLARE_DELAYED_WORK(eas_owned_reaper, eas_owned_reap);

static bool eas_owned_task_alive(struct task_struct *task, struct pid *target)
{
	return task && pid_alive(task) && task_tgid(task) == target &&
		!READ_ONCE(task->exit_state) &&
		!(READ_ONCE(task->flags) & (PF_EXITING | PF_KTHREAD));
}

static bool eas_owned_member_live(struct eas_owned_member *member)
{
	return (!member->group || mtk_group_task_is_live(member->group)) &&
		(!member->vip || mtk_perf_task_is_live(member->vip));
}

static void eas_owned_member_put(struct eas_owned_member *member)
{
	if (!member || --member->users)
		return;
	list_del(&member->node);
	mtk_group_task_release(member->group);
	mtk_perf_task_release(member->vip);
	put_task_struct(member->task);
	kfree(member);
}

static struct eas_owned_member *eas_owned_member_get(struct pid *identity,
	struct eas_owned_session *session)
{
	struct task_struct *task = get_pid_task(identity, PIDTYPE_PID);
	struct eas_owned_member *member;
	int ret;

	if (!eas_owned_task_alive(task, session->target)) {
		if (task)
			put_task_struct(task);
		return ERR_PTR(-ESRCH);
	}
	list_for_each_entry(member, &eas_owned_members, node) {
		if (member->task != task)
			continue;
		put_task_struct(task);
		if (!!member->group != session->use_group ||
			!!member->vip != session->use_vip)
			return ERR_PTR(-EBUSY);
		if (!eas_owned_member_live(member))
			return ERR_PTR(-ESTALE);
		member->users++;
		return member;
	}
	member = kzalloc(sizeof(*member), GFP_KERNEL);
	if (!member) {
		put_task_struct(task);
		return ERR_PTR(-ENOMEM);
	}
	member->task = task;
	member->users = 1;
	if (session->use_group) {
		ret = mtk_group_task_acquire(task, GROUP_ID_2, &member->group);
		if (ret)
			goto fail;
	}
	if (session->use_vip) {
		ret = mtk_perf_task_acquire(task, true, &member->vip);
		if (ret)
			goto fail;
	}
	if (!eas_owned_member_live(member)) {
		ret = -ESTALE;
		goto fail;
	}
	list_add_tail(&member->node, &eas_owned_members);
	return member;
fail:
	mtk_group_task_release(member->group);
	mtk_perf_task_release(member->vip);
	put_task_struct(task);
	kfree(member);
	return ERR_PTR(ret);
}

static void eas_owned_clear(struct eas_owned_session *session)
{
	unsigned int i;

	for (i = 0; i < session->count; i++) {
		eas_owned_member_put(session->members[i]);
		put_pid(session->threads[i]);
	}
	mtk_group_ratio_release(session->ratio);
	put_pid(session->owner);
	put_pid(session->target);
	memset(session, 0, sizeof(*session));
}

static void eas_owned_withdraw(struct eas_owned_session *session)
{
	unsigned int i;

	for (i = 0; i < session->count; i++) {
		eas_owned_member_put(session->members[i]);
		session->members[i] = NULL;
	}
	mtk_group_ratio_release(session->ratio);
	session->ratio = NULL;
	session->quiesced = true;
}

static bool eas_owned_session_live(struct eas_owned_session *session)
{
	unsigned int i;

	if (session->paused || session->quiesced)
		return true; /* no requests; explicit SET_THREADS/RESUME may repair */
	if ((session->use_group && group_get_mode() == GP_MODE_0) ||
		(session->use_vip && !sched_vip_enable_get()) ||
		(session->use_group && (!session->ratio ||
			!mtk_group_ratio_is_live(session->ratio))))
		return false;
	for (i = 0; i < session->count; i++) {
		struct task_struct *task = get_pid_task(session->threads[i], PIDTYPE_PID);
		bool live = eas_owned_task_alive(task, session->target);

		if (task)
			put_task_struct(task);
		if (!live || !session->members[i] ||
			!eas_owned_member_live(session->members[i]))
			return false;
	}
	return true;
}

static bool eas_owned_sweep(void)
{
	bool active = false;
	int i;

	for (i = 0; i < EAS_OWNED_MAX_SESSIONS; i++) {
		struct eas_owned_session *session = &eas_owned_sessions[i];

		if (session->used) {
			if (!adpf_process_alive(session->owner) ||
				!adpf_process_alive(session->target))
				eas_owned_clear(session);
			else if (!eas_owned_session_live(session))
				eas_owned_withdraw(session);
		}
		active |= session->used;
	}
	return active;
}

static void eas_owned_reap(struct work_struct *work)
{
	mutex_lock(&adpf_mutex);
	if (eas_owned_sweep())
		queue_delayed_work(system_wq, &eas_owned_reaper, HZ);
	mutex_unlock(&adpf_mutex);
}

/* Candidate references/resources are independent of the live session. On any
 * failure release only the candidate; never drop the old membership prefix.
 * As with the underlying providers, ordinary writes may revoke a half at any
 * time. They are detected here/on dispatch/reaping, never overwritten to retry.
 */
static int eas_owned_replace(struct eas_owned_session *session,
	struct pid * const *identities, unsigned int count, bool activate)
{
	struct pid *next_ids[ADPF_MAX_THREAD] = {};
	struct eas_owned_member *next_members[ADPF_MAX_THREAD] = {};
	struct mtk_group_ratio_lease *new_ratio = NULL, *ratio;
	unsigned int i, j;
	int ret = 0;

	if (!count || count > ADPF_MAX_THREAD || !identities)
		return -EINVAL;
	if (activate && ((session->use_group && group_get_mode() == GP_MODE_0) ||
		(session->use_vip && !sched_vip_enable_get())))
		return -EOPNOTSUPP;
	if (activate && session->use_group && !session->ratio) {
		ret = mtk_group_ratio_acquire(GROUP_ID_2, &new_ratio);
		if (ret)
			return ret;
	}
	for (i = 0; i < count; i++) {
		struct task_struct *task;

		if (!identities[i]) {
			ret = -EINVAL;
			goto abort;
		}
		for (j = 0; j < i; j++)
			if (identities[i] == identities[j]) {
				ret = -EINVAL;
				goto abort;
			}
		next_ids[i] = get_pid(identities[i]);
		if (activate) {
			next_members[i] = eas_owned_member_get(identities[i], session);
			if (IS_ERR(next_members[i])) {
				ret = PTR_ERR(next_members[i]);
				next_members[i] = NULL;
				goto abort;
			}
		} else {
			task = get_pid_task(identities[i], PIDTYPE_PID);
			if (!eas_owned_task_alive(task, session->target))
				ret = -ESRCH;
			if (task)
				put_task_struct(task);
			if (ret)
				goto abort;
		}
	}
	if (!adpf_process_alive(session->owner) || !adpf_process_alive(session->target)) {
		ret = -ESRCH;
		goto abort;
	}
	for (i = 0; i < count; i++)
		if (next_members[i] && !eas_owned_member_live(next_members[i])) {
			ret = -ESTALE;
			goto abort;
		}
	ratio = session->ratio ? session->ratio : new_ratio;
	if (activate && ratio) {
		ret = mtk_group_ratio_set(ratio, session->cap);
		if (ret)
			goto abort;
	}
	for (i = 0; i < session->count; i++) {
		eas_owned_member_put(session->members[i]);
		put_pid(session->threads[i]);
	}
	if (new_ratio)
		session->ratio = new_ratio; /* move, not another owner */
	session->quiesced = false;
	memcpy(session->threads, next_ids, sizeof(next_ids));
	memcpy(session->members, next_members, sizeof(next_members));
	session->count = count;
	return 0;
abort:
	mtk_group_ratio_release(new_ratio);
	for (i = 0; i < count; i++) {
		eas_owned_member_put(next_members[i]);
		put_pid(next_ids[i]);
	}
	return ret;
}

int sched_adpf_callback(struct _SESSION *event)
{
	struct eas_owned_session *session = NULL, *free_slot = NULL;
	unsigned int i;
	int ret = 0;

	if (!event || !event->owner || !event->target)
		return -EINVAL;
	mutex_lock(&adpf_mutex);
	eas_owned_sweep();
	for (i = 0; i < EAS_OWNED_MAX_SESSIONS; i++) {
		struct eas_owned_session *s = &eas_owned_sessions[i];

		if (!s->used) {
			if (!free_slot)
				free_slot = s;
		} else if (s->owner == event->owner && s->target == event->target &&
			s->sid == event->sid)
			session = s;
	}
	/* Cleanup is valid after process death, disable or resource revocation. */
	if (event->cmd == ADPF_CLOSE) {
		if (session)
			eas_owned_clear(session);
		goto out;
	}
	if (!eas_adpf_enable) {
		ret = -EACCES;
		goto out;
	}
	if (event->cmd == ADPF_CREATE_HINT_SESSION) {
		struct eas_owned_session next = {};

		if (session) {
			ret = -EEXIST;
			goto out;
		}
		if (!free_slot) {
			ret = -ENOSPC;
			goto out;
		}
		if (event->durationNanos <= 0) {
			ret = -EINVAL;
			goto out;
		}
		next.owner = get_pid(event->owner);
		next.target = get_pid(event->target);
		next.sid = event->sid;
		next.target_ns = event->durationNanos;
		next.use_group = group_get_mode() != GP_MODE_0;
		next.use_vip = IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK) && sched_vip_enable_get();
		if (!next.use_group && !next.use_vip) {
			ret = -EOPNOTSUPP;
			goto abort_create;
		}
		ret = eas_owned_replace(&next, event->thread_owners,
			event->threadIds_size, true);
		if (ret)
			goto abort_create;
		next.used = true;
		*free_slot = next; /* owning move; no fallible steps after publication */
		if (!eas_owned_pinned) {
			__module_get(THIS_MODULE);
			eas_owned_pinned = true;
		}
		queue_delayed_work(system_wq, &eas_owned_reaper, HZ);
		goto out;
abort_create:
		eas_owned_clear(&next);
		goto out;
	}
	if (!session) {
		ret = -ENOENT;
		goto out;
	}
	switch (event->cmd) {
	case ADPF_SET_THREADS:
		ret = eas_owned_replace(session, event->thread_owners,
			event->threadIds_size, !session->paused);
		break;
	case ADPF_UPDATE_TARGET_WORK_DURATION:
		if (event->targetDurationNanos <= 0)
			ret = -EINVAL;
		else
			session->target_ns = event->targetDurationNanos;
		break;
	case ADPF_REPORT_ACTUAL_WORK_DURATION: {
		struct _SESSION_WORK_DURATION *sample;
		u64 duration;
		unsigned int cap;

		if (event->work_duration_size <= 0 ||
			event->work_duration_size > ADPF_MAX_THREAD ||
			!event->workDuration[event->work_duration_size - 1]) {
			ret = -EINVAL;
			break;
		}
		sample = event->workDuration[event->work_duration_size - 1];
		if (sample->durationNanos <= 0 || !session->target_ns) {
			ret = -EINVAL;
			break;
		}
		duration = sample->durationNanos;
		cap = duration >= session->target_ns ? SCHED_CAPACITY_SCALE :
			max_t(u64, 1, mul_u64_u64_div_u64(duration,
				SCHED_CAPACITY_SCALE, session->target_ns));
		if (session->quiesced && !session->paused) {
			unsigned int old_cap = session->cap;

			/* A NEW work report is a new request, not a reaper restoring an
			 * old boost over an ordinary FPSGO/native writer. Re-admit only
			 * captured PID objects and take a fresh current-state baseline.
			 */
			session->cap = cap;
			ret = eas_owned_replace(session, session->threads,
				session->count, true);
			if (ret)
				session->cap = old_cap;
		} else {
			if (!session->paused && session->ratio)
				ret = mtk_group_ratio_set(session->ratio, cap);
			if (!ret)
				session->cap = cap;
		}
		break;
	}
	case ADPF_PAUSE:
		eas_owned_withdraw(session);
		session->paused = true;
		break;
	case ADPF_RESUME:
		if (session->paused || session->quiesced) {
			ret = eas_owned_replace(session, session->threads,
				session->count, true);
			if (!ret)
				session->paused = false;
		}
		break;
	default:
		/* No invented hint/energy policy for native vendor TODO commands. */
		ret = -EOPNOTSUPP;
		break;
	}
	/* A real ordinary writer may revoke a resource during a request. */
	if (session && session->used && !eas_owned_session_live(session))
		eas_owned_withdraw(session);
out:
	if (trace_sched_adpf_get_value_enabled())
		trace_sched_adpf_get_value(event->cmd, event->sid, event->tgid,
			event->uid, event->threadIds_size, event->targetDurationNanos);
	mutex_unlock(&adpf_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sched_adpf_callback);

void eas_adpf_release_sessions(void)
{
	unsigned int i;

	/* The owning adapter first unregisters/joins its PowerHAL SRCU callback.
	 * This drains resources without changing an operator's enable policy.
	 */
	mutex_lock(&adpf_mutex);
	for (i = 0; i < EAS_OWNED_MAX_SESSIONS; i++)
		if (eas_owned_sessions[i].used)
			eas_owned_clear(&eas_owned_sessions[i]);
	mutex_unlock(&adpf_mutex);
}
EXPORT_SYMBOL_GPL(eas_adpf_release_sessions);

void set_eas_adpf_enable(int val)
{
	unsigned int i;

	mutex_lock(&adpf_mutex);
	WRITE_ONCE(eas_adpf_enable, !!val);
	if (!val)
		for (i = 0; i < EAS_OWNED_MAX_SESSIONS; i++)
			if (eas_owned_sessions[i].used)
				eas_owned_clear(&eas_owned_sessions[i]);
	/* An already scheduled reaper can run once and observe the empty table.
	 * No cancel_sync under this mutex and no stale sessions restored on enable.
	 */
	mutex_unlock(&adpf_mutex);
}
EXPORT_SYMBOL_GPL(set_eas_adpf_enable);

int get_eas_adpf_enable(void)
{
	return READ_ONCE(eas_adpf_enable);
}
EXPORT_SYMBOL_GPL(get_eas_adpf_enable);
