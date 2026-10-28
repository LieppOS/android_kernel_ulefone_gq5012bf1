// SPDX-License-Identifier: GPL-2.0
/* LieppOS PowerHAL policy/QoS ownership. No SoC frequency constants or
 * scheduler-private storage. cpufreq 6.1 CREATE_POLICY is sent after per-CPU
 * policy publication and core QoS initialization; REMOVE_POLICY precedes free.
 */
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>
#include <linux/string.h>
#include "powerhal_policy.h"

struct powerhal_policy {
	struct cpufreq_policy *policy; /* owned cpufreq_cpu_get reference */
	struct freq_qos_request min_request, max_request;
	cpumask_t cpus;
	struct powerhal_policy_state state;
};
static struct powerhal_policy policies[POWERHAL_MAX_POLICIES];
static DEFINE_MUTEX(policy_lock);
static unsigned int policy_count;
static bool notifier_registered;

static void detach_policy(struct powerhal_policy *p)
{
	if (!p->policy)
		return;
	if (freq_qos_request_active(&p->min_request))
		freq_qos_remove_request(&p->min_request);
	if (freq_qos_request_active(&p->max_request))
		freq_qos_remove_request(&p->max_request);
	cpufreq_cpu_put(p->policy);
	p->policy = NULL;
}

/* Takes ownership of the caller's reference, including every failure path. */
static int attach_policy(struct cpufreq_policy *policy)
{
	struct powerhal_policy *p = NULL;
	unsigned int i;
	int ret;

	lockdep_assert_held(&policy_lock);
	for (i = 0; i < policy_count; i++) {
		if (!cpumask_equal(&policies[i].cpus, policy->related_cpus))
			continue;
		p = &policies[i];
		if (p->policy) {
			cpufreq_cpu_put(policy);
			return 0;
		}
		break;
	}
	if (!p) {
		if (policy_count >= POWERHAL_MAX_POLICIES) {
			cpufreq_cpu_put(policy);
			return -E2BIG;
		}
		p = &policies[policy_count++];
		cpumask_copy(&p->cpus, policy->related_cpus);
		p->state.min = FREQ_QOS_MIN_DEFAULT_VALUE;
		p->state.max = FREQ_QOS_MAX_DEFAULT_VALUE;
	}
	p->policy = policy;
	p->state.hardware_min = policy->cpuinfo.min_freq;
	p->state.hardware_max = policy->cpuinfo.max_freq;
	/* Recreated policy, same domain: revalidate owned requests against its
	 * new hardware range; all other owners (including thermal) keep theirs.
	 */
	if (p->state.min != FREQ_QOS_MIN_DEFAULT_VALUE)
		p->state.min = clamp_t(int, p->state.min, p->state.hardware_min, p->state.hardware_max);
	if (p->state.max != FREQ_QOS_MAX_DEFAULT_VALUE)
		p->state.max = clamp_t(int, p->state.max, p->state.hardware_min, p->state.hardware_max);
	if (p->state.max < p->state.min)
		p->state.min = p->state.max;
	ret = freq_qos_add_request(&policy->constraints, &p->max_request,
				   FREQ_QOS_MAX, p->state.max);
	if (ret < 0)
		goto fail;
	ret = freq_qos_add_request(&policy->constraints, &p->min_request,
				   FREQ_QOS_MIN, p->state.min);
	if (ret < 0)
		goto fail;
	return 0;
fail:
	detach_policy(p);
	return ret;
}

static int policy_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	unsigned int i;
	int ret = 0;

	mutex_lock(&policy_lock);
	if (event == CPUFREQ_REMOVE_POLICY) {
		for (i = 0; i < policy_count; i++)
			if (policies[i].policy == policy)
				detach_policy(&policies[i]);
	} else if (event == CPUFREQ_CREATE_POLICY) {
		struct cpufreq_policy *owned = cpufreq_cpu_get(policy->cpu);

		if (owned == policy)
			ret = attach_policy(owned);
		else {
			if (owned)
				cpufreq_cpu_put(owned);
			ret = -ENODEV;
		}
	}
	mutex_unlock(&policy_lock);
	if (ret < 0)
		pr_warn_ratelimited("powerhal: QoS attach failed for CPU%u: %d\n", policy->cpu, ret);
	/* Do not veto cpufreq core lifecycle; the service returns ENODEV for
	 * unattached domains instead of pretending requests were applied.
	 */
	return NOTIFY_OK;
}
static struct notifier_block policy_notifier = { .notifier_call = policy_event };

void powerhal_policy_exit(void)
{
	unsigned int i;

	if (notifier_registered) {
		cpufreq_unregister_notifier(&policy_notifier, CPUFREQ_POLICY_NOTIFIER);
		notifier_registered = false;
	}
	mutex_lock(&policy_lock);
	for (i = 0; i < policy_count; i++)
		detach_policy(&policies[i]);
	memset(policies, 0, sizeof(policies));
	policy_count = 0;
	mutex_unlock(&policy_lock);
}

int powerhal_policy_init(void)
{
	int cpu, ret;

	ret = cpufreq_register_notifier(&policy_notifier, CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		return ret;
	notifier_registered = true;
	cpus_read_lock();
	mutex_lock(&policy_lock);
	for_each_possible_cpu(cpu) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

		if (!policy)
			continue;
		ret = attach_policy(policy);
		if (ret < 0)
			break;
	}
	if (!policy_count && ret >= 0)
		ret = -EPROBE_DEFER;
	mutex_unlock(&policy_lock);
	cpus_read_unlock();
	if (ret < 0)
		powerhal_policy_exit();
	return ret < 0 ? ret : 0;
}

int powerhal_policy_count(void)
{
	int count;

	mutex_lock(&policy_lock);
	count = policy_count;
	mutex_unlock(&policy_lock);
	return count;
}

int powerhal_policy_read(int index, struct powerhal_policy_state *state)
{
	int ret = -ENODEV;

	if (!state)
		return -EINVAL;
	mutex_lock(&policy_lock);
	if (index >= 0 && index < policy_count && policies[index].policy) {
		*state = policies[index].state;
		ret = 0;
	}
	mutex_unlock(&policy_lock);
	return ret;
}

int powerhal_policy_set(int index, int min, int max)
{
	struct powerhal_policy *p;
	int ret;

	if (min < -1 || max < -1)
		return -EINVAL;
	mutex_lock(&policy_lock);
	if (index < 0 || index >= policy_count || !policies[index].policy) {
		ret = -ENODEV;
		goto out;
	}
	p = &policies[index];
	min = min == -1 ? FREQ_QOS_MIN_DEFAULT_VALUE :
		clamp_t(int, min, 0, p->state.hardware_max);
	max = (max == -1 || max == FREQ_QOS_MAX_DEFAULT_VALUE) ? FREQ_QOS_MAX_DEFAULT_VALUE :
		clamp_t(int, max, p->state.hardware_min, p->state.hardware_max);
	if (min > max) {
		ret = -EINVAL;
		goto out;
	}
	ret = freq_qos_update_request(&p->max_request, max);
	if (ret < 0)
		goto out;
	ret = freq_qos_update_request(&p->min_request, min);
	if (ret < 0) {
		int rollback = freq_qos_update_request(&p->max_request, p->state.max);

		if (rollback < 0)
			pr_err("powerhal: max request rollback failed: %d\n", rollback);
		goto out;
	}
	p->state.min = min;
	p->state.max = max;
	ret = 0;
out:
	mutex_unlock(&policy_lock);
	return ret;
}
