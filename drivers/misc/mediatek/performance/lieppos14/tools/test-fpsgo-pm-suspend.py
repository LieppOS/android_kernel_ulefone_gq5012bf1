#!/usr/bin/env python3
"""Run the real FPSGO/FBT suspend-resume coordination.

Executes the production PM notifier, the cpufreq ceiling quiesce/resume, the
loading sampler and the policy attach path against modeled kernel primitives,
and verifies at source level that both rescue workers carry the suspend-epoch
guard (those workers need the render tree and thread locks and are therefore
not extractable as free functions).
"""
import argparse
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
a.output.mkdir(exist_ok=False)

base = root/'public/drivers/misc/mediatek/performance/fpsgo_v3'
ctrl = (base/'fbt/src/fbt_cpu_ctrl.c').read_text()
cpu = (base/'fbt/src/fbt_cpu.c').read_text()


def take(text, signature):
    m = re.search(r'^' + re.escape(signature) + r'\n\{', text, re.M)
    assert m, signature
    end, depth = m.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end] + '\n'


# --- source-level guard verification (workers hold render/thread locks) ---
sjerk_worker = take(cpu, 'static void fbt_do_sjerk(struct work_struct *work)')
jerk_worker = take(cpu, 'static void fbt_do_jerk(struct work_struct *work)')
assert 'fbt_pm_epoch_current(jerk->pm_epoch)' in sjerk_worker, 'sjerk epoch guard missing'
assert 'fbt_pm_epoch_current(jerk->pm_epoch)' in jerk_worker, 'jerk epoch guard missing'
assert 'register_pm_notifier(&fbt_pm_notifier)' in cpu and \
       'unregister_pm_notifier(&fbt_pm_notifier)' in cpu, 'PM notifier not owned'
assert cpu.count('register_pm_notifier(&fbt_pm_notifier)') == 2, 'duplicate PM registration'

prefix = r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FREQ_QOS_MAX_DEFAULT_VALUE INT_MAX
#define FREQ_QOS_MIN 0
#define FREQ_QOS_MAX 1
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define clamp_t(t, v, lo, hi) ((t)clamp((v), (lo), (hi)))
#define pr_warn_ratelimited(...) ((void)0)
#define pr_info(...) ((void)0)
#define DEBUG_LOG 0
#define PM_SUSPEND_PREPARE 3
#define PM_POST_SUSPEND 4
#define PM_HIBERNATION_PREPARE 1
#define PM_POST_HIBERNATION 2
#define PM_RESTORE_PREPARE 5
#define PM_POST_RESTORE 6
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define CPUFREQ_CREATE_POLICY 0
#define CPUFREQ_REMOVE_POLICY 1
#define FBT_CEILING_UNAPPLIED (-1)
typedef unsigned long long u64;
typedef unsigned int u32;
#define MODEL_CPUS 8
#define MODEL_POLICIES 3
#define nr_cpu_ids MODEL_CPUS
#define for_each_possible_cpu(i) for ((i) = 0; (i) < MODEL_CPUS; (i)++)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define mul_u64_u64_div_u64(a, b, c) (((a) * (b)) / (c))

struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); };
struct cpu_info { unsigned long long time; };
struct freq_constraints {
	bool alive;
	struct { int target_value; } min_freq;
	int max_requests, min_notifiers;
	int fbt_applied;     /* the FBT-owned aggregate */
	int other_owner;     /* an unrelated owner's request, must never change */
};
struct cpufreq_policy {
	unsigned int cpu, related_cpus;
	struct { unsigned int min_freq, max_freq; } cpuinfo;
	struct freq_constraints constraints;
	int refcount;
	bool published;
};
struct freq_qos_request { struct freq_constraints *qos; int value; bool active; };

static pthread_mutex_t ctrl_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int lock_depth;
#define mutex_lock(m) do { assert(!pthread_mutex_lock(&ctrl_mutex)); lock_depth++; } while (0)
#define mutex_unlock(m) do { lock_depth--; assert(!pthread_mutex_unlock(&ctrl_mutex)); } while (0)
#define lockdep_assert_held(l) assert(lock_depth > 0)
static int cpu_ctrl_lock, fbt_mlock;

