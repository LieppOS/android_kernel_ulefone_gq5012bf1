/* SPDX-License-Identifier: GPL-2.0 */
/* Embedded in nonlinear_opp_cap.c, before its first scalar setter. The actual
 * native controls remain in their original TU, with their original defaults.
 */
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "mtk_perf_ctrl.h"

struct mtk_perf_ctrl_lease {
	unsigned int gears;
	bool saved_gu, saved_cu;
	int saved_wl;
	int saved_max[MAX_NR_CPUS];
};
static DEFINE_RAW_SPINLOCK(mtk_perf_ctrl_lock);
static struct mtk_perf_ctrl_lease *mtk_perf_ctrl_owner;
static void mtk_perf_ctrl_preempt_locked(void);
#endif
