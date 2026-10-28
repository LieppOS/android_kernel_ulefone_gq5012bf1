// SPDX-License-Identifier: GPL-2.0
/* Production session methods; fake PID/queue/RCU services, NOT a kernel proof. */
#include "adpf_production.h"

static struct pid owner_a = {1, 42, true}, reused = {1, 42, true}, other = {1, 43, true};
static struct task_struct caller_a = {&owner_a}, same_process = {&owner_a};
static struct task_struct caller_reused = {&reused}, caller_other = {&other};
static struct adpf_session_snapshot delivered[1024];
static unsigned int deliveries;

static int capture(struct _SESSION *session)
{
	assert(deliveries < 1024);
	assert(adpf_copy_session(&delivered[deliveries++], session) == 0);
	return 0;
}

#define UNCHANGED(sid, call, error) do { \
	struct _SESSION saved = *sessionList[sid]; \
	struct _SESSION_WORK_DURATION durations[ADPF_MAX_THREAD]; \
	int pending = atomic_read(&adpf_pending); \
	struct pid *owner = adpf_owners[sid]; \
	memcpy(durations, adpf_durations[sid], sizeof(durations)); \
	assert((call) == (error)); \
	assert(!memcmp(&saved, sessionList[sid], sizeof(saved))); \
	assert(!memcmp(durations, adpf_durations[sid], sizeof(durations))); \
	assert(adpf_owners[sid] == owner); \
	assert(atomic_read(&adpf_pending) == pending); \
} while (0)

static int create(unsigned int sid)
{
	int threads[] = {201, 202};
	return adpf_create_session_hint(sid, 200, 10001, threads, 2, 1000);
}

static void reap(void)
{
	adpf_reaper.pending = false;
	adpf_reaper.work.fn(&adpf_reaper.work);
}

static void *update_thread(void *unused)
{
	current = &same_process;
	for (int i = 0; i < 50; i++)
		assert(adpf_update_work_duaration(2, 100+i) == 0);
	return NULL;
}