static bool freq_qos_request_active(struct freq_qos_request *r) { return r->active; }
static int freq_qos_add_request(struct freq_constraints *c, struct freq_qos_request *r,
				int type, int value)
{
	assert(c && c->alive && !r->active);
	(void)type;
	r->qos = c; r->value = value; r->active = true;
	c->max_requests++; c->fbt_applied = value;
	return 0;
}
static int freq_qos_remove_request(struct freq_qos_request *r)
{
	assert(r->active && r->qos && r->qos->alive);
	r->qos->max_requests--; r->qos->fbt_applied = FREQ_QOS_MAX_DEFAULT_VALUE;
	r->active = false; r->qos = NULL;
	return 0;
}
static int freq_qos_update_request(struct freq_qos_request *r, int value)
{
	assert(r->active && r->qos && r->qos->alive);
	if (r->value == value)
		return 0;
	r->value = value; r->qos->fbt_applied = value;
	return 1;
}
static int freq_qos_add_notifier(struct freq_constraints *c, int t, struct notifier_block *nb)
{ assert(c && c->alive && nb->notifier_call); (void)t; c->min_notifiers++; return 0; }
static int freq_qos_remove_notifier(struct freq_constraints *c, int t, struct notifier_block *nb)
{ assert(c && c->alive && c->min_notifiers > 0); (void)t; (void)nb; c->min_notifiers--; return 0; }

static struct cpufreq_policy *published[MODEL_POLICIES];
static struct cpufreq_policy *cpufreq_cpu_get(int cpu)
{
	int i;

	for (i = 0; i < MODEL_POLICIES; i++)
		if (published[i] && published[i]->published &&
		    (published[i]->related_cpus & (1u << cpu))) {
			published[i]->refcount++;
			return published[i];
		}
	return NULL;
}
static void cpufreq_cpu_put(struct cpufreq_policy *p) { assert(p->refcount > 0); p->refcount--; }

static unsigned int domain_mask[MODEL_POLICIES] = { 0x0f, 0x70, 0x80 };
static bool fpsgo_cpu_policy_matches(unsigned int index, unsigned int mask)
{ return index < MODEL_POLICIES && domain_mask[index] == mask; }

/* modeled time-derived kernel counters */
static unsigned long long model_wall[MODEL_CPUS], model_idle[MODEL_CPUS];
static bool model_idle_supported = true;
static bool cpu_active(int cpu) { (void)cpu; return true; }
static unsigned long long get_cpu_idle_time(int cpu, unsigned long long *wall, int io)
{
	(void)io;
	*wall = model_wall[cpu];
	return model_idle_supported ? model_idle[cpu] : (unsigned long long)-1;
}

/* modeled timer + workqueue */
static int timer_armed, work_queued, work_joined, wq_flushed;
struct hrtimer { int dummy; };
struct work_struct { int dummy; };
struct workqueue_struct { int dummy; };
static struct hrtimer cpu_ctrl_hrt;
static struct work_struct cpu_loading_timeout_work;
static struct workqueue_struct wq_obj;
static struct workqueue_struct *g_psCpuLoadingWorkQueue = &wq_obj;
static struct workqueue_struct *g_psFbtCpuCtrlWorkQueue = &wq_obj;
#define ms_to_ktime(x) (x)
#define ns_to_ktime(x) (x)
typedef unsigned long long ktime_t;
#define HRTIMER_MODE_REL 0
enum hrtimer_restart { HRTIMER_NORESTART = 0 };
static void hrtimer_start(struct hrtimer *t, ktime_t k, int mode)
{ (void)t; (void)k; (void)mode; timer_armed = 1; }
static void hrtimer_cancel(struct hrtimer *t) { (void)t; timer_armed = 0; }
static void queue_work(struct workqueue_struct *q, struct work_struct *w)
{ (void)q; (void)w; work_queued++; }
static void cancel_work_sync(struct work_struct *w) { (void)w; work_joined++; }
static void flush_workqueue(struct workqueue_struct *q) { (void)q; wq_flushed++; }
static void __cpu_ctrl_systrace(int v, const char *n) { (void)v; (void)n; }
static void __cpu_ctrl_freq_systrace(int p, int f) { (void)p; (void)f; }

