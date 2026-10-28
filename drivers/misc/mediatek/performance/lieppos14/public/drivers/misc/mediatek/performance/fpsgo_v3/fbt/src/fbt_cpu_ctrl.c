// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) "[fbt_cpu_ctrl]"fmt

#define DEBUG_LOG	0

#include <linux/slab.h>
#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include "../../mt-plat/fpsgo_common.h"
#include "../../base/include/fpsgo_base.h"
#include "../../base/include/fpsgo_cpu_policy.h"
#include "../include/fbt_cpu_ctrl.h"

/*--------------------------------------------*/

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* No ceiling is applied to this domain: not a frequency, and never equal to
 * a value the update path could compute.
 */
#define FBT_CEILING_UNAPPLIED	(-1)


struct FBT_CPU_CTRL_NOTIFIER_PUSH_TAG {
	int policy_id;
	unsigned int freq;
	struct work_struct sWork;
};

struct cpu_info {
	u64 time;
};

struct cfp_req {
	int enabled;
	cfp_notifier_fn_t cb;
};

struct cfp_req cfp_req_tbl[CFP_KIR_MAX_NUM];

/* Configurable */
struct workqueue_struct *g_psFbtCpuCtrlWorkQueue;

struct cpufreq_policy **fbt_cpu_policy;
struct freq_qos_request *fbt_cpu_rq;

static int policy_num;
static bool ctrl_stopping = true;
static bool ctrl_ready;
/* Temporary system-suspend quiesce. Unlike ctrl_stopping, no resource is
 * released and no state is forgotten: the ceiling demand keeps being tracked
 * so it can be reapplied verbatim on resume.
 */
static bool ctrl_suspended;
static bool policy_notifier_registered;
/* Published while the per-policy tables exist, independently of ctrl_ready:
 * a policy recreated during init or while FPSGO is globally disabled must
 * still be reattached, but one recreated during teardown must not be.
 */
static bool policy_table_live;
static bool *min_notifier_added;
static int *fbt_opp_cnt;
static unsigned int **fbt_opp_tbl;
static int fbt_opp_policy_num;
static unsigned int *fbt_min_freq;
static unsigned int *fbt_max_freq;
static int *fbt_cur_ceiling;
static int *fbt_last_ceiling;
static int *fbt_final_ceiling;
static int *fbt_cur_floor;

static DEFINE_MUTEX(cpu_ctrl_lock);

static int cfp_onoff;
static int cfp_polling_ms;
static int cfp_up_time;
static int cfp_down_time;
static int cfp_up_loading;
static int cfp_down_loading;
static int L_ceiling_min;
static int L_min_cap_enable;
static int ceiling_min_B_EARA;
static int ceiling_min_M_EARA;
static int ceiling_min_enable_EARA;


module_param(cfp_onoff, int, 0644);
module_param(cfp_polling_ms, int, 0644);
module_param(cfp_up_time, int, 0644);
module_param(cfp_down_time, int, 0644);
module_param(cfp_up_loading, int, 0644);
module_param(cfp_down_loading, int, 0644);
module_param(L_ceiling_min, int, 0644);
module_param(L_min_cap_enable, int, 0644);
module_param(ceiling_min_B_EARA, int, 0644);
module_param(ceiling_min_M_EARA, int, 0644);
module_param(ceiling_min_enable_EARA, int, 0644);


static int cfp_cur_up_time;
static int cfp_cur_down_time;
static int cfp_cur_loading; /*record previous cpu loading*/
static int cfp_ceil_rel; /* release ceiling */
static int cfp_enable;

static struct workqueue_struct *g_psCpuLoadingWorkQueue;
static struct hrtimer cpu_ctrl_hrt;
static void notify_cpu_loading_timeout(struct work_struct *work);
static DECLARE_WORK(cpu_loading_timeout_work,
		(void *) notify_cpu_loading_timeout);/*statically create work */

/* cpu loading tracking */
static struct cpu_info *cur_wall_time, *cur_idle_time,
		       *prev_wall_time, *prev_idle_time;


/*--------------------INIT------------------------*/
/* local function */
static void __cpu_ctrl_freq_systrace(int policy, int freq)
{
	if (policy == 0) {
		__cpu_ctrl_systrace(freq, "cpu_ceil_cluster_0");
		__cpu_ctrl_systrace(L_min_cap_enable ? L_ceiling_min : -1,
			"L_ceiling_min");
		if (policy_num == 2) {
			__cpu_ctrl_systrace(ceiling_min_enable_EARA ? ceiling_min_M_EARA : -1,
			"ceiling_min_M_EARA");
		}
	} else if (policy == 1) {
		__cpu_ctrl_systrace(freq, "cpu_ceil_cluster_1");
		if (policy_num == 2) {
			__cpu_ctrl_systrace(ceiling_min_enable_EARA ? ceiling_min_B_EARA : -1,
			"ceiling_min_B_EARA");
		} else {
			__cpu_ctrl_systrace(ceiling_min_enable_EARA ? ceiling_min_M_EARA : -1,
			"ceiling_min_M_EARA");
		}
	} else if (policy == 2) {
		__cpu_ctrl_systrace(freq, "cpu_ceil_cluster_2");
		__cpu_ctrl_systrace(ceiling_min_enable_EARA ? ceiling_min_B_EARA : -1,
			"ceiling_min_B_EARA");
	}
}

