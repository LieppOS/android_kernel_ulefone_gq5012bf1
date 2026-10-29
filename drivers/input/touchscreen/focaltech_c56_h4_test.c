/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host test of the privately extracted ACTUAL C56-H4 recovery worker.
 * All hardware/API operations below are explicit mocks; no controller
 * firmware or Linux kernel code is executed. The extracted header is
 * generated from the exact H4 private source and the H3 negative control.
 *
 * This is not KUnit, a kernel execution, or hardware evidence.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint8_t u8;
struct work_struct {
	int unused;
};
struct fts_ts_data {
	bool fw_is_running;
};
struct setting {
	u8 upgsts_boot;
};
struct fts_upgrade {
	struct work_struct fwrecover_work;
	struct fts_ts_data *ts_data;
	struct setting *setting_nf;
};

static int read_result, read_value, bootid_result;
static int resume_result, resume_running;
static int resume_calls, recover_calls, info_logs, error_logs;
static struct fts_upgrade *current;

#define FW_AUTO 0
#define FTS_REG_BOOT_STATE 0xd0
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define FTS_FUNC_ENTER() ((void)0)
#define FTS_FUNC_EXIT() ((void)0)
#define FTS_INFO(...) (++info_logs)
#define FTS_ERROR(...) (++error_logs)

static int fts_read_reg(int reg, u8 *out)
{
	assert(reg == FTS_REG_BOOT_STATE);
	if (read_result)
		return read_result;
	*out = (u8)read_value;
	return 0;
}

static int fts_check_bootid(void)
{
	return bootid_result;
}

static int fts_fw_resume(bool need_reset, int type)
{
	assert(!need_reset && type == FW_AUTO);
	resume_calls++;
	current->ts_data->fw_is_running = resume_running;
	return resume_result;
}

static void fts_tp_state_recovery(struct fts_ts_data *ts)
{
	assert(ts == current->ts_data);
	recover_calls++;
}

#include "c56h4-worker-extracted.h"

static struct fts_ts_data ts;
static struct setting setting;
static struct fts_upgrade upg;

static void reset_case(int rr, int rv, int bi, int fr, int running)
{
	ts.fw_is_running = true;
	setting.upgsts_boot = 2;
	upg.ts_data = &ts;
	upg.setting_nf = &setting;
	current = &upg;
	read_result = rr;
	read_value = rv;
	bootid_result = bi;
	resume_result = fr;
	resume_running = running;
	resume_calls = recover_calls = info_logs = error_logs = 0;
}

static void expect(int fw, int resumes, int recovers, int infos, int errors)
{
	assert(ts.fw_is_running == fw);
	assert(resume_calls == resumes);
	assert(recover_calls == recovers);
	assert(info_logs == infos);
	assert(error_logs == errors);
}

int main(int argc, char **argv)
{
	int which;

	assert(argc == 2);
	which = atoi(argv[1]);

	switch (which) {
	case 0: /* verified success */
		reset_case(0, 2, 0, 0, true);
		fts_fwrecover_work(&upg.fwrecover_work);
		expect(true, 1, 1, 2, 0);
		break;
	case 1: /* negative control: H3 accepts a failed download */
		reset_case(0, 2, 0, -EIO, false);
		c56_h3_old_fwrecover_work(&upg.fwrecover_work);
		assert(ts.fw_is_running);
		assert(resume_calls == 1 && recover_calls == 1);
		assert(info_logs == 2 && error_logs == 0);
		puts("NEGATIVE_CONTROL_REPRODUCED");
		break;
	case 2: /* PRAM/ECC/start failure */
		reset_case(0, 2, 0, -EIO, false);
		fts_fwrecover_work(&upg.fwrecover_work);
		expect(false, 1, 0, 1, 1);
		break;
	case 3: /* resume returns 0 but no firmware was started */
		reset_case(0, 2, 0, 0, false);
		fts_fwrecover_work(&upg.fwrecover_work);
		expect(false, 1, 0, 1, 1);
		break;
	case 4: /* boot-state read failure */
		reset_case(-EIO, 2, 0, 0, true);
		fts_fwrecover_work(&upg.fwrecover_work);
		expect(false, 0, 0, 0, 1);
		break;
	case 5: /* boot-ID mismatch/read failure */
		reset_case(0, 2, -EIO, 0, true);
		fts_fwrecover_work(&upg.fwrecover_work);
		expect(false, 0, 0, 0, 1);
		break;
	case 6: /* not in boot mode: preserve original running assumption */
		reset_case(0, 0, 0, 0, true);
		fts_fwrecover_work(&upg.fwrecover_work);
		expect(true, 0, 0, 0, 0);
		break;
	case 7: /* any non-zero resume return is rejected */
		reset_case(0, 2, 0, 1, true);
		fts_fwrecover_work(&upg.fwrecover_work);
		expect(false, 1, 0, 1, 1);
		break;
	case 8: /* a failed attempt leaves a retryable state */
		reset_case(0, 2, 0, -EIO, false);
		fts_fwrecover_work(&upg.fwrecover_work);
		expect(false, 1, 0, 1, 1);
		resume_result = 0;
		resume_running = true;
		fts_fwrecover_work(&upg.fwrecover_work);
		expect(true, 2, 1, 3, 1);
		break;
	default:
		abort();
	}

	printf("PASS %d\n", which);
	return 0;
}
