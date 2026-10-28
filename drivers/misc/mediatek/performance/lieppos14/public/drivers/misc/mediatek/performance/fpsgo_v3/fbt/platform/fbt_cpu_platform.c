// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/cpumask.h>
#include <linux/interconnect.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <linux/sched/cputime.h>
#include <sched/sched.h>
#include <uapi/linux/sched/types.h>
#include "../../mt-plat/fpsgo_common.h"
#include "dvfsrc-exp.h"
#include "../../base/include/fpsgo_base.h"
#include "../include/fbt_cpu_platform.h"
#if IS_ENABLED(CONFIG_MTK_SCHEDULER)
#include <drivers/misc/mediatek/sched/common.h>
#endif
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
#include <drivers/misc/mediatek/sched/sugov/mtk_perf_ctrl.h>
#endif

static int mask_int[FPSGO_PREFER_TOTAL];
static struct cpumask mask[FPSGO_PREFER_TOTAL];
static int mask_done;
static struct icc_path *bw_path;
static DEFINE_MUTEX(fbt_platform_lock);
static struct device_node *node;
static unsigned int peak_bw;
static int plat_gcc_enable;
static int plat_sbe_rescue_enable;
static int plat_ux_scroll_policy_type;
static int plat_cpu_limit;

void fbt_notify_CM_limit(int reach_limit)
{
#if IS_ENABLED(CONFIG_MTK_CM_MGR)
	cm_mgr_perf_set_status(reach_limit);
#endif
	fpsgo_systrace_c_fbt_debug(-100, 0, reach_limit, "notify_cm");
}

static int generate_cpu_mask(void);
static int generate_sbe_rescue_enable(void);
static int generate_ux_scroll_policy_type(void);
static int platform_fpsgo_probe(struct platform_device *pdev)
{
	int ret = 0, retval = 0;

	mutex_lock(&fbt_platform_lock);
	node = pdev->dev.of_node;

	FPSGO_LOGE("%s\n", __func__);

	bw_path = of_icc_get(&pdev->dev, "fpsgo-perf-bw");
	if (IS_ERR(bw_path)) {
		ret = PTR_ERR(bw_path);
		bw_path = NULL;
		if (ret == -EPROBE_DEFER) {
			mutex_unlock(&fbt_platform_lock);
			return ret;
		}
		dev_info(&pdev->dev, "optional performance bandwidth path unavailable: %d\n", ret);
	}

#if IS_ENABLED(CONFIG_MTK_DVFSRC)
		peak_bw = dvfsrc_get_required_opp_peak_bw(node, 0);
#endif /* CONFIG_MTK_DVFSRC */

	ret = of_property_read_u32(node,
			 "gcc-enable", &retval);
	if (!ret)
		plat_gcc_enable = retval;
	else
		FPSGO_LOGE("%s unable to get plat_gcc_enable\n", __func__);

	ret = of_property_read_u32(node,
			 "cpu-limit", &retval);
	if (!ret)
		plat_cpu_limit = retval;

	generate_cpu_mask();
	generate_sbe_rescue_enable();
	generate_ux_scroll_policy_type();
	mutex_unlock(&fbt_platform_lock);
	return 0;
}

static int platform_fpsgo_remove(struct platform_device *pdev)
{
	mutex_lock(&fbt_platform_lock);
	if (bw_path) {
		icc_set_bw(bw_path, 0, 0);
		icc_put(bw_path);
		bw_path = NULL;
	}
	mask_done = 0;
	node = NULL;
	mutex_unlock(&fbt_platform_lock);
	return 0;
}

static const struct of_device_id platform_fpsgo_of_match[] = {
	{ .compatible = "mediatek,fpsgo", },
	{},
};

static const struct platform_device_id platform_fpsgo_id_table[] = {
	{ "fpsgo", 0},
	{ },
};

static struct platform_driver mtk_platform_fpsgo_driver = {
	.probe = platform_fpsgo_probe,
	.remove	= platform_fpsgo_remove,
	.driver = {
		.name = "fpsgo",
		.owner = THIS_MODULE,
		.of_match_table = platform_fpsgo_of_match,
	},
	.id_table = platform_fpsgo_id_table,
};