int main(void)
{
	int threads[] = {300, 301, 302};
	struct _ADPF_WORK_DURATION samples[] = {{11, 22}, {33, 44}};
	struct file file = {};
	int slot, before;
	pthread_t a, b;

	/* Kernel allocation/registration boundaries. The session API bodies and
	 * event dispatch below are taken verbatim from the maintained service.
	 */
	for (int i = 0; i < ADPF_MAX_SESSION; i++) {
		sessionList[i] = &adpf_sessions[i];
		for (int j = 0; j < ADPF_MAX_THREAD; j++)
			sessionList[i]->workDuration[j] = &adpf_durations[i][j];
	}
	adpf_events = &queue;
	adpf_stopping = false;
	current = &caller_a;
	slot = adpf_register_callback(capture);
	assert(slot == 0 && adpf_register_callback(capture) == -EEXIST);

	fail_alloc = 1;
	UNCHANGED(2, create(2), -ENOMEM);
	assert(owner_a.refs == 1);
	assert(create(ADPF_MAX_SESSION) == -EINVAL);
	assert(create(2) == 0 && owner_a.refs == 3 && adpf_reaper.pending);
	UNCHANGED(2, create(2), -EEXIST);
	assert(sessionList[2]->tgid == 200 && adpf_owners[2] == &owner_a);
	/* Change numeric lookup AFTER commit but BEFORE dispatch. Both the live
	 * slot and the queued snapshot must still name the captured old thread.
	 */
	t201.alive = false;
	reuse_thread201 = true;
	assert(sessionList[2]->thread_owners[0] == &t201);
	drain(&queue);
	assert(delivered[0].session.thread_owners[0] == &t201);
	assert(delivered[0].session.thread_owners[0] != &t201_reused);
	assert(deliveries == 1 && delivered[0].session.sid == 2);
	assert(delivered[0].session.threadIds[0] == 201);

	/* Allocation failure of EVERY mutating command preserves all backing
	 * arrays, pointers, owner and queue accounting, not just a used flag.
	 */
	fail_alloc = 1; UNCHANGED(2, adpf_update_work_duaration(2, 2000), -ENOMEM);
	fail_alloc = 1; UNCHANGED(2, adpf_report_actual_work_duaration(2, samples, 2), -ENOMEM);
	fail_alloc = 1; UNCHANGED(2, adpf_set_threads(2, threads, 3), -ENOMEM);
	fail_alloc = 1; UNCHANGED(2, adpf_sent_hint(2, CPU_LOAD_UP), -ENOMEM);
	fail_alloc = 1; UNCHANGED(2, adpf_pause(2), -ENOMEM);
	fail_alloc = 1; UNCHANGED(2, adpf_resume(2), -ENOMEM);
	fail_alloc = 1; UNCHANGED(2, adpf_close(2), -ENOMEM);
	UNCHANGED(2, adpf_sent_hint(2, POWER_EFFICIENCY+1), -EINVAL);
	UNCHANGED(2, adpf_update_work_duaration(2, 0), -EINVAL);
	assert(adpf_close(63) == -ENOENT && adpf_pause(64) == -EINVAL);
	int partial[] = {300, 9999}, foreign_ids[] = {300, 303}, duplicate[] = {300, 300};
	UNCHANGED(2, adpf_set_threads(2, partial, 2), -ESRCH);
	UNCHANGED(2, adpf_set_threads(2, foreign_ids, 2), -EPERM);
	UNCHANGED(2, adpf_set_threads(2, duplicate, 2), -EINVAL);
	assert(t300.refs == 1 && t303.refs == 1);

	/* Other threads of the daemon are allowed. Another daemon or even a
	 * recycled numeric PID cannot alter/close it; no fd lifetime assumption.
	 */
	current = &caller_other;
	UNCHANGED(2, adpf_update_work_duaration(2, 2000), -EPERM);
	UNCHANGED(2, adpf_report_actual_work_duaration(2, samples, 2), -EPERM);
	UNCHANGED(2, adpf_set_threads(2, threads, 3), -EPERM);
	UNCHANGED(2, adpf_sent_hint(2, CPU_LOAD_UP), -EPERM);
	UNCHANGED(2, adpf_pause(2), -EPERM);
	UNCHANGED(2, adpf_resume(2), -EPERM);
	UNCHANGED(2, adpf_close(2), -EPERM);
	current = &caller_reused;
	UNCHANGED(2, adpf_close(2), -EPERM);
	current = &same_process;
	assert(adpf_set_threads(2, threads, 3) == 0);
	threads[0] = 999;
	assert(sessionList[2]->threadIds[0] == 300);
	assert(adpf_report_actual_work_duaration(2, samples, 2) == 0);
	samples[0].durationNanos = 999;
	drain(&queue);
	assert(delivered[1].session.threadIds[0] == 300);
	assert(delivered[2].session.workDuration[0]->durationNanos == 22);
	assert(delivered[2].session.workDuration[0] != sessionList[2]->workDuration[0]);
	assert(sessionList[2]->workDuration[0] == &adpf_durations[2][0]);

	before = deliveries;
	assert(!pthread_create(&a, NULL, update_thread, NULL));
	assert(!pthread_create(&b, NULL, update_thread, NULL));
	pthread_join(a, NULL); pthread_join(b, NULL);
	assert(atomic_read(&adpf_pending) == 100);
	drain(&queue);
	assert(deliveries == before+100);
	for (int i = before; i < deliveries; i++) {
		assert(delivered[i].session.cmd == ADPF_UPDATE_TARGET_WORK_DURATION);
		assert(delivered[i].session.targetDurationNanos >= 100);
	}

	assert(create(3) == 0 && create(4) == 0);
	drain(&queue);
	/* A full session table reserves one future cleanup event per owner.
	 * Saturating ordinary producers must still permit one ordered atomic
	 * disable batch, without expanding the original 256-event limit.
	 */
	for (int i = 0; i < ADPF_MAX_SESSION; i++)
		if (sessionList[i]->used != SESSION_USED)
			assert(create(i) == 0);
	drain(&queue);
	int admitted = 0, ret;
	while ((ret = adpf_update_work_duaration(2, 1000)) == 0)
		admitted++;
	assert(ret == -ENOMEM && admitted == ADPF_MAX_PENDING_EVENTS - ADPF_MAX_SESSION);
	assert(atomic_read(&adpf_pending) == admitted);
	assert(perfmgr_adpf_enable_proc_write(&file, "0", 1, NULL) == 1);
	assert(atomic_read(&adpf_pending) == ADPF_MAX_PENDING_EVENTS);
	for (int i = 0; i < ADPF_MAX_SESSION; i++)
		assert(sessionList[i]->used == SESSION_UNUSED);
	drain(&queue);
	assert(owner_a.refs == 1 && !atomic_read(&adpf_pending));
	assert(perfmgr_adpf_enable_proc_write(&file, "1", 1, NULL) == 1);
	assert(!create(2) && !create(3) && !create(4));
	drain(&queue);
	/* Disable-all must not close a prefix if reservation #2 fails. */
	fail_alloc = 2;
	assert(perfmgr_adpf_enable_proc_write(&file, "0", 1, NULL) == -ENOMEM);
	assert(adpf_enable == 1 && atomic_read(&adpf_pending) == 0);
	for (int i = 2; i <= 4; i++)
		assert(sessionList[i]->used == SESSION_USED && adpf_owners[i] == &owner_a);
	assert(owner_a.refs == 4);
	/* Prepared reservations count toward the SAME old 256-event bound. */
	adpf_pending.counter = 255;
	assert(perfmgr_adpf_enable_proc_write(&file, "0", 1, NULL) == -ENOMEM);
	assert(adpf_pending.counter == 255 && owner_a.refs == 4 && adpf_enable == 1);
	adpf_pending.counter = 0;
	assert(perfmgr_adpf_enable_proc_write(&file, "0", 1, NULL) == 1);
	assert(adpf_enable == 0 && owner_a.refs == 4); /* three queued CLOSE refs */
	assert(create(5) == -EACCES);
	drain(&queue);
	assert(perfmgr_adpf_enable_proc_write(&file, "1", 1, NULL) == 1);

	/* Dead creator: failed CLOSE remains live/retryable. Actual reaper then
	 * delivers CLOSE before a replacement process can publish a new CREATE.
	 */
	assert(create(5) == 0);
	drain(&queue);
	owner_a.alive = false;
	current = &caller_reused;
	fail_alloc = 1; reap();
	assert(owner_a.refs == 2 && sessionList[5]->used == SESSION_USED);
	assert(adpf_reaper.pending);
	UNCHANGED(5, adpf_close(5), -EPERM);
	before = deliveries;
	assert(create(5) == 0); /* synchronously orders the stale CLOSE */
	assert(owner_a.refs == 2 && reused.refs == 3); /* old CLOSE + new CREATE */
	drain(&queue);
	assert(deliveries == before+2);
	assert(delivered[before].session.cmd == ADPF_CLOSE);
	assert(delivered[before+1].session.cmd == ADPF_CREATE_HINT_SESSION);
	reused.alive = false;
	reap();
	assert(reused.refs == 2 && sessionList[5]->used == SESSION_UNUSED);
	drain(&queue);
	assert(!adpf_reaper.pending);

	owner_a.alive = true;
	current = &caller_a;
	owner_a.zombie = true;
	owner_a.other_live = true;
	assert(create(7) == 0); /* leader exit alone does not kill the process */
	drain(&queue);
	reap();
	assert(sessionList[7]->used == SESSION_USED);
	owner_a.other_live = false;
	reap(); /* all-zombie process is dead although its PID still has a task */
	assert(sessionList[7]->used == SESSION_UNUSED);
	drain(&queue);
	owner_a.zombie = false;
	assert(create(6) == 0);
	assert(adpf_unregister_callback(slot) == slot);
	powerhal_destroy_events(); /* pending event + active owner + pending reap */
	assert(!adpf_events && adpf_stopping && !adpf_reaper.pending);
	assert(owner_a.refs == 1 && reused.refs == 1 && other.refs == 1 && target_app.refs == 1);
	assert(atomic_read(&adpf_pending) == 0 && allocs == frees);
	assert(t201.refs == 1 && t201_reused.refs == 1 && t202.refs == 1);
	assert(t300.refs == 1 && t301.refs == 1 && t302.refs == 1 && t303.refs == 1);
	assert(create(7) == -EACCES);
	assert(adpf_close(6) == -ESHUTDOWN);
	return 0;
}