/* production module state (fbt_cpu_ctrl.c) */
static int policy_num = MODEL_POLICIES;
static bool ctrl_stopping, ctrl_ready = true, ctrl_suspended, policy_table_live = true;
static bool min_notifier_added_store[MODEL_POLICIES];
static bool *min_notifier_added = min_notifier_added_store;
static int opp_cnt_store[MODEL_POLICIES] = { 4, 4, 4 };
static unsigned int opp0[4] = { 2000, 1500, 1000, 500 };
static unsigned int opp1[4] = { 2600, 2000, 1400, 700 };
static unsigned int opp2[4] = { 3000, 2400, 1600, 800 };
static unsigned int *opp_store[MODEL_POLICIES] = { opp0, opp1, opp2 };
static int *fbt_opp_cnt = opp_cnt_store;
static unsigned int **fbt_opp_tbl = opp_store;
static struct cpufreq_policy *policy_store[MODEL_POLICIES];
static struct cpufreq_policy **fbt_cpu_policy = policy_store;
static struct freq_qos_request rq_store[MODEL_POLICIES];
static struct freq_qos_request *fbt_cpu_rq = rq_store;
static struct notifier_block nb_store[MODEL_POLICIES];
static struct notifier_block *fbt_freq_min_notifier = nb_store;
static unsigned int fbt_min_freq[MODEL_POLICIES] = { 500, 700, 800 };
static unsigned int fbt_max_freq[MODEL_POLICIES] = { 2000, 2600, 3000 };
static int fbt_cur_ceiling[MODEL_POLICIES] = { 2000, 2600, 3000 };
static int fbt_last_ceiling[MODEL_POLICIES] = { -1, -1, -1 };
static int fbt_final_ceiling[MODEL_POLICIES] = { -1, -1, -1 };
static int fbt_cur_floor[MODEL_POLICIES] = { 500, 700, 800 };
static int cfp_ceil_rel, cfp_onoff = 1, cfp_enable = 1, cfp_polling_ms = 64;
static int cfp_cur_up_time, cfp_cur_down_time, cfp_cur_loading;
static int cfp_up_time = 1, cfp_down_time = 16, cfp_up_loading = 90, cfp_down_loading = 80;
static int L_min_cap_enable, L_ceiling_min;
static int ceiling_min_enable_EARA, ceiling_min_M_EARA, ceiling_min_B_EARA;
static struct cpu_info cur_wall_store[MODEL_CPUS], cur_idle_store[MODEL_CPUS];
static struct cpu_info prev_wall_store[MODEL_CPUS], prev_idle_store[MODEL_CPUS];
static struct cpu_info *cur_wall_time = cur_wall_store, *cur_idle_time = cur_idle_store;
static struct cpu_info *prev_wall_time = prev_wall_store, *prev_idle_time = prev_idle_store;
static int freq_min_notifier_call(struct notifier_block *nb, unsigned long e, void *p)
{ (void)nb; (void)e; (void)p; return 0; }
typedef void (*cfp_notifier_fn_t)(int);
#define CFP_KIR_FPSGO 0
static void fbt_cfp_notifier(int heavy);
static int cfp_mon_enable(int k, cfp_notifier_fn_t cb) { (void)k; (void)cb; return 0; }
static int cfp_mon_disable(int k) { (void)k; return 0; }
static void fbt_cfp_notifier(int heavy) { (void)heavy; }

/* production module state (fbt_cpu.c side) */
struct fbt_sjerk {
	struct pid *identity;
	unsigned long long arm_seq, work_seq;
	unsigned int pm_epoch;
	int active_id, jerking;
	struct hrtimer timer;
	struct work_struct work;
};
static struct fbt_sjerk sjerk;
static int sjerk_cancelled;
static void fbt_cancel_sjerk(void)
{
	hrtimer_cancel(&sjerk.timer);
	sjerk.arm_seq++;
	sjerk.jerking = 0;
	sjerk.active_id = -1;
	sjerk.identity = NULL;
	sjerk_cancelled++;
}
'''

mid = '\n'.join([
    take(ctrl, 'static int fbt_ceiling_request_locked(unsigned int i,\n'
               '\t\t\t\t      const struct cpufreq_policy *policy)'),
    take(ctrl, 'static void __update_cpu_freq_locked(void)'),
    take(ctrl, 'static void enable_cpu_loading_timer(void)'),
    take(ctrl, 'static void disable_cpu_loading_timer(void)'),
    take(ctrl, 'static enum hrtimer_restart handle_cpu_loading_timeout(struct hrtimer *timer)'),
    take(ctrl, 'static int update_cpu_loading_locked(void)'),
    take(ctrl, 'static void fbt_detach_policy(unsigned int i)'),
    take(ctrl, 'static int fbt_attach_policy_locked(unsigned int i, struct cpufreq_policy *policy)'),
    take(ctrl, 'static int fbt_policy_index(const struct cpufreq_policy *policy)'),
    take(ctrl, 'static int fbt_policy_event(struct notifier_block *nb, unsigned long event, void *data)'),
    take(ctrl, 'int fbt_set_cpu_freq_ceiling(int num, int *freq)'),
    take(ctrl, 'int fbt_cpu_ctrl_suspend(void)'),
    take(ctrl, 'int fbt_cpu_ctrl_resume(void)'),
    take(cpu, 'static int fbt_pm_event(struct notifier_block *nb, unsigned long event, void *data)'),
])
# the production epoch helper and its storage, taken verbatim
epoch = re.search(r'static unsigned int fbt_pm_epoch;\n\nstatic inline bool '
                  r'fbt_pm_epoch_current\(unsigned int armed\)\n\{\n.*?\n\}\n', cpu, re.S)
assert epoch, 'epoch helper missing'
mid = epoch.group(0) + '\n' + mid

suffix = r'''
/* ---- harness ---- */