int init_fbt_platform(void)
{
	FPSGO_LOGE("%s\n", __func__);
	return platform_driver_register(&mtk_platform_fpsgo_driver);
}

void exit_fbt_platform(void)
{
	platform_driver_unregister(&mtk_platform_fpsgo_driver);
}

void fbt_boost_dram(int boost)
{
	/* Serialize requests against driver unbind and interconnect release. */
	mutex_lock(&fbt_platform_lock);
	if (!bw_path || !peak_bw) {
		mutex_unlock(&fbt_platform_lock);
		return;
	}
	if (boost)
		icc_set_bw(bw_path, 0, peak_bw);
	else
		icc_set_bw(bw_path, 0, 0);

	mutex_unlock(&fbt_platform_lock);
	fpsgo_systrace_c_fbt_debug(-100, 0, boost, "dram_boost");
}

void fbt_set_boost_value(unsigned int base_blc)
{
	base_blc = clamp(base_blc, 1U, 100U);
	fpsgo_sentcmd(FPSGO_SET_BOOST_TA, base_blc, -1);
	fpsgo_systrace_c_fbt_debug(-100, 0, base_blc, "TA_cap");
}

void fbt_clear_boost_value(void)
{
	fpsgo_sentcmd(FPSGO_SET_BOOST_TA, -1, -1);
	fpsgo_systrace_c_fbt_debug(-100, 0, 0, "TA_cap");

	fbt_notify_CM_limit(0);
	fbt_boost_dram(0);
}

static void fbt_build_task_cap_attr(unsigned int min_blc, unsigned int max_blc,
			unsigned int max_util, struct sched_attr *attr)
{
	unsigned int min_blc_1024;
	unsigned int max_blc_1024;

	min_blc = min(min_blc, 100U);
	max_blc = min(max_blc, 100U);
	max_util = min(max_util, 1024U);
	max_blc_1024 = (max_util != 1024U) ? max_util : ((max_blc << 10) / 100U);
	max_blc_1024 = clamp(max_blc_1024, 1U, 1024U);

	min_blc_1024 = (min_blc << 10) / 100U;
	min_blc_1024 = clamp(min_blc_1024, 1U, max_blc_1024);

	attr->sched_policy = -1;
	attr->sched_flags =
		SCHED_FLAG_KEEP_ALL |
		SCHED_FLAG_UTIL_CLAMP |
		SCHED_FLAG_RESET_ON_FORK;

	if (min_blc == 0 && max_blc == 100) {
		attr->sched_util_min = -1;
		attr->sched_util_max = -1;
	} else {
		attr->sched_util_min = (min_blc_1024 << 10) / 1280;
		attr->sched_util_max = (max_blc_1024 << 10) / 1280;
	}
}

static void fbt_trace_task_cap(int pid, int ret, const struct sched_attr *attr)
{
	if (ret != 0) {
		fpsgo_systrace_c_fbt(pid, 0, ret, "uclamp fail");
		fpsgo_systrace_c_fbt(pid, 0, 0, "uclamp fail");
		return;
	}

	fpsgo_systrace_c_fbt_debug(pid, 0, attr->sched_util_min, "min_cap");
	fpsgo_systrace_c_fbt_debug(pid, 0, attr->sched_util_max, "max_cap");
}

/* Applies to the caller's already-retained task. No numeric lookup happens
 * here, so the clamp cannot land on a task that merely reuses the tid.
 */
void fbt_set_per_task_cap_task(struct task_struct *p, unsigned int min_blc,
			unsigned int max_blc, unsigned int max_util)
{
	struct sched_attr attr = {};
	int ret;

	if (!p)
		return;

	fbt_build_task_cap_attr(min_blc, max_blc, max_util, &attr);
	/* KEEP_ALL lets the scheduler serialize policy/priority itself.
	 * Reading p->policy/rt_priority or uclamp state here races sched_setattr.
	 */
	ret = sched_setattr_nocheck(p, &attr);
	fbt_trace_task_cap(task_pid_nr(p), ret, &attr);
}