/* The live FBT ceiling demand for a domain, in freq_qos terms, revalidated
 * against the hardware range of the policy the request is attached to. A
 * released ceiling owns FREQ_QOS_MAX_DEFAULT_VALUE, not a numeric cap. The
 * demand itself is maintained even while the domain is detached, so it always
 * reflects the current renderers rather than a value cached before detachment,
 * while the range revalidation follows the live policy: a recreated domain may
 * be driven by a different driver or OPP range than the one sampled at init.
 */
static int fbt_ceiling_request_locked(unsigned int i,
				      const struct cpufreq_policy *policy)
{
	int value;

	/* Suspended: hold no frequency constraint on behalf of renderers that
	 * are frozen, while keeping the demand itself recorded.
	 */
	if (READ_ONCE(ctrl_suspended))
		return FREQ_QOS_MAX_DEFAULT_VALUE;
	if (cfp_ceil_rel != 0 && cfp_onoff != 0)
		return FREQ_QOS_MAX_DEFAULT_VALUE;
	value = fbt_cur_ceiling[i] < fbt_cur_floor[i] ?
		fbt_cur_floor[i] : fbt_cur_ceiling[i];
	if (!policy->cpuinfo.min_freq || !policy->cpuinfo.max_freq ||
	    policy->cpuinfo.min_freq > policy->cpuinfo.max_freq)
		return FREQ_QOS_MAX_DEFAULT_VALUE;
	return clamp_t(int, value, (int)policy->cpuinfo.min_freq,
		       (int)policy->cpuinfo.max_freq);
}

static void __update_cpu_freq_locked(void)
{
	int i, ceiling_to_set;

	for (i = 0; i < policy_num; i++) {
		if (!fbt_cpu_policy[i] || !freq_qos_request_active(&fbt_cpu_rq[i]))
			continue;
#if DEBUG_LOG
		pr_info("%s, i:%d, min:%d, max:%d, cur_min:%d, cur_max:%d,
		final:%d\n", __func__, i, fbt_min_freq[i], fbt_max_freq[i],
		fbt_cur_floor[i], fbt_cur_ceiling[i], fbt_final_ceiling[i]);
#endif

		ceiling_to_set = fbt_ceiling_request_locked(i, fbt_cpu_policy[i]);
		if (ceiling_to_set != FREQ_QOS_MAX_DEFAULT_VALUE) {

			if (fbt_final_ceiling[i] != ceiling_to_set) {
				int ret = freq_qos_update_request(&fbt_cpu_rq[i], ceiling_to_set);

				if (ret < 0) {
					pr_warn_ratelimited("QoS update policy %d failed: %d\n", i, ret);
					continue;
				}
				fbt_final_ceiling[i] = ceiling_to_set;
				__cpu_ctrl_freq_systrace(i, fbt_final_ceiling[i]);
			}
		} else {
			int ret = freq_qos_update_request(&fbt_cpu_rq[i], FREQ_QOS_MAX_DEFAULT_VALUE);

			if (ret < 0) {
				pr_warn_ratelimited("QoS release policy %d failed: %d\n", i, ret);
				continue;
			}
			fbt_final_ceiling[i] = 0;
			__cpu_ctrl_freq_systrace(i, fbt_max_freq[i]);
		}
	}
}

/*hrtimer trigger*/
static void enable_cpu_loading_timer(void)
{
	ktime_t ktime;

	if (READ_ONCE(ctrl_stopping) || !READ_ONCE(ctrl_ready) ||
	    READ_ONCE(ctrl_suspended) ||
	    !READ_ONCE(cfp_enable) || !READ_ONCE(cfp_onoff) || policy_num == 0)
		return;

	ktime = ms_to_ktime(clamp(cfp_polling_ms, 1, 10000));
	hrtimer_start(&cpu_ctrl_hrt, ktime, HRTIMER_MODE_REL);
}

/*close hrtimer*/
static void disable_cpu_loading_timer(void)
{
	hrtimer_cancel(&cpu_ctrl_hrt);
}

static enum hrtimer_restart handle_cpu_loading_timeout(struct hrtimer *timer)
{
	if (!READ_ONCE(ctrl_stopping) && !READ_ONCE(ctrl_suspended) &&
	    g_psCpuLoadingWorkQueue)
		queue_work(g_psCpuLoadingWorkQueue, &cpu_loading_timeout_work);
	return HRTIMER_NORESTART;
}