static struct cpufreq_policy *make_policy(int idx)
{
	struct cpufreq_policy *p = calloc(1, sizeof(*p));

	assert(p);
	p->related_cpus = domain_mask[idx];
	p->cpu = idx == 0 ? 0 : (idx == 1 ? 4 : 7);
	p->cpuinfo.min_freq = fbt_min_freq[idx];
	p->cpuinfo.max_freq = fbt_max_freq[idx];
	p->constraints.alive = true;
	p->constraints.min_freq.target_value = fbt_min_freq[idx];
	p->constraints.fbt_applied = FREQ_QOS_MAX_DEFAULT_VALUE;
	p->constraints.other_owner = 1234;      /* another owner's lease */
	published[idx] = p;
	p->published = true;
	return p;
}

static void set_ceiling(int c0, int c1, int c2)
{
	int f[MODEL_POLICIES] = { c0, c1, c2 };

	assert(fbt_set_cpu_freq_ceiling(MODEL_POLICIES, f) == 0);
}

static void pm(unsigned long event)
{
	int ret = fbt_pm_event(NULL, event, NULL);

	assert(ret == NOTIFY_OK || ret == NOTIFY_DONE);
}

int main(void)
{
	int i, before;

	for (i = 0; i < MODEL_POLICIES; i++) {
		struct cpufreq_policy *p = make_policy(i);

		assert(fbt_policy_event(NULL, 0 /* CREATE */, p) == NOTIFY_OK);
		assert(fbt_cpu_policy[i] == p);
	}
	set_ceiling(1500, 2000, 2400);
	assert(fbt_cpu_policy[0]->constraints.fbt_applied == 1500);

	/* 1. suspend withdraws the owned constraint, preserves the demand and
	 * never touches another owner's request
	 */
	before = work_joined;
	pm(PM_SUSPEND_PREPARE);
	assert(ctrl_suspended);
	for (i = 0; i < MODEL_POLICIES; i++) {
		assert(fbt_cpu_policy[i]->constraints.fbt_applied == FREQ_QOS_MAX_DEFAULT_VALUE);
		assert(fbt_cpu_policy[i]->constraints.other_owner == 1234);
		assert(fbt_cpu_rq[i].active);          /* ownership kept */
	}
	assert(fbt_cur_ceiling[0] == 1500);            /* demand preserved */
	assert(!timer_armed && work_joined > before && wq_flushed > 0);
	assert(sjerk_cancelled == 1 && sjerk.arm_seq == 1);

	/* 2. nothing re-arms the sampler or queues work while suspended */
	enable_cpu_loading_timer();
	assert(!timer_armed);
	before = work_queued;
	assert(handle_cpu_loading_timeout(&cpu_ctrl_hrt) == HRTIMER_NORESTART);
	assert(work_queued == before);
	set_ceiling(1200, 1800, 2200);                 /* demand keeps updating */
	assert(fbt_cur_ceiling[0] == 1200);
	for (i = 0; i < MODEL_POLICIES; i++)
		assert(fbt_cpu_policy[i]->constraints.fbt_applied == FREQ_QOS_MAX_DEFAULT_VALUE);

	/* 3. a policy recreated while suspended attaches without a constraint */
	{
		struct cpufreq_policy *old = fbt_cpu_policy[1], *fresh;

		published[1] = NULL; old->published = false;
		assert(fbt_policy_event(NULL, 1 /* REMOVE */, old) == NOTIFY_OK);
		assert(old->refcount == 0 && old->constraints.max_requests == 0);
		free(old);
		fresh = make_policy(1);
		assert(fbt_policy_event(NULL, 0, fresh) == NOTIFY_OK);
		assert(fbt_cpu_policy[1] == fresh && fbt_cpu_rq[1].active);
		assert(fresh->constraints.fbt_applied == FREQ_QOS_MAX_DEFAULT_VALUE);
	}

	/* 4. suspend is idempotent */
	before = work_joined;
	pm(PM_SUSPEND_PREPARE);
	assert(ctrl_suspended && work_joined > before);  /* drains again, no state damage */
	assert(fbt_cur_ceiling[0] == 1200);

	/* 5. rescue armed before suspend is stale; armed after resume is live */
	{
		unsigned int armed_before = sjerk.pm_epoch;

		sjerk.pm_epoch = armed_before;
		assert(!fbt_pm_epoch_current(armed_before - 1));
		assert(fbt_pm_epoch_current(fbt_pm_epoch));
	}

	/* 6. a counter discontinuity across the sleep gap is a baseline, not a
	 * 100%-busy sample that would release every ceiling
	 */
	for (i = 0; i < MODEL_CPUS; i++) { model_wall[i] = 1000000; model_idle[i] = 900000; }
	mutex_lock(&cpu_ctrl_lock);
	update_cpu_loading_locked();
	update_cpu_loading_locked();
	mutex_unlock(&cpu_ctrl_lock);
	assert(cfp_cur_loading == 0);
	pm(PM_POST_SUSPEND);
	assert(!ctrl_suspended);
	assert(cfp_cur_loading == 0 && cfp_cur_up_time == 0 && cfp_cur_down_time == 0);
	for (i = 0; i < MODEL_CPUS; i++) {
		assert(cur_wall_time[i].time == 0 && prev_wall_time[i].time == 0);
		assert(cur_idle_time[i].time == 0 && prev_idle_time[i].time == 0);
	}
	/* the first post-resume sample sees a huge wall jump and only baselines */
	for (i = 0; i < MODEL_CPUS; i++) { model_wall[i] = 60000000000ULL; model_idle[i] = 900000; }
	mutex_lock(&cpu_ctrl_lock);
	update_cpu_loading_locked();
	mutex_unlock(&cpu_ctrl_lock);
	assert(cfp_cur_loading == 0);   /* no spurious heavy-load release */

	/* 7. resume reapplies the demand that is live now, not the pre-suspend one */
	assert(fbt_cpu_policy[0]->constraints.fbt_applied == 1200);
	assert(fbt_cpu_policy[1]->constraints.fbt_applied == 1800);
	assert(timer_armed);

	/* 8. renderer exit while suspended is not resurrected by resume */
	pm(PM_SUSPEND_PREPARE);
	set_ceiling(-1, -1, -1);
	pm(PM_POST_SUSPEND);
	for (i = 0; i < MODEL_POLICIES; i++)
		assert(fbt_cpu_policy[i]->constraints.fbt_applied == (int)fbt_max_freq[i]);

	/* 9. resume is idempotent and teardown wins over a late resume */
	pm(PM_POST_SUSPEND);
	assert(!ctrl_suspended);
	pm(PM_SUSPEND_PREPARE);
	ctrl_stopping = true;
	pm(PM_POST_SUSPEND);
	assert(!ctrl_suspended);
	ctrl_stopping = false;

	/* 10. unrelated PM events are ignored */
	assert(fbt_pm_event(NULL, 99, NULL) == NOTIFY_DONE);
	assert(lock_depth == 0);
	puts("PASS actual FPSGO/FBT suspend coordination: constraint withdrawal with "
	     "preserved demand, sampler quiesce and drain, attach while suspended, "
	     "rescue epoch retirement, sleep-gap rebaseline, live-demand resume, "
	     "renderer exit, idempotence and teardown precedence");
	return 0;
}
'''

c = a.output/'pm-suspend.c'
c.write_text(prefix + mid + suffix)
exe = a.output/'pm-suspend'
subprocess.run([os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g', '-pthread',
                '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
print('source guards verified: sjerk/jerk workers drop pre-suspend epochs; '
      'PM notifier registered and unregistered exactly once')
