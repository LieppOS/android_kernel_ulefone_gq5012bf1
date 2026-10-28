/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

#ifndef __FPSGO_CPU_POLICY_H__
#define __FPSGO_CPU_POLICY_H__

#include <linux/cpumask.h>

int fpsgo_cpu_policy_init(void);
/* Early initialization rollback only; this is not framework unload support. */
void fpsgo_cpu_policy_exit(void);
int fpsgo_get_cpu_policy_num(void);
int fpsgo_get_cpu_policy_id(unsigned int cpu);
int fpsgo_get_cpu_policy_cpu(unsigned int index);
int fpsgo_get_cpu_policy_opp_count(unsigned int index);
unsigned int fpsgo_get_cpu_policy_freq(unsigned int index, unsigned int opp);
bool fpsgo_cpu_policy_matches(unsigned int index, const struct cpumask *mask);
/* Immutable until early rollback; runtime callbacks never release this map. */
const struct cpumask *fpsgo_get_cpu_policy_mask(unsigned int index);
int fpsgo_get_cpu_opp_info(int **opp_cnt, unsigned int ***opp_tbl);

#endif /* __FPSGO_CPU_POLICY_H__ */