/*update info*/
static int update_cpu_loading_locked(void)
{
	int i, tmp_cpu_loading = 0;

	u64 wall_time = 0, idle_time = 0;

	for_each_possible_cpu(i) {
		u64 wall_delta, idle_delta;

		prev_wall_time[i].time = cur_wall_time[i].time;
		prev_idle_time[i].time = cur_idle_time[i].time;
		/* Best-effort metric, not a topology transaction. Per-CPU idle
		 * storage survives hotplug; never take cpus_read_lock under
		 * cpu_ctrl_lock (REMOVE_POLICY takes these in the opposite order).
		 */
		if (!cpu_active(i)) {
			cur_wall_time[i].time = cur_idle_time[i].time = 0;
			continue;
		}
		/* Idle time includes iowait. Unsupported accounting is -1ULL. */
		cur_idle_time[i].time = get_cpu_idle_time(i,
				&cur_wall_time[i].time, 1);
		if (!cpu_active(i) || cur_idle_time[i].time == -1ULL) {
			cur_wall_time[i].time = cur_idle_time[i].time = 0;
			continue;
		}
		/* First sample after offline/reset is only a baseline. Regressing
		 * counters must not underflow into an enormous busy interval.
		 */
		if (!prev_wall_time[i].time ||
			cur_wall_time[i].time < prev_wall_time[i].time ||
			cur_idle_time[i].time < prev_idle_time[i].time)
			continue;
		wall_delta = cur_wall_time[i].time - prev_wall_time[i].time;
		idle_delta = cur_idle_time[i].time - prev_idle_time[i].time;
		if (idle_delta > wall_delta)
			continue;
		wall_time += wall_delta;
		idle_time += idle_delta;
	}

	if (wall_time > 0 && wall_time > idle_time)
		tmp_cpu_loading = mul_u64_u64_div_u64(wall_time - idle_time,
			100, wall_time);

	cfp_cur_loading = tmp_cpu_loading;

#if DEBUG_LOG
	pr_info("%s loading:%d\n", __func__, cfp_cur_loading);
#endif
	__cpu_ctrl_systrace(cfp_cur_loading, "cfp_loading");
	return 0;
}

static void handle_cfp_callback(int heavy)
{
	int i;

	for (i = 0; i < CFP_KIR_MAX_NUM; i++)
		if (cfp_req_tbl[i].enabled && cfp_req_tbl[i].cb)
			cfp_req_tbl[i].cb(heavy);
}

static void update_cfp_policy_locked(void)
{
	if (cfp_cur_loading >= cfp_up_loading) {
		cfp_cur_down_time = 0;
		cfp_cur_up_time =
			MIN(cfp_cur_up_time + 1, cfp_up_time);

		if (cfp_cur_up_time >= cfp_up_time) {
			if (!cfp_ceil_rel) {
				cfp_ceil_rel = 1;
				__cpu_ctrl_systrace(1, "cfp_ceil_rel");
				handle_cfp_callback(1);
			}
		}

	} else if (cfp_cur_loading < cfp_down_loading) {
		cfp_cur_up_time = 0;
		cfp_cur_down_time =
			MIN(cfp_cur_down_time + 1, cfp_down_time);

		if (cfp_cur_down_time >= cfp_down_time) {
			if (cfp_ceil_rel) {
				cfp_ceil_rel = 0;
				__cpu_ctrl_systrace(0, "cfp_ceil_rel");
				handle_cfp_callback(0);
			}
		}
	} else {
		cfp_cur_up_time = 0;
		cfp_cur_down_time = 0;
	}
}

static void notify_cpu_loading_timeout(struct work_struct *work)
{
#if DEBUG_LOG
	pr_info("%s\n", __func__);
#endif

	mutex_lock(&cpu_ctrl_lock);
	if (!ctrl_stopping && ctrl_ready && cfp_enable) {
		update_cpu_loading_locked();
		update_cfp_policy_locked();
	}
	mutex_unlock(&cpu_ctrl_lock);
	enable_cpu_loading_timer();
}

static void update_cfp_ctrl_locked(int kicker)
{
	int i, action = 0;

	for (i = 0; i < CFP_KIR_MAX_NUM; i++) {
		if (cfp_req_tbl[i].enabled) {
			action = 1;
			break;
		}
	}

	if (cfp_enable == action)
		return;

	cfp_enable = action;

	if (action)
		enable_cpu_loading_timer();
	else
		disable_cpu_loading_timer();
}

int cfp_mon_enable(int kicker, cfp_notifier_fn_t cb)
{
	if (kicker < 0 || kicker >= CFP_KIR_MAX_NUM)
		return -EFAULT;

	mutex_lock(&cpu_ctrl_lock);

	if (cfp_req_tbl[kicker].enabled)
		goto ERR_EXIT;

	cfp_req_tbl[kicker].enabled = 1;
	cfp_req_tbl[kicker].cb = cb;
	update_cfp_ctrl_locked(kicker);

ERR_EXIT:
	mutex_unlock(&cpu_ctrl_lock);
	return 0;
}

int cfp_mon_disable(int kicker)
{
	if (kicker < 0 || kicker >= CFP_KIR_MAX_NUM)
		return -EFAULT;

	mutex_lock(&cpu_ctrl_lock);

	if (cfp_req_tbl[kicker].enabled == 0)
		goto ERR_EXIT;

	cfp_req_tbl[kicker].enabled = 0;
	cfp_req_tbl[kicker].cb = NULL;
	update_cfp_ctrl_locked(kicker);

ERR_EXIT:
	mutex_unlock(&cpu_ctrl_lock);
	return 0;
}

