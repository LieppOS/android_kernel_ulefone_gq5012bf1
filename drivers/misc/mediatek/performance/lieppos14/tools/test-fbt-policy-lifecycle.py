#!/usr/bin/env python3
"""Run the real FBT cpufreq policy lifecycle against a modeled freq_qos core.

Extracts the production attach/detach/notifier/update functions verbatim and
exercises removal, recreation, repeated cycles, concurrent boost updates,
renderer exit while detached, QoS failure injection, global FPSGO disable,
teardown and domain repartition.
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

src = (root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/src/fbt_cpu_ctrl.c').read_text()


def take(signature):
    m = re.search(r'^' + re.escape(signature) + r'\n\{', src, re.M)
    assert m, signature
    end, depth = m.end(), 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[m.start():end] + '\n'


wanted = [
    'static int fbt_ceiling_request_locked(unsigned int i,\n'
    '\t\t\t\t      const struct cpufreq_policy *policy)',
    'static void __update_cpu_freq_locked(void)',
    'static void fbt_detach_policy(unsigned int i)',
    'static int fbt_attach_policy_locked(unsigned int i, struct cpufreq_policy *policy)',
    'static int fbt_policy_index(const struct cpufreq_policy *policy)',
    'static int fbt_policy_event(struct notifier_block *nb, unsigned long event, void *data)',
    'int fbt_set_cpu_freq_ceiling(int num, int *freq)',
]
body = '\n'.join(take(s) for s in wanted)
# the production FBT_CEILING_UNAPPLIED definition, not a harness guess
unapplied = re.search(r'#define FBT_CEILING_UNAPPLIED\t\((-?\d+)\)', src)
assert unapplied, 'production sentinel missing'

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
#define FREQ_QOS_MIN_DEFAULT_VALUE 0
#define CPUFREQ_CREATE_POLICY 0
#define CPUFREQ_REMOVE_POLICY 1
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define FREQ_QOS_MIN 0
#define FREQ_QOS_MAX 1
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define clamp_t(t, v, lo, hi) ((t)((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v))))
#define pr_warn_ratelimited(...) ((void)0)
#define pr_info(...) ((void)0)
#define lockdep_assert_held(l) assert(lock_depth > 0)
#define DEBUG_LOG 0
'''
prefix += '#define FBT_CEILING_UNAPPLIED (' + unapplied.group(1) + ')\n'
prefix += r'''
struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); };

struct freq_constraints {
	bool alive;
	struct { int target_value; } min_freq;
	int max_requests;      /* live FREQ_QOS_MAX requests on this object */
	int min_notifiers;     /* live FREQ_QOS_MIN notifiers on this object */
	int applied_max;       /* aggregate of the single FBT owner */
};

struct cpufreq_policy {
	unsigned int cpu;
	unsigned int related_cpus;    /* bitmask, modeled cpumask */
	struct { unsigned int min_freq, max_freq; } cpuinfo;
	struct freq_constraints constraints;
	int refcount;
	bool published;
};

struct freq_qos_request {
	struct freq_constraints *qos;
	int type;
	int value;
	bool active;
};

static pthread_mutex_t ctrl_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int lock_depth;
#define mutex_lock(m) do { assert(!pthread_mutex_lock(&ctrl_mutex)); lock_depth++; } while (0)
#define mutex_unlock(m) do { lock_depth--; assert(!pthread_mutex_unlock(&ctrl_mutex)); } while (0)
static int cpu_ctrl_lock;

/* failure injection */
static atomic_int fail_add_request, fail_add_notifier;
static atomic_int attach_failures;

static bool freq_qos_request_active(struct freq_qos_request *r) { return r->active; }

static int freq_qos_add_request(struct freq_constraints *c, struct freq_qos_request *r,
				int type, int value)
{
	assert(c && c->alive);
	assert(!r->active);            /* double registration is a bug */
	if (atomic_fetch_sub(&fail_add_request, 0) > 0) {
		atomic_fetch_sub(&fail_add_request, 1);
		return -ENOMEM;
	}
	r->qos = c; r->type = type; r->value = value; r->active = true;
	c->max_requests++;
	c->applied_max = value;
	return 0;
}

static int freq_qos_remove_request(struct freq_qos_request *r)
{
	assert(r->active);             /* double removal is a bug */
	assert(r->qos && r->qos->alive);  /* never touch a destroyed policy */
	r->qos->max_requests--;
	r->qos->applied_max = FREQ_QOS_MAX_DEFAULT_VALUE;
	r->active = false; r->qos = NULL;
	return 0;
}

static int freq_qos_update_request(struct freq_qos_request *r, int value)
{
	assert(r->active && r->qos && r->qos->alive);
	if (r->value == value)
		return 0;
	r->value = value;
	r->qos->applied_max = value;
	return 1;
}

static int freq_qos_add_notifier(struct freq_constraints *c, int type, struct notifier_block *nb)
{
	assert(c && c->alive && nb->notifier_call);
	if (atomic_fetch_sub(&fail_add_notifier, 0) > 0) {
		atomic_fetch_sub(&fail_add_notifier, 1);
		return -ENOMEM;
	}
	c->min_notifiers++;
	return 0;
}

static int freq_qos_remove_notifier(struct freq_constraints *c, int type, struct notifier_block *nb)
{
	assert(c && c->alive && c->min_notifiers > 0);
	c->min_notifiers--;
	return 0;
}

/* modeled cpufreq core publication */
#define MODEL_POLICIES 3
static struct cpufreq_policy *published[MODEL_POLICIES];
static pthread_mutex_t publish_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct cpufreq_policy *cpufreq_cpu_get(int cpu)
{
	struct cpufreq_policy *p = NULL;
	int i;

	assert(!pthread_mutex_lock(&publish_mutex));
	for (i = 0; i < MODEL_POLICIES; i++)
		if (published[i] && published[i]->published &&
		    (published[i]->related_cpus & (1u << cpu))) {
			p = published[i];
			p->refcount++;
			break;
		}
	assert(!pthread_mutex_unlock(&publish_mutex));
	return p;
}

static void cpufreq_cpu_put(struct cpufreq_policy *p)
{
	assert(!pthread_mutex_lock(&publish_mutex));
	assert(p && p->refcount > 0);
	p->refcount--;
	assert(!pthread_mutex_unlock(&publish_mutex));
}

/* fpsgo immutable domain map */
static unsigned int domain_mask[MODEL_POLICIES] = { 0x0f, 0x70, 0x80 };
static bool fpsgo_cpu_policy_matches(unsigned int index, unsigned int mask)
{
	return index < MODEL_POLICIES && domain_mask[index] == mask;
}
static int fpsgo_get_cpu_policy_cpu(unsigned int index)
{
	unsigned int m = domain_mask[index];
	int cpu = 0;

	while (!(m & 1u)) { m >>= 1; cpu++; }
	return cpu;
}

static void __cpu_ctrl_freq_systrace(int policy, int freq) { (void)policy; (void)freq; }
typedef void (*cfp_notifier_fn_t)(int);
static int cfp_mon_enable(int kicker, cfp_notifier_fn_t cb) { (void)kicker; (void)cb; return 0; }
static int cfp_mon_disable(int kicker) { (void)kicker; return 0; }
#define CFP_KIR_FPSGO 0
static int freq_min_notifier_call(struct notifier_block *nb, unsigned long e, void *p)
{ (void)nb; (void)e; (void)p; return 0; }
static void fbt_cfp_notifier(int heavy) { (void)heavy; }

/* production module state */
static int policy_num = MODEL_POLICIES;
static bool ctrl_stopping, ctrl_ready = true, policy_table_live = true;
static bool ctrl_suspended;   /* temporary PM quiesce, not a disable */
static bool min_notifier_added_store[MODEL_POLICIES];
static bool *min_notifier_added = min_notifier_added_store;
static int fbt_opp_cnt_store[MODEL_POLICIES] = { 4, 4, 4 };
static unsigned int opp0[4] = { 2000, 1500, 1000, 500 };
static unsigned int opp1[4] = { 2600, 2000, 1400, 700 };
static unsigned int opp2[4] = { 3000, 2400, 1600, 800 };
static unsigned int *opp_store[MODEL_POLICIES] = { opp0, opp1, opp2 };
static int *fbt_opp_cnt = fbt_opp_cnt_store;
static unsigned int **fbt_opp_tbl = opp_store;
static struct cpufreq_policy *fbt_cpu_policy_store[MODEL_POLICIES];
static struct cpufreq_policy **fbt_cpu_policy = fbt_cpu_policy_store;
static struct freq_qos_request fbt_cpu_rq_store[MODEL_POLICIES];
static struct freq_qos_request *fbt_cpu_rq = fbt_cpu_rq_store;
static struct notifier_block fbt_freq_min_notifier_store[MODEL_POLICIES];
static struct notifier_block *fbt_freq_min_notifier = fbt_freq_min_notifier_store;
static unsigned int fbt_min_freq[MODEL_POLICIES] = { 500, 700, 800 };
static unsigned int fbt_max_freq[MODEL_POLICIES] = { 2000, 2600, 3000 };
static int fbt_cur_ceiling[MODEL_POLICIES] = { 2000, 2600, 3000 };
static int fbt_last_ceiling[MODEL_POLICIES] = { -1, -1, -1 };
static int fbt_final_ceiling[MODEL_POLICIES] = { -1, -1, -1 };
static int fbt_cur_floor[MODEL_POLICIES] = { 500, 700, 800 };
static int cfp_ceil_rel, cfp_onoff = 1;
static int L_min_cap_enable, L_ceiling_min;
static int ceiling_min_enable_EARA, ceiling_min_M_EARA, ceiling_min_B_EARA;
'''

suffix = r'''
/* ---- harness ---- */
static struct cpufreq_policy *make_policy(int idx, unsigned int hw_min, unsigned int hw_max)
{
	struct cpufreq_policy *p = calloc(1, sizeof(*p));

	assert(p);
	p->related_cpus = domain_mask[idx];
	p->cpu = fpsgo_get_cpu_policy_cpu(idx);
	p->cpuinfo.min_freq = hw_min;
	p->cpuinfo.max_freq = hw_max;
	p->constraints.alive = true;
	p->constraints.min_freq.target_value = hw_min;
	p->constraints.applied_max = FREQ_QOS_MAX_DEFAULT_VALUE;
	assert(!pthread_mutex_lock(&publish_mutex));
	published[idx] = p;
	p->published = true;
	assert(!pthread_mutex_unlock(&publish_mutex));
	return p;
}

static void create_event(struct cpufreq_policy *p)
{
	assert(fbt_policy_event(NULL, CPUFREQ_CREATE_POLICY, p) == NOTIFY_OK);
}

static void remove_and_free(int idx, struct cpufreq_policy *p)
{
	assert(!pthread_mutex_lock(&publish_mutex));
	published[idx] = NULL;
	p->published = false;
	assert(!pthread_mutex_unlock(&publish_mutex));
	assert(fbt_policy_event(NULL, CPUFREQ_REMOVE_POLICY, p) == NOTIFY_OK);
	/* the cpufreq core frees the policy right after the chain returns */
	assert(p->refcount == 0);          /* no stale reference retained */
	/* no request or notifier left on constraints about to be destroyed */
	assert(p->constraints.max_requests == 0);
	assert(p->constraints.min_notifiers == 0);
	p->constraints.alive = false;
	free(p);
}

static void set_ceiling(int c0, int c1, int c2)
{
	int f[MODEL_POLICIES] = { c0, c1, c2 };

	assert(fbt_set_cpu_freq_ceiling(MODEL_POLICIES, f) == 0);
}

static atomic_int stop_flag;
static void *boost_thread(void *arg)
{
	long i = 0;

	(void)arg;
	while (!atomic_load(&stop_flag)) {
		int v = 1000 + (int)(i % 900);

		set_ceiling(v, v + 100, v + 200);
		i++;
	}
	return NULL;
}

int main(void)
{
	struct cpufreq_policy *p1, *p2, *odd;
	int i;

	/* 1. initial attach for every domain */
	for (i = 0; i < MODEL_POLICIES; i++) {
		struct cpufreq_policy *p = make_policy(i, fbt_min_freq[i], fbt_max_freq[i]);

		create_event(p);
		assert(fbt_cpu_policy[i] == p && p->refcount == 1);
		assert(p->constraints.max_requests == 1 && p->constraints.min_notifiers == 1);
	}

	/* a live FBT ceiling demand */
	set_ceiling(1500, 2000, 2400);
	assert(fbt_cpu_policy[0]->constraints.applied_max == 1500);
	assert(fbt_cpu_policy[2]->constraints.applied_max == 2400);

	/* 2. remove -> create: the live ceiling is reattached, not lost */
	p1 = fbt_cpu_policy[0];
	remove_and_free(0, p1);
	assert(fbt_cpu_policy[0] == NULL && !fbt_cpu_rq[0].active);
	assert(fbt_final_ceiling[0] == FBT_CEILING_UNAPPLIED);
	p2 = make_policy(0, fbt_min_freq[0], fbt_max_freq[0]);
	create_event(p2);
	assert(fbt_cpu_policy[0] == p2 && p2->refcount == 1);
	assert(p2->constraints.applied_max == 1500);   /* boost restored */
	assert(fbt_final_ceiling[0] == 1500);
	assert(p2->constraints.min_notifiers == 1);

	/* a second CREATE for an attached domain must not register twice */
	create_event(p2);
	assert(p2->refcount == 1 && p2->constraints.max_requests == 1 &&
	       p2->constraints.min_notifiers == 1);

	/* a REMOVE for a policy we never attached must not detach anything */
	odd = calloc(1, sizeof(*odd));
	assert(odd);
	odd->constraints.alive = true;
	assert(fbt_policy_event(NULL, CPUFREQ_REMOVE_POLICY, odd) == NOTIFY_OK);
	assert(fbt_cpu_policy[0] == p2 && fbt_cpu_rq[0].active);
	free(odd);

	/* 3. repeated remove/create cycles keep ownership balanced */
	for (i = 0; i < 64; i++) {
		struct cpufreq_policy *cur = fbt_cpu_policy[1];

		remove_and_free(1, cur);
		cur = make_policy(1, fbt_min_freq[1], fbt_max_freq[1]);
		create_event(cur);
		assert(fbt_cpu_policy[1] == cur && cur->refcount == 1);
		assert(cur->constraints.applied_max == 2000);
		assert(cur->constraints.max_requests == 1 && cur->constraints.min_notifiers == 1);
	}

	/* 4. renderer exit while detached: no resurrection of its boost */
	p1 = fbt_cpu_policy[2];
	remove_and_free(2, p1);
	set_ceiling(1500, 2000, -1);     /* renderer released cluster 2 */
	p2 = make_policy(2, fbt_min_freq[2], fbt_max_freq[2]);
	create_event(p2);
	assert(fbt_cpu_policy[2] == p2);
	assert(p2->constraints.applied_max == (int)fbt_max_freq[2]); /* no cap */
	assert(fbt_final_ceiling[2] != 2400);

	/* 5. QoS request failure: stay detached, leak nothing, recover later */
	p1 = fbt_cpu_policy[2];
	remove_and_free(2, p1);
	atomic_store(&fail_add_request, 1);
	p2 = make_policy(2, fbt_min_freq[2], fbt_max_freq[2]);
	create_event(p2);
	assert(fbt_cpu_policy[2] == NULL && !fbt_cpu_rq[2].active);
	assert(!min_notifier_added[2] && p2->refcount == 0);
	assert(p2->constraints.max_requests == 0 && p2->constraints.min_notifiers == 0);
	/* notifier failure rolls the request back */
	atomic_store(&fail_add_notifier, 1);
	create_event(p2);
	assert(fbt_cpu_policy[2] == NULL && !fbt_cpu_rq[2].active);
	assert(p2->refcount == 0 && p2->constraints.max_requests == 0 &&
	       p2->constraints.min_notifiers == 0);
	create_event(p2);                 /* no injection left: recovers */
	assert(fbt_cpu_policy[2] == p2 && p2->refcount == 1);

	/* 6. global FPSGO disable during recreation: still reattached (the
	 * subsystem is only quiesced), but nothing is pushed while disabled.
	 */
	p1 = fbt_cpu_policy[0];
	remove_and_free(0, p1);
	ctrl_ready = false;
	p2 = make_policy(0, fbt_min_freq[0], fbt_max_freq[0]);
	create_event(p2);
	assert(fbt_cpu_policy[0] == p2 && fbt_cpu_rq[0].active);
	ctrl_ready = true;

	/* teardown in progress: a created policy must NOT be attached */
	p1 = fbt_cpu_policy[0];
	remove_and_free(0, p1);
	policy_table_live = false;
	p2 = make_policy(0, fbt_min_freq[0], fbt_max_freq[0]);
	create_event(p2);
	assert(fbt_cpu_policy[0] == NULL && !fbt_cpu_rq[0].active && p2->refcount == 0);
	policy_table_live = true;
	create_event(p2);
	assert(fbt_cpu_policy[0] == p2);

	/* 7. repartitioned domain: never inherits another domain's state */
	p1 = fbt_cpu_policy[0];
	remove_and_free(0, p1);
	odd = calloc(1, sizeof(*odd));
	assert(odd);
	odd->related_cpus = 0x03;        /* split of domain 0 */
	odd->cpu = 0;
	odd->cpuinfo.min_freq = 500;
	odd->cpuinfo.max_freq = 2000;
	odd->constraints.alive = true;
	assert(!pthread_mutex_lock(&publish_mutex));
	published[0] = odd; odd->published = true;
	assert(!pthread_mutex_unlock(&publish_mutex));
	create_event(odd);
	assert(fbt_cpu_policy[0] == NULL && odd->refcount == 0);
	assert(odd->constraints.max_requests == 0 && odd->constraints.min_notifiers == 0);
	assert(!pthread_mutex_lock(&publish_mutex));
	published[0] = NULL; odd->published = false;
	assert(!pthread_mutex_unlock(&publish_mutex));
	free(odd);
	p2 = make_policy(0, fbt_min_freq[0], fbt_max_freq[0]);
	create_event(p2);
	assert(fbt_cpu_policy[0] == p2);

	/* 8. hardware range change on recreation: the owned request is
	 * revalidated instead of replaying an out-of-range cap.
	 */
	set_ceiling(1900, 2000, 2400);
	p1 = fbt_cpu_policy[0];
	remove_and_free(0, p1);
	p2 = make_policy(0, 400, 1200);  /* new driver, lower range */
	create_event(p2);
	assert(fbt_cpu_policy[0] == p2);
	assert(p2->constraints.applied_max == 1200);
	assert(fbt_final_ceiling[0] == 1200);

	/* 9. concurrent boost updates across repeated recreation */
	{
		pthread_t t;

		atomic_store(&stop_flag, 0);
		assert(!pthread_create(&t, NULL, boost_thread, NULL));
		for (i = 0; i < 200; i++) {
			struct cpufreq_policy *cur = fbt_cpu_policy[1];

			remove_and_free(1, cur);
			cur = make_policy(1, fbt_min_freq[1], fbt_max_freq[1]);
			create_event(cur);
			assert(fbt_cpu_policy[1] == cur && cur->refcount == 1);
			assert(cur->constraints.max_requests == 1);
			assert(cur->constraints.min_notifiers == 1);
		}
		atomic_store(&stop_flag, 1);
		assert(!pthread_join(t, NULL));
	}
	for (i = 0; i < MODEL_POLICIES; i++) {
		assert(fbt_cpu_policy[i]);
		assert(fbt_cpu_policy[i]->refcount == 1);
		assert(fbt_cpu_rq[i].active && min_notifier_added[i]);
		/* whatever the last concurrent update asked for is what the
		 * live policy carries: no detached-era value survives
		 */
		assert(fbt_cpu_policy[i]->constraints.applied_max ==
		       (fbt_final_ceiling[i] ? fbt_final_ceiling[i] : FREQ_QOS_MAX_DEFAULT_VALUE));
	}
	assert(lock_depth == 0);
	puts("PASS actual FBT policy lifecycle: recreation reattach, repeated cycles, "
	     "renderer exit, QoS failure rollback, disable/teardown, repartition, "
	     "hardware-range revalidation and concurrent boost updates");
	return 0;
}
'''

c = a.output/'fbt-policy-lifecycle.c'
c.write_text(prefix + body + suffix)
exe = a.output/'fbt-policy-lifecycle'
subprocess.run([os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g', '-pthread',
                '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
