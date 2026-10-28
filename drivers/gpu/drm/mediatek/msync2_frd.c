// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MSYNC2 frame-rate decision helper.
 *
 * This module supplies the three DRM callback implementations used by the
 * MediaTek display driver's MSYNC2 path and the FPSGO frame-info entry point.
 * The algorithms and the four TE-type time limits were reconstructed from the
 * shipped GQ5012BF1 msync2_frd.ko; no register values or panel timings are
 * invented here.  The level table itself is owned by the display driver and
 * is passed in by the caller.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/minmax.h>
#include <drm/mediatek_drm.h>

#define MSYNC_LEVELS 20

extern int (*mtk_drm_get_target_fps_fp)(unsigned int vrefresh,
					unsigned int atomic_fps);
extern int (*mtk_sync_te_level_decision_fp)(void *level_tb,
					    unsigned int te_type,
					    unsigned int target_fps,
					    unsigned int *fps_level,
					    unsigned int *min_fps,
					    unsigned long x_time);
extern int (*mtk_sync_slow_descent_fp)(unsigned int fps_level,
				       unsigned int fps_level_old,
				       unsigned int delay_frame_num);

static unsigned int fpsgo2msycn_target_fps;
static unsigned int fpsgo2msycn_q2q_time;
static unsigned int fpsgo2msycn_q2q_time2;
static unsigned int current_vrefresh;

/* Stock msync2_frd.c uses the same misspelling for these internal globals. */
static unsigned int fpsgo2msync_delay_frame;

/*
 * The stock object contains four 64-bit limits, indexed by te_type - 1:
 * REQUEST_TE, MULTI_TE, an unused type, and TRIGGER_LEVEL_TE.
 */
static const unsigned long msync_te_x_time_limit[4] = {
	8400, 8400, 0, 8400,
};

static int (*saved_target_fps_fp)(unsigned int vrefresh, unsigned int atomic_fps);
static int (*saved_te_level_decision_fp)(void *level_tb, unsigned int te_type,
					 unsigned int target_fps,
					 unsigned int *fps_level,
					 unsigned int *min_fps,
					 unsigned long x_time);
static int (*saved_slow_descent_fp)(unsigned int fps_level,
				    unsigned int fps_level_old,
				    unsigned int delay_frame_num);

int fpsgo2msync_hint_frameinfo(unsigned int render_tid, unsigned int reader_bufID,
			       unsigned int target_fps, unsigned int q2q_time,
			       unsigned int q2q_time2)
{
	fpsgo2msycn_target_fps = target_fps;
	fpsgo2msycn_q2q_time = q2q_time;
	fpsgo2msycn_q2q_time2 = q2q_time2;
	return 0;
}
/*
 * The stock msync2_frd.ko defines this symbol globally but does not export it;
 * keep it non-exported so the module does not add a provider that stock lacks.
 */

static int mtk_drm_get_target_fps(unsigned int vrefresh, unsigned int atomic_fps)
{
	unsigned int q2q_fps = 0;
	unsigned int target_fps;

	if (fpsgo2msycn_q2q_time / 1000)
		q2q_fps = 1000000 / (fpsgo2msycn_q2q_time / 1000);

	target_fps = max(fpsgo2msycn_target_fps, atomic_fps);
	target_fps = max(target_fps, q2q_fps);
	target_fps = min(vrefresh, target_fps);

	current_vrefresh = vrefresh;
	return target_fps;
}

static int mtk_sync_slow_descent(unsigned int fps_level,
				 unsigned int fps_level_old,
				 unsigned int delay_frame_num)
{
	bool descend;

	if (fps_level < fps_level_old) {
		descend = fpsgo2msync_delay_frame < delay_frame_num;
		fpsgo2msync_delay_frame = descend ?
			fpsgo2msync_delay_frame + 1 : 0;
	} else {
		descend = false;
		fpsgo2msync_delay_frame = 0;
	}

	return descend;
}

static int mtk_sync_te_level_decision(void *level_tb, unsigned int te_type,
				      unsigned int target_fps,
				      unsigned int *fps_level,
				      unsigned int *min_fps,
				      unsigned long x_time)
{
	struct msync_level_table *table = level_tb;
	unsigned long limit = 0;
	unsigned int i, max_idx = 0, max_fps;
	unsigned int idx = MSYNC_LEVELS;
	bool found = false;

	if (!table || !fps_level || !min_fps)
		return -EINVAL;

	if (te_type - 1 < ARRAY_SIZE(msync_te_x_time_limit))
		limit = msync_te_x_time_limit[te_type - 1];

	for (i = 0; i < MSYNC_LEVELS; i++) {
		if (table[i].level_fps >= target_fps) {
			idx = i;
			found = true;
			break;
		}
	}

	if (found) {
		*fps_level = table[idx].level_fps;
		*min_fps = table[idx].min_fps;
	} else {
		/* The stock code reads one entry past the table here. */
		*fps_level = 0;
		*min_fps = 0;
	}

	if (found && x_time < 1000000 && limit < x_time &&
	    idx < MSYNC_LEVELS - 1 &&
	    table[idx].min_fps <= target_fps) {
		unsigned int next_fps = table[idx + 1].level_fps;

		if (table[idx].level_fps < next_fps && next_fps <= current_vrefresh) {
			*fps_level = next_fps;
			*min_fps = table[idx + 1].min_fps;
		}
	}

	max_fps = table[0].level_fps;
	for (i = 1; i < MSYNC_LEVELS; i++) {
		if (table[i].level_fps > max_fps) {
			max_fps = table[i].level_fps;
			max_idx = i;
		}
	}

	if (max_fps <= target_fps) {
		*fps_level = table[max_idx].level_fps;
		*min_fps = table[max_idx].min_fps;
	}

	return 0;
}

static int __init msync2_frd_init(void)
{
	saved_target_fps_fp = mtk_drm_get_target_fps_fp;
	saved_te_level_decision_fp = mtk_sync_te_level_decision_fp;
	saved_slow_descent_fp = mtk_sync_slow_descent_fp;

	mtk_drm_get_target_fps_fp = mtk_drm_get_target_fps;
	mtk_sync_te_level_decision_fp = mtk_sync_te_level_decision;
	mtk_sync_slow_descent_fp = mtk_sync_slow_descent;
	return 0;
}

static void __exit msync2_frd_exit(void)
{
	mtk_drm_get_target_fps_fp = saved_target_fps_fp;
	mtk_sync_te_level_decision_fp = saved_te_level_decision_fp;
	mtk_sync_slow_descent_fp = saved_slow_descent_fp;
}

module_init(msync2_frd_init);
module_exit(msync2_frd_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek MSYNC2");
MODULE_AUTHOR("MediaTek Inc.");
MODULE_SOFTDEP("pre: mediatek-drm");
