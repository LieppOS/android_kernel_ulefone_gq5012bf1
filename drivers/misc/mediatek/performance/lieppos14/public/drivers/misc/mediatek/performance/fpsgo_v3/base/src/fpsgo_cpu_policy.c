// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 MediaTek Inc.
 * Immutable policy/OPP snapshot owned by the resident FPSGO framework.
 */

#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sort.h>

#include "../include/fpsgo_cpu_policy.h"

static int policy_num;
static int *opp_count;
static unsigned int **opp_table;
static cpumask_t *policy_masks;
static int *cpu_policy_id;

static int cmp_uint(const void *a, const void *b)
{
	unsigned int x = *(const unsigned int *)a;
	unsigned int y = *(const unsigned int *)b;

	/* Frequencies are unsigned; subtraction can overflow a signed int. */
	return (y > x) - (y < x);
}

static void free_opp_snapshot(int count, int *counts, unsigned int **tables)
{
	int i;

	if (tables)
		for (i = 0; i < count; i++)
			kfree(tables[i]);
	kfree(tables);
	kfree(counts);
}

int fpsgo_cpu_policy_init(void)
{
	struct cpufreq_frequency_table *pos;
	struct cpufreq_policy *policy;
	unsigned int **tables;
	cpumask_t *masks, seen;
	int *counts, *ids;
	int cpu, count, i, num = 0, ret = -ENOMEM;

	if (policy_num)
		return -EBUSY;
	/* A single pass avoids inconsistent two-pass policy counts. All temporary
	 * slots start empty so any table-allocation failure has one unwind path.
	 */
	counts = kcalloc(nr_cpu_ids, sizeof(*counts), GFP_KERNEL);
	tables = kcalloc(nr_cpu_ids, sizeof(*tables), GFP_KERNEL);
	masks = kcalloc(nr_cpu_ids, sizeof(*masks), GFP_KERNEL);
	ids = kmalloc_array(nr_cpu_ids, sizeof(*ids), GFP_KERNEL);
	if (!counts || !tables || !masks || !ids)
		goto free_snapshot;
	for (i = 0; i < nr_cpu_ids; i++)
		ids[i] = -1;
	cpumask_clear(&seen);

	/* Stabilize policy removal/creation while copying driver-owned tables.
	 * No policy pointer or table pointer escapes its held reference.
	 */
	cpus_read_lock();
	for_each_possible_cpu(cpu) {
		if (cpumask_test_cpu(cpu, &seen))
			continue;
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		ret = -EINVAL;
		if (!policy->freq_table)
			goto put_policy;
		count = 0;
		cpufreq_for_each_valid_entry(pos, policy->freq_table) {
			if (!pos->frequency)
				goto put_policy;
			count++;
		}
		if (!count)
			goto put_policy;
		tables[num] = kcalloc(count, sizeof(**tables), GFP_KERNEL);
		if (!tables[num]) {
			ret = -ENOMEM;
			goto put_policy;
		}
		counts[num] = count;
		i = 0;
		cpufreq_for_each_valid_entry(pos, policy->freq_table)
			tables[num][i++] = pos->frequency;
		sort(tables[num], count, sizeof(**tables), cmp_uint, NULL);
		cpumask_copy(&masks[num], policy->related_cpus);
		cpumask_or(&seen, &seen, policy->related_cpus);
		for_each_possible_cpu(i)
			if (cpumask_test_cpu(i, policy->related_cpus))
				ids[i] = num;
		num++;
		cpufreq_cpu_put(policy);
	}
	cpus_read_unlock();
	if (!num) {
		ret = -ENODEV;
		goto free_snapshot;
	}

	/* Published once before tracepoints, workers or frame callbacks exist.
	 * This module cannot unload; only an early init failure may free it.
	 */
	opp_count = counts;
	opp_table = tables;
	policy_masks = masks;
	cpu_policy_id = ids;
	policy_num = num;
	return 0;

put_policy:
	cpufreq_cpu_put(policy);
	cpus_read_unlock();
free_snapshot:
	/* The failed row is also zero-initialized or owned by this allocation. */
	free_opp_snapshot(nr_cpu_ids, counts, tables);
	kfree(masks);
	kfree(ids);
	return ret;
}

/* Only before frame/cpufreq callbacks are registered; not an unload routine. */
void fpsgo_cpu_policy_exit(void)
{
	free_opp_snapshot(policy_num, opp_count, opp_table);
	kfree(policy_masks);
	kfree(cpu_policy_id);
	opp_count = NULL;
	opp_table = NULL;
	policy_masks = NULL;
	cpu_policy_id = NULL;
	policy_num = 0;
}

int fpsgo_get_cpu_policy_num(void)
{
	return policy_num;
}

int fpsgo_get_cpu_policy_id(unsigned int cpu)
{
	if (cpu >= nr_cpu_ids || !cpu_policy_id)
		return -ENODEV;
	return cpu_policy_id[cpu];
}

int fpsgo_get_cpu_policy_cpu(unsigned int index)
{
	if (index >= policy_num)
		return -ENODEV;
	return cpumask_first(&policy_masks[index]);
}

int fpsgo_get_cpu_policy_opp_count(unsigned int index)
{
	if (index >= policy_num)
		return 0;
	return opp_count[index];
}

unsigned int fpsgo_get_cpu_policy_freq(unsigned int index, unsigned int opp)
{
	if (index >= policy_num || opp >= opp_count[index])
		return 0;
	return opp_table[index][opp];
}

bool fpsgo_cpu_policy_matches(unsigned int index, const struct cpumask *mask)
{
	return index < policy_num && mask && cpumask_equal(&policy_masks[index], mask);
}

const struct cpumask *fpsgo_get_cpu_policy_mask(unsigned int index)
{
	if (index >= policy_num || !policy_masks)
		return NULL;
	return &policy_masks[index];
}

int fpsgo_get_cpu_opp_info(int **opp_cnt, unsigned int ***opp_tbl)
{
	int *counts;
	unsigned int **tables;
	int i;

	if (!opp_cnt || !opp_tbl)
		return -EINVAL;
	*opp_cnt = NULL;
	*opp_tbl = NULL;
	if (!policy_num)
		return -ENODEV;
	counts = kcalloc(policy_num, sizeof(*counts), GFP_KERNEL);
	tables = kcalloc(policy_num, sizeof(*tables), GFP_KERNEL);
	if (!counts || !tables)
		goto fail;
	for (i = 0; i < policy_num; i++) {
		counts[i] = opp_count[i];
		tables[i] = kmemdup(opp_table[i], counts[i] * sizeof(**tables), GFP_KERNEL);
		if (!tables[i])
			goto fail;
	}
	/* No partially allocated output escapes to FBT on failure. */
	*opp_cnt = counts;
	*opp_tbl = tables;
	return 0;
fail:
	free_opp_snapshot(policy_num, counts, tables);
	return -ENOMEM;
}