static void fbt_cpu_ctrl_notifier_wq_cb(struct work_struct *psWork)
{
	int id;

	struct FBT_CPU_CTRL_NOTIFIER_PUSH_TAG *vpPush =
		FPSGO_CONTAINER_OF(psWork,
				struct FBT_CPU_CTRL_NOTIFIER_PUSH_TAG, sWork);

	if (!vpPush)
		return;

#if DEBUG_LOG
	pr_info("%s, policy:%d, freq:%d", __func__, vpPush->policy_id, vpPush->freq);
#endif

	id = vpPush->policy_id;

	if (id < 0) {
		kfree(vpPush);
		return;
	}

	mutex_lock(&cpu_ctrl_lock);

	if (!ctrl_stopping && ctrl_ready && id < policy_num) {

		if (vpPush->freq > fbt_max_freq[id])
			fbt_cur_floor[id] = fbt_max_freq[id];
		else if (vpPush->freq < fbt_min_freq[id])
			fbt_cur_floor[id] = fbt_min_freq[id];
		else
			fbt_cur_floor[id] = vpPush->freq;
	}

	if (!ctrl_stopping && ctrl_ready)
		__update_cpu_freq_locked();

	mutex_unlock(&cpu_ctrl_lock);

	fpsgo_free(vpPush, sizeof(struct FBT_CPU_CTRL_NOTIFIER_PUSH_TAG));
}

static struct notifier_block *fbt_freq_min_notifier;

static int freq_min_notifier_call(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	int i;
	struct FBT_CPU_CTRL_NOTIFIER_PUSH_TAG *vpPush;
#if DEBUG_LOG
	pr_info("%s, event:%d", __func__, event);
#endif

	if (READ_ONCE(ctrl_stopping) || !READ_ONCE(ctrl_ready) || !g_psFbtCpuCtrlWorkQueue)
		return NOTIFY_DONE;

	vpPush = (struct FBT_CPU_CTRL_NOTIFIER_PUSH_TAG *)
		fpsgo_alloc_atomic(sizeof(struct FBT_CPU_CTRL_NOTIFIER_PUSH_TAG));

	if (!vpPush)
		return NOTIFY_DONE;

	vpPush->policy_id = -1;

	for (i = 0; i < policy_num; i++) {
		if (this == fbt_freq_min_notifier+i) {
#if DEBUG_LOG
			pr_info("%s, policy:%d", __func__, i);
#endif
			vpPush->policy_id = i;
			vpPush->freq = event;
		}
	}

	INIT_WORK(&vpPush->sWork, fbt_cpu_ctrl_notifier_wq_cb);
	queue_work(g_psFbtCpuCtrlWorkQueue, &vpPush->sWork);

	return NOTIFY_DONE;
}

/* cpu_ctrl_lock serializes policy detachment against request updates. */
static void fbt_detach_policy(unsigned int i)
{
	struct cpufreq_policy *policy = fbt_cpu_policy[i];

	if (!policy)
		return;
	if (min_notifier_added[i]) {
		freq_qos_remove_notifier(&policy->constraints, FREQ_QOS_MIN,
					&fbt_freq_min_notifier[i]);
		min_notifier_added[i] = false;
	}
	if (freq_qos_request_active(&fbt_cpu_rq[i]))
		freq_qos_remove_request(&fbt_cpu_rq[i]);
	fbt_cpu_policy[i] = NULL;
	/* Nothing is applied to a detached domain. Keeping the last applied
	 * value here would make a later reattach believe the new request
	 * already carries it and skip the update.
	 */
	fbt_final_ceiling[i] = FBT_CEILING_UNAPPLIED;
	cpufreq_cpu_put(policy);
}

/*
 * Attach the FBT-owned ceiling request and minimum-constraint notifier to a
 * freshly created policy for domain @i. Takes ownership of the caller's
 * policy reference on success and releases nothing the caller still owns on
 * failure, where no partial state is published.
 *
 * Context: the cpufreq policy notifier chain is blocking, so sleeping
 * allocations inside freq_qos are permitted; the CPU hotplug lock is already
 * held by the cpufreq core caller and must never be taken here.
 */