void fbt_set_per_task_cap(int pid, unsigned int min_blc,
			unsigned int max_blc, unsigned int max_util)
{
	struct task_struct *p;

	if (!pid)
		return;

	if (pid < 0) {
		struct sched_attr attr = {};

		fbt_build_task_cap_attr(min_blc, max_blc, max_util, &attr);
		fbt_trace_task_cap(pid, -ESRCH, &attr);
		return;
	}

	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (likely(p))
		get_task_struct(p);
	rcu_read_unlock();

	if (unlikely(!p)) {
		struct sched_attr attr = {};

		fbt_build_task_cap_attr(min_blc, max_blc, max_util, &attr);
		fbt_trace_task_cap(pid, -ESRCH, &attr);
		return;
	}

	fbt_set_per_task_cap_task(p, min_blc, max_blc, max_util);
	put_task_struct(p);
}

static int generate_cpu_mask(void)
{
	int i, ret, cpu;
	int temp_mask = 0;

	ret = of_property_read_u32_array(node, "fbt-cpu-mask",
			mask_int, FPSGO_PREFER_TOTAL);

	for (i = 0; i < FPSGO_PREFER_TOTAL; i++) {
		cpumask_clear(&mask[i]);
		temp_mask = mask_int[i];
		for_each_possible_cpu(cpu) {
			if (cpu < BITS_PER_TYPE(unsigned int) && ((unsigned int)temp_mask & BIT(cpu)))
				cpumask_set_cpu(cpu, &mask[i]);
		}
		FPSGO_LOGE("%s i:%d mask:%d %*pbl\n",
			__func__, i, mask_int[i], cpumask_pr_args(&mask[i]));
	}

	if (!ret)
		mask_done = 1;

	return ret;
}

struct cpumask fbt_generate_user_cpu_mask(int mask_int)
{
	unsigned long cpumask_ulval = (unsigned int)mask_int;
	struct cpumask cpumask_setting;
	int cpu;

	cpumask_clear(&cpumask_setting);
	for_each_possible_cpu(cpu) {
		if (cpu < BITS_PER_TYPE(unsigned int) && (cpumask_ulval & BIT(cpu)))
			cpumask_set_cpu(cpu, &cpumask_setting);
	}
	return cpumask_setting;
}

static int generate_sbe_rescue_enable(void)
{
	int ret = 0, retval = 0;

	ret = of_property_read_u32(node,
		 "sbe-resceue-enable", &retval);
	if (!ret)
		plat_sbe_rescue_enable = retval;
	else
		FPSGO_LOGE("%s unable to get plat_sbe_rescue_enable\n", __func__);

	return ret;
}

static int generate_ux_scroll_policy_type(void)
{
	int ret = 0, retval = 0;

	plat_ux_scroll_policy_type = 1;
	ret = of_property_read_u32(node,
		 "ux-scrol-policy-type", &retval);
	if (!ret)
		plat_ux_scroll_policy_type = retval;
	else
		FPSGO_LOGE("%s unable to get plat_ux_scroll_policy_type\n", __func__);

	return ret;
}

int fbt_check_ls_task(struct task_struct *p)
{
#if IS_ENABLED(CONFIG_MTK_SCHEDULER)
	if (!IS_ENABLED(CONFIG_MTK_SCHEDULER) || !p)
		return 0; /* No vendor latency-sensitive attribute exists. */
	return is_task_latency_sensitive(p);
#else
	return 0; /* Capability query: vendor LS is not present. */
#endif
}

int fbt_check_ls(int pid)
{
#if IS_ENABLED(CONFIG_MTK_SCHEDULER)
	struct task_struct *tsk;
	int ls = 0;

	if (!IS_ENABLED(CONFIG_MTK_SCHEDULER))
		return 0;
	rcu_read_lock();
	tsk = find_task_by_vpid(pid);
	if (!tsk)
		goto EXIT;
	get_task_struct(tsk);
	ls = fbt_check_ls_task(tsk);
	put_task_struct(tsk);

EXIT:
	rcu_read_unlock();
	return ls;
#else
	return 0;
#endif
}

