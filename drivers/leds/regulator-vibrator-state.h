/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2026 The LieppOS Contributors */
#ifndef _REGULATOR_VIBRATOR_STATE_H
#define _REGULATOR_VIBRATOR_STATE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#endif

/* Software protection policy, NOT an established physical/thermal rating. */
#define REG_VIBR_MAX_MS 5000U
#define REG_VIBR_RETRY_MS 250U

/* Caller serializes every access. Time is monotonic/boottime nanoseconds. */
struct reg_vibr_state {
	unsigned long long deadline_ns;
	bool active;
	bool blocked;
	bool fault;
};

enum reg_vibr_start_result {
	REG_VIBR_OK,
	REG_VIBR_INVALID,
	REG_VIBR_BLOCKED,
	REG_VIBR_FAULT,
};

static inline enum reg_vibr_start_result
reg_vibr_start(struct reg_vibr_state *state, unsigned long long now_ns,
	       unsigned int duration_ms)
{
	unsigned long long delta = (unsigned long long)duration_ms * 1000000ULL;

	if (!duration_ms || duration_ms > REG_VIBR_MAX_MS ||
	    now_ns > (~0ULL >> 1) - delta)
		return REG_VIBR_INVALID;
	if (state->blocked)
		return REG_VIBR_BLOCKED;
	if (state->fault)
		return REG_VIBR_FAULT;
	state->deadline_ns = now_ns + delta;
	state->active = true;
	return REG_VIBR_OK;
}

static inline bool reg_vibr_due(const struct reg_vibr_state *state,
			       unsigned long long now_ns)
{
	return state->active && now_ns >= state->deadline_ns;
}

static inline void reg_vibr_cancel(struct reg_vibr_state *state)
{
	state->active = false;
}

static inline void reg_vibr_fail(struct reg_vibr_state *state)
{
	state->active = false;
	state->fault = true;
}

static inline void reg_vibr_off_result(struct reg_vibr_state *state,
				      bool success, bool explicit_off)
{
	state->active = false;
	if (!success)
		state->fault = true;
	else if (explicit_off)
		state->fault = false;
}

static inline void reg_vibr_quiesce(struct reg_vibr_state *state)
{
	state->blocked = true;
	state->active = false;
}

#endif