static int fbt_attach_policy_locked(unsigned int i, struct cpufreq_policy *policy)
{
	int value, ret;

	lockdep_assert_held(&cpu_ctrl_lock);
	if (fbt_cpu_policy[i])
		return fbt_cpu_policy[i] == policy ? 0 : -EBUSY;
	if (!fbt_opp_cnt || !fbt_opp_tbl || fbt_opp_cnt[i] <= 0 || !fbt_opp_tbl[i])
		return -EINVAL;
	if (min_notifier_added[i] || freq_qos_request_active(&fbt_cpu_rq[i]))
		return -EBUSY;

	/* Reattach the live demand only, revalidated against the new policy's
	 * hardware range instead of replaying a cached index.
	 */
	value = fbt_ceiling_request_locked(i, policy);

	fbt_freq_min_notifier[i].notifier_call = freq_min_notifier_call;
	ret = freq_qos_add_request(&policy->constraints, &fbt_cpu_rq[i],
				   FREQ_QOS_MAX, value);
	if (ret < 0)
		return ret;
	ret = freq_qos_add_notifier(&policy->constraints, FREQ_QOS_MIN,
				    &fbt_freq_min_notifier[i]);
	if (ret < 0) {
		freq_qos_remove_request(&fbt_cpu_rq[i]);
		return ret;
	}
	min_notifier_added[i] = true;
	fbt_cpu_policy[i] = policy; /* reference transferred, released on detach */
	/* The minimum-constraint notifier was absent while detached, so the
	 * cached floor is stale: resample the new constraint's target.
	 */
	fbt_cur_floor[i] = READ_ONCE(policy->constraints.min_freq.target_value);
	fbt_final_ceiling[i] = value == FREQ_QOS_MAX_DEFAULT_VALUE ? 0 : value;
	return 0;
}

/* Identify the FBT domain of a created policy. Exact related-CPU equality:
 * a repartitioned domain is not this domain and must not inherit its
 * ceiling, OPP table or floor.
 */
static int fbt_policy_index(const struct cpufreq_policy *policy)
{
	int i;

	for (i = 0; i < policy_num; i++)
		if (fpsgo_cpu_policy_matches(i, policy->related_cpus))
			return i;
	return -ENODEV;
}

static int fbt_policy_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	struct cpufreq_policy *owned;
	int i, index, ret = 0;

	if (!policy)
		return NOTIFY_DONE;

	if (event == CPUFREQ_REMOVE_POLICY) {
		mutex_lock(&cpu_ctrl_lock);
		if (fbt_cpu_policy && fbt_cpu_rq && min_notifier_added &&
		    fbt_freq_min_notifier)
			for (i = 0; i < policy_num; i++)
				if (fbt_cpu_policy[i] == policy)
					fbt_detach_policy(i);
		mutex_unlock(&cpu_ctrl_lock);
		/* The policy object is freed after this chain completes: never
		 * keep a pointer, a request or a notifier on it.
		 */
		return NOTIFY_OK;
	}

	if (event != CPUFREQ_CREATE_POLICY)
		return NOTIFY_DONE;

	/* cpufreq publishes the per-CPU policy and adds its own min/max
	 * requests before sending CREATE_POLICY, so the domain is already
	 * resolvable and a reference can be acquired through the normal API
	 * rather than by trusting the notifier argument.
	 */
	mutex_lock(&cpu_ctrl_lock);
	if (!READ_ONCE(policy_table_live) || !fbt_cpu_policy || !fbt_cpu_rq ||
	    !min_notifier_added || !fbt_freq_min_notifier)
		goto out;
	index = fbt_policy_index(policy);
	if (index < 0) {
		ret = index;
		goto out;
	}
	if (fbt_cpu_policy[index])
		goto out; /* already attached: never register a second time */
	owned = cpufreq_cpu_get(policy->cpu);
	if (owned != policy) {
		if (owned)
			cpufreq_cpu_put(owned);
		ret = -ENODEV;
		goto out;
	}
	ret = fbt_attach_policy_locked(index, owned);
	if (ret < 0)
		cpufreq_cpu_put(owned);
	else if (!READ_ONCE(ctrl_stopping) && READ_ONCE(ctrl_ready))
		__update_cpu_freq_locked();
out:
	mutex_unlock(&cpu_ctrl_lock);
	if (ret < 0 && ret != -ENODEV)
		pr_warn_ratelimited("policy attach failed for CPU%u: %d\n",
				    policy->cpu, ret);
	/* Never veto the cpufreq core lifecycle: an unattached domain simply
	 * carries no FBT ceiling until it is recreated again.
	 */
	return NOTIFY_OK;
}

static struct notifier_block fbt_policy_notifier = {
	.notifier_call = fbt_policy_event,
};