int fbt_check_vip_task(struct task_struct *p)
{
#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
	struct vip_task_struct *vts;

	if (!IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK) || !p)
		return 0;
	vts = &((struct mtk_task *) p->android_vendor_data1)->vip_task;
	return vts->basic_vip;
#else
	return 0; /* No vendor VIP state may be read from reserved task bytes. */
#endif
}

int fbt_check_vip(int pid)
{
#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
	struct task_struct *tsk;
	int is_vip = 0;

	if (!IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK))
		return 0;
	rcu_read_lock();
	tsk = find_task_by_vpid(pid);
	if (!tsk)
		goto EXIT;
	get_task_struct(tsk);
	is_vip = fbt_check_vip_task(tsk);
	put_task_struct(tsk);

EXIT:
	rcu_read_unlock();
	return is_vip;
#else
	return 0;
#endif
}

int fbt_check_vvip_task(struct task_struct *p)
{
#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
	struct vip_task_struct *vts;

	if (!IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK) || !p)
		return 0;
	vts = &((struct mtk_task *) p->android_vendor_data1)->vip_task;
	return vts->vvip;
#else
	return 0;
#endif
}

int fbt_check_vvip(int pid)
{
#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
	struct task_struct *tsk;
	int is_vvip = 0;

	if (!IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK))
		return 0;
	rcu_read_lock();
	tsk = find_task_by_vpid(pid);
	if (!tsk)
		goto EXIT;
	get_task_struct(tsk);
	is_vvip = fbt_check_vvip_task(tsk);
	put_task_struct(tsk);

EXIT:
	rcu_read_unlock();
	return is_vvip;
#else
	return 0;
#endif
}

/* Hard affinity operates directly on the caller's retained task. Soft
 * affinity still crosses a vendor ABI that accepts only a numeric tid; that
 * tid is derived from the retained task, never from a stale array value.
 */
int fbt_set_affinity_task(struct task_struct *p, unsigned int prefer_type)
{
	int pid, ret;

	if (!p)
		return -ESRCH;
	if (prefer_type >= FPSGO_PREFER_TOTAL)
		return -EINVAL;
	if (!mask_done)
		return -ENODEV;

	pid = task_pid_nr(p);
	ret = fpsgo_task_setaffinity(p, &mask[prefer_type]);
	if (ret) {
		fpsgo_systrace_c_fbt(pid, 0, ret, "setaffinity fail");
		fpsgo_systrace_c_fbt(pid, 0, 0, "setaffinity fail");
		return ret;
	}
	fpsgo_systrace_c_fbt(pid, 0, prefer_type, "set_affinity");
	return 0;
}

/* These are the native vendor-field operations, applied to an owned task.
 * Neither a task reference nor a struct pid reference reserves a PID number:
 * converting back to pid_t and calling a numeric setter can hit its successor.
 */
void fbt_set_vip_task(struct task_struct *p, bool vip, bool vvip)
{
#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
	struct vip_task_struct *vts;

	if (!p || !pid_alive(p))
		return;
	vts = &((struct mtk_task *)p->android_vendor_data1)->vip_task;
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
	/* Preserve native legacy-writer preemption of a C2PS task lease. */
	mtk_perf_task_legacy_vip(p, vip);
#else
	WRITE_ONCE(vts->basic_vip, vip);
#endif
	WRITE_ONCE(vts->vvip, vvip);
#endif
}

void fbt_set_ls_task(struct task_struct *p, bool set)
{
#if IS_ENABLED(CONFIG_MTK_SCHEDULER)
	if (p && pid_alive(p))
		WRITE_ONCE(((struct mtk_task *)p->android_vendor_data1)->sa_task.latency_sensitive, set);
#endif
}