static int fbt_cpu_topo_info(void)
{
	int ret, cpu, i;
	struct cpufreq_policy *policy;

	policy_num = fpsgo_get_cpu_policy_num();
	if (policy_num <= 0)
		return -ENODEV;
	if (fpsgo_get_cpu_opp_info(&fbt_opp_cnt, &fbt_opp_tbl) < 0)
		return -ENODEV;

	fbt_min_freq = kcalloc(policy_num, sizeof(*fbt_min_freq), GFP_KERNEL);
	fbt_max_freq = kcalloc(policy_num, sizeof(*fbt_max_freq), GFP_KERNEL);
	fbt_cpu_policy = kcalloc(policy_num, sizeof(*fbt_cpu_policy), GFP_KERNEL);
	fbt_cpu_rq = kcalloc(policy_num, sizeof(*fbt_cpu_rq), GFP_KERNEL);
	fbt_freq_min_notifier = kcalloc(policy_num, sizeof(*fbt_freq_min_notifier), GFP_KERNEL);
	min_notifier_added = kcalloc(policy_num, sizeof(*min_notifier_added), GFP_KERNEL);
	fbt_cur_ceiling = kcalloc(policy_num, sizeof(*fbt_cur_ceiling), GFP_KERNEL);
	fbt_last_ceiling = kcalloc(policy_num, sizeof(*fbt_last_ceiling), GFP_KERNEL);
	fbt_final_ceiling = kcalloc(policy_num, sizeof(*fbt_final_ceiling), GFP_KERNEL);
	fbt_cur_floor = kcalloc(policy_num, sizeof(*fbt_cur_floor), GFP_KERNEL);
	if (!fbt_min_freq || !fbt_max_freq || !fbt_cpu_policy || !fbt_cpu_rq ||
	    !fbt_freq_min_notifier || !min_notifier_added || !fbt_cur_ceiling ||
	    !fbt_last_ceiling || !fbt_final_ceiling || !fbt_cur_floor)
		return -ENOMEM;

	fbt_opp_policy_num = policy_num; /* owned snapshot extent, freed on exit */
	for (i = 0; i < policy_num; i++) {
		if (fbt_opp_cnt[i] <= 0 || !fbt_opp_tbl[i])
			return -EINVAL;
		fbt_max_freq[i] = fbt_opp_tbl[i][0];
		fbt_min_freq[i] = fbt_opp_tbl[i][fbt_opp_cnt[i] - 1];
		fbt_cur_ceiling[i] = fbt_max_freq[i];
		fbt_final_ceiling[i] = FBT_CEILING_UNAPPLIED;
		fbt_last_ceiling[i] = -1;
		fbt_cur_floor[i] = fbt_min_freq[i];
	}

	/* The tables are complete: a policy created from here on can be
	 * attached by the notifier even before init finishes.
	 */
	WRITE_ONCE(policy_table_live, true);
	ret = cpufreq_register_notifier(&fbt_policy_notifier, CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		WRITE_ONCE(policy_table_live, false);
		return ret;
	}
	policy_notifier_registered = true;
	/* Prevent policy destruction between acquiring its ref and publishing
	 * the notifier/request pair. The REMOVE_POLICY callback owns detachment.
	 * cpu_ctrl_lock additionally serializes this initial attach against a
	 * concurrent CREATE/REMOVE notification for the same domain.
	 */
	cpus_read_lock();
	mutex_lock(&cpu_ctrl_lock);
	for (i = 0; i < policy_num; i++) {
		if (fbt_cpu_policy[i])
			continue; /* already attached by the notifier */
		cpu = fpsgo_get_cpu_policy_cpu(i);
		if (cpu < 0) {
			ret = -ENODEV;
			goto out;
		}
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			ret = -ENODEV;
			goto out;
		}
		if (!fpsgo_cpu_policy_matches(i, policy->related_cpus)) {
			cpufreq_cpu_put(policy);
			ret = -ENODEV;
			goto out;
		}
		/* freq_qos_read_value is not exported by either GKI. Its
		 * documented snapshot is the constraint's atomic target value,
		 * resampled by the shared attach path.
		 */
		ret = fbt_attach_policy_locked(i, policy);
		if (ret < 0) {
			cpufreq_cpu_put(policy);
			goto out;
		}
	}
	ret = 0;
out:
	mutex_unlock(&cpu_ctrl_lock);
	cpus_read_unlock();
	return ret;
}

static void fbt_cfp_notifier(int heavy)
{
	__update_cpu_freq_locked();
}

int fbt_set_cpu_freq_ceiling(int num, int *freq)
{
	int i, need_cfp = 0;
	int need_update = 0;

	if (!freq || num <= 0)
		return -EINVAL;
	mutex_lock(&cpu_ctrl_lock);
	if (ctrl_stopping || !ctrl_ready || num != policy_num) {
		mutex_unlock(&cpu_ctrl_lock);
		return -EAGAIN;
	}
	if (L_min_cap_enable && L_ceiling_min != 0 &&
		freq[0] >= 0 && freq[0] < L_ceiling_min)
		freq[0] = L_ceiling_min;

	if (policy_num >= 2 && ceiling_min_enable_EARA) {
		if (freq[policy_num-2] >=0 && freq[policy_num-2] < ceiling_min_M_EARA)
			freq[policy_num-2] = ceiling_min_M_EARA;
		if (freq[policy_num-1] >=0 && freq[policy_num-1] < ceiling_min_B_EARA)
			freq[policy_num-1] = ceiling_min_B_EARA;
	}


	for (i = 0; i < policy_num && i < num; i++) {
#if DEBUG_LOG
		pr_info("%s Cluster i:%d, freq:%d, num:%d\n", __func__, i, freq[i], num);
#endif
		if (fbt_last_ceiling[i] != freq[i]) {
			need_update = 1;
			fbt_last_ceiling[i] = freq[i];
		}

		if (freq[i] == -1)
			fbt_cur_ceiling[i] = fbt_max_freq[i];
		else {
			need_cfp = 1;

			if (freq[i] > fbt_max_freq[i])
				fbt_cur_ceiling[i] = fbt_max_freq[i];
			else if (freq[i] < fbt_min_freq[i])
				fbt_cur_ceiling[i] = fbt_min_freq[i];
			else
				fbt_cur_ceiling[i] = freq[i];
		}
	}

	if (need_update)
		__update_cpu_freq_locked();
	mutex_unlock(&cpu_ctrl_lock);

	/* enable / disable CFP */
	if (need_cfp)
		cfp_mon_enable(CFP_KIR_FPSGO, fbt_cfp_notifier);
	else
		cfp_mon_disable(CFP_KIR_FPSGO);

	return 0;
}

void update_userlimit_cpu_freq(int kicker, int cluster_num, struct cpu_ctrl_data *pld)
{
	int *freq;
	int i;

	if (!pld || cluster_num <= 0 || cluster_num > nr_cpu_ids)
		return;
	freq = kcalloc(cluster_num, sizeof(*freq), GFP_KERNEL);
	if (!freq)
		return;

	for (i = 0; i < cluster_num; i++)
		freq[i] = pld[i].max;

	fbt_set_cpu_freq_ceiling(cluster_num, freq);

	kfree(freq);
}

void fbt_cpu_L_ceiling_min(int freq)
{
	L_ceiling_min = freq;
}

int fbt_cpu_ctrl_get_ceil(void)
{
	int ceiled = 1;

	mutex_lock(&cpu_ctrl_lock);
	if (cfp_ceil_rel == 1 && cfp_onoff == 1)
		ceiled = 0;

	mutex_unlock(&cpu_ctrl_lock);

	return ceiled;
}

/*
 * System suspend quiesce. Called from a blocking PM notifier before the
 * freezer, so sleeping and joining work are both allowed.
 *
 * Withdraws the frequency constraints held on behalf of renderers that are
 * about to be frozen, stops the loading sampler and joins its worker so no
 * work runs against state that resume will rebaseline. The ceiling demand
 * itself is preserved as metadata, and the subsystem stays initialised: this
 * is not a disable.
 */
int fbt_cpu_ctrl_suspend(void)
{
	mutex_lock(&cpu_ctrl_lock);
	if (READ_ONCE(ctrl_stopping) || READ_ONCE(ctrl_suspended)) {
		mutex_unlock(&cpu_ctrl_lock);
		return 0;
	}
	WRITE_ONCE(ctrl_suspended, true);
	/* Releases every owned request to its default: other owners, including
	 * thermal and the cpufreq core, keep theirs untouched.
	 */
	if (READ_ONCE(ctrl_ready))
		__update_cpu_freq_locked();
	mutex_unlock(&cpu_ctrl_lock);
	/* Both workers take cpu_ctrl_lock: never join them while holding it. */
	disable_cpu_loading_timer();
	cancel_work_sync(&cpu_loading_timeout_work);
	if (g_psFbtCpuCtrlWorkQueue)
		flush_workqueue(g_psFbtCpuCtrlWorkQueue);
	return 0;
}

/*
 * System resume. Rebaselines every time-derived sample across the sleep gap
 * and reapplies the ceiling demand that is live at this moment, which the
 * normal update path kept current while suspended: a renderer that exited
 * meanwhile is therefore not resurrected.
 */
int fbt_cpu_ctrl_resume(void)
{
	int i;

	mutex_lock(&cpu_ctrl_lock);
	if (!READ_ONCE(ctrl_suspended)) {
		mutex_unlock(&cpu_ctrl_lock);
		return 0;
	}
	WRITE_ONCE(ctrl_suspended, false);
	/* Idle and wall counters are not comparable across the sleep gap: the
	 * next sample must be a baseline, exactly like the offline/reset case.
	 */
	if (cur_wall_time && cur_idle_time && prev_wall_time && prev_idle_time) {
		for (i = 0; i < nr_cpu_ids; i++) {
			cur_wall_time[i].time = 0;
			cur_idle_time[i].time = 0;
			prev_wall_time[i].time = 0;
			prev_idle_time[i].time = 0;
		}
	}
	/* Hysteresis counted in sampling periods that did not happen. */
	cfp_cur_up_time = 0;
	cfp_cur_down_time = 0;
	cfp_cur_loading = 0;
	if (READ_ONCE(ctrl_stopping) || !READ_ONCE(ctrl_ready)) {
		mutex_unlock(&cpu_ctrl_lock);
		return 0;
	}
	__update_cpu_freq_locked();
	mutex_unlock(&cpu_ctrl_lock);
	enable_cpu_loading_timer();
	return 0;
}