int fbt_set_soft_affinity_task(struct task_struct *p, int set,
			unsigned int prefer_type)
{
#if IS_ENABLED(CONFIG_MTK_SCHEDULER)
	struct soft_affinity_task *sa;
	struct cpumask reset_mask;

	if (!p || !pid_alive(p))
		return -ESRCH;
	if (prefer_type >= FPSGO_PREFER_TOTAL)
		return -EINVAL;
	if (!mask_done)
		return -ENODEV;
	sa = &((struct mtk_task *)p->android_vendor_data1)->sa_task;
	/* Preserve native unset_task_ls_prefer_cpus()'s 0xff reset mask. */
	reset_mask = fbt_generate_user_cpu_mask(0xff);
	cpumask_copy(&sa->soft_cpumask, set ? &mask[prefer_type] : &reset_mask);
	WRITE_ONCE(sa->latency_sensitive, !!set);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

int fbt_set_affinity(pid_t pid, unsigned int prefer_type)
{
	int ret = 0;

	if (prefer_type >= FPSGO_PREFER_TOTAL)
		return -EINVAL;
	if (!mask_done) {
		ret = -ENODEV;
		goto out;
	}

	ret = fpsgo_sched_setaffinity(pid, &mask[prefer_type]);

out:
	if (ret != 0) {
		fpsgo_systrace_c_fbt(pid, 0, ret, "setaffinity fail");
		fpsgo_systrace_c_fbt(pid, 0, 0, "setaffinity fail");
		return ret;
	}
	fpsgo_systrace_c_fbt(pid, 0, prefer_type, "set_affinity");
	return ret;
}

int fbt_set_soft_affinity(int pid, int set, unsigned int prefer_type)
{
	int ret = 0;

	if (!IS_ENABLED(CONFIG_MTK_SCHEDULER))
		return -EOPNOTSUPP;
	if (prefer_type >= FPSGO_PREFER_TOTAL)
		return -EINVAL;
	if (!mask_done) {
		ret = -ENODEV;
		goto out;
	}

	if (set)
		set_task_ls_prefer_cpus(pid, mask_int[prefer_type]);
	else
		unset_task_ls_prefer_cpus(pid);

out:
	if (ret != 0) {
		fpsgo_systrace_c_fbt(pid, 0, ret, "softaffinity fail");
		fpsgo_systrace_c_fbt(pid, 0, 0, "softaffinity fail");
		return ret;
	}
	fpsgo_systrace_c_fbt(pid, 0, prefer_type, "soft_affinity");
	return ret;
}

int fbt_get_L_min_ceiling(void)
{
	return 0;
}

int fbt_get_default_boost_ta(void)
{
	return 0;
}

int fbt_get_default_adj_loading(void)
{
	return 0;
}

int fbt_get_default_adj_count(void)
{
	return 10;
}

int fbt_get_default_adj_tdiff(void)
{
	return 1000000;
}

int fbt_get_cluster_limit(int *cluster, int *freq, int *r_freq, int *cpu)
{
/*
 * Use return value to specify limit on frequency or core
 * 1. when return value is FPSGO_LIMIT_NO_LIMIT -> no limit
 * 2. when return value is FPSGO_LIMIT_FREQ -> frequency limit
 * 2.1 when cluster is set and freq is set -> ceiling limit
 * 2.2 when cluster is set and r_freq is set -> rescue ceiling limit
 * 3. when return value is FPSGO_LIMIT_CPU -> cpu limit
 * 3.1 when cpu is set valid -> cpu isolation
 */
	int limit = plat_cpu_limit;

	if (limit == FPSGO_LIMIT_CPU)
		*cpu = 7;
	else if (limit == FPSGO_LIMIT_FREQ) {
		*cluster = 2;
		*freq = 2600000;
	}

	return limit;
}

int fbt_get_default_qr_enable(void)
{
	return 1;
}

int fbt_get_default_gcc_enable(void)
{
	return plat_gcc_enable;
}

int fbt_get_default_sbe_rescue_enable(void)
{
	return plat_sbe_rescue_enable;
}

int fbt_get_ux_scroll_policy_type(void)
{
	return plat_ux_scroll_policy_type;
}

int fbt_get_l_min_bhropp(void)
{
	return 0;
}