int fbt_cpu_ctrl_exit(void)
{
	int i;

	/* Stop producers before draining their work. Never flush while holding
	 * cpu_ctrl_lock: both work functions acquire it.
	 */
	mutex_lock(&cpu_ctrl_lock);
	WRITE_ONCE(ctrl_stopping, true);
	WRITE_ONCE(ctrl_ready, false);
	WRITE_ONCE(ctrl_suspended, false);
	/* Stop reattaching before the notifier is unregistered: a policy
	 * created during teardown must not acquire a request that the
	 * detach loop below has already passed.
	 */
	WRITE_ONCE(policy_table_live, false);
	cfp_enable = 0;
	mutex_unlock(&cpu_ctrl_lock);
	if (policy_notifier_registered) {
		cpufreq_unregister_notifier(&fbt_policy_notifier, CPUFREQ_POLICY_NOTIFIER);
		policy_notifier_registered = false;
	}
	mutex_lock(&cpu_ctrl_lock);
	if (fbt_cpu_policy && fbt_cpu_rq && min_notifier_added && fbt_freq_min_notifier)
		for (i = 0; i < policy_num; i++)
			fbt_detach_policy(i);
	mutex_unlock(&cpu_ctrl_lock);
	hrtimer_cancel(&cpu_ctrl_hrt);
	cancel_work_sync(&cpu_loading_timeout_work);
	if (g_psCpuLoadingWorkQueue) {
		destroy_workqueue(g_psCpuLoadingWorkQueue);
		g_psCpuLoadingWorkQueue = NULL;
	}
	if (g_psFbtCpuCtrlWorkQueue) {
		destroy_workqueue(g_psFbtCpuCtrlWorkQueue);
		g_psFbtCpuCtrlWorkQueue = NULL;
	}
	/* The OPP snapshot is a private copy handed over by the policy
	 * module; nothing else can release it.
	 */
	if (fbt_opp_tbl) {
		for (i = 0; i < fbt_opp_policy_num; i++)
			kfree(fbt_opp_tbl[i]);
	}
	fbt_opp_policy_num = 0;
#define FBT_FREE(p) do { kfree(p); (p) = NULL; } while (0)
	FBT_FREE(fbt_opp_tbl);
	FBT_FREE(fbt_opp_cnt);
	FBT_FREE(fbt_min_freq);
	FBT_FREE(fbt_max_freq);
	FBT_FREE(fbt_cpu_policy);
	FBT_FREE(fbt_cpu_rq);
	FBT_FREE(fbt_freq_min_notifier);
	FBT_FREE(min_notifier_added);
	FBT_FREE(fbt_cur_ceiling);
	FBT_FREE(fbt_last_ceiling);
	FBT_FREE(fbt_final_ceiling);
	FBT_FREE(fbt_cur_floor);
	FBT_FREE(cur_wall_time);
	FBT_FREE(cur_idle_time);
	FBT_FREE(prev_wall_time);
	FBT_FREE(prev_idle_time);
#undef FBT_FREE
	policy_num = 0;
	return 0;
}

int fbt_cpu_ctrl_init(void)
{
	int ret = -ENOMEM;

	hrtimer_init(&cpu_ctrl_hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cpu_ctrl_hrt.function = handle_cpu_loading_timeout;
	WRITE_ONCE(ctrl_stopping, true);
	WRITE_ONCE(ctrl_ready, false);
	WRITE_ONCE(ctrl_suspended, false);
	g_psCpuLoadingWorkQueue = alloc_ordered_workqueue("fbt_cpu_load", WQ_MEM_RECLAIM);
	g_psFbtCpuCtrlWorkQueue = alloc_ordered_workqueue("fbt_cpu_ctrl", WQ_MEM_RECLAIM);
	if (!g_psCpuLoadingWorkQueue || !g_psFbtCpuCtrlWorkQueue)
		goto fail;

	/* CPU identifiers can be sparse: num_possible_cpus() is not an array
	 * index limit. nr_cpu_ids is, and kcalloc zeroes all histories.
	 */
	cur_wall_time = kcalloc(nr_cpu_ids, sizeof(*cur_wall_time), GFP_KERNEL);
	cur_idle_time = kcalloc(nr_cpu_ids, sizeof(*cur_idle_time), GFP_KERNEL);
	prev_wall_time = kcalloc(nr_cpu_ids, sizeof(*prev_wall_time), GFP_KERNEL);
	prev_idle_time = kcalloc(nr_cpu_ids, sizeof(*prev_idle_time), GFP_KERNEL);
	if (!cur_wall_time || !cur_idle_time || !prev_wall_time || !prev_idle_time)
		goto fail;
	ret = fbt_cpu_topo_info();
	if (ret)
		goto fail;

	cfp_onoff = 1;
	cfp_enable = 0;
	cfp_polling_ms = 64;
	cfp_up_time = 1;
	cfp_down_time = 16;
	cfp_up_loading = 90;
	cfp_down_loading = 80;
	L_ceiling_min = 0;
	L_min_cap_enable = 1;
	ceiling_min_B_EARA = ceiling_min_M_EARA = ceiling_min_enable_EARA = 0;
	memset(cfp_req_tbl, 0, sizeof(cfp_req_tbl));
	WRITE_ONCE(ctrl_ready, true);
	WRITE_ONCE(ctrl_stopping, false);
	return 0;
fail:
	fbt_cpu_ctrl_exit();
	return ret;
}

