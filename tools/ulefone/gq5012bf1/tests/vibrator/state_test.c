/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2026 The LieppOS Contributors */
/* Tests actual shared policy code; NOT kernel timers, regulator hardware or KUnit. */
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "regulator-vibrator-state.h"

#define MS(x) ((unsigned long long)(x) * 1000000ULL)

static struct reg_vibr_state shared;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long long clock_ns;

static void *stress(void *unused)
{
	unsigned int i;

	(void)unused;
	for (i = 0; i < 10000; i++) {
		pthread_mutex_lock(&lock);
		clock_ns += MS(1);
		assert(reg_vibr_start(&shared, clock_ns, 1 + i % REG_VIBR_MAX_MS) == REG_VIBR_OK);
		assert(shared.deadline_ns > clock_ns);
		assert(shared.deadline_ns - clock_ns <= MS(REG_VIBR_MAX_MS));
		reg_vibr_cancel(&shared);
		assert(!reg_vibr_due(&shared, ULLONG_MAX));
		pthread_mutex_unlock(&lock);
	}
	return NULL;
}

int main(void)
{
	struct reg_vibr_state state = { 0 }, before;
	pthread_t threads[4];
	unsigned int i;
	int passed = 0;

	assert(REG_VIBR_MAX_MS > 0 && REG_VIBR_MAX_MS <= INT_MAX);
	assert(reg_vibr_start(&state, MS(10), REG_VIBR_MAX_MS) == REG_VIBR_OK);
	assert(state.active && state.deadline_ns == MS(10 + REG_VIBR_MAX_MS));
	puts("PASS bounded activation"); passed++;

	before = state;
	assert(reg_vibr_start(&state, MS(20), REG_VIBR_MAX_MS + 1) == REG_VIBR_INVALID);
	assert(state.active == before.active && state.deadline_ns == before.deadline_ns);
	puts("PASS oversized request preserves old bounded request"); passed++;

	assert(reg_vibr_start(&state, MS(20), 0) == REG_VIBR_INVALID);
	assert(state.deadline_ns == before.deadline_ns);
	puts("PASS zero is cancellation not activation"); passed++;

	assert(reg_vibr_start(&state, MS(100), 50) == REG_VIBR_OK);
	assert(!reg_vibr_due(&state, MS(149)));
	assert(reg_vibr_due(&state, MS(150)) && reg_vibr_due(&state, MS(151)));
	puts("PASS exact expiry boundary"); passed++;

	assert(reg_vibr_start(&state, MS(140), 500) == REG_VIBR_OK);
	assert(!reg_vibr_due(&state, MS(150)));
	assert(reg_vibr_due(&state, MS(640)));
	puts("PASS stale queued expiry cannot cancel replacement"); passed++;

	reg_vibr_cancel(&state);
	assert(!state.active && !reg_vibr_due(&state, ULLONG_MAX));
	puts("PASS cancelled generation stays cancelled"); passed++;

	reg_vibr_fail(&state);
	assert(!state.active && state.fault);
	assert(reg_vibr_start(&state, 0, 1) == REG_VIBR_FAULT);
	puts("PASS asynchronous fault prevents reenable"); passed++;

	reg_vibr_off_result(&state, true, false);
	assert(state.fault && !state.active);
	puts("PASS background recovery does not silently clear fault"); passed++;

	reg_vibr_off_result(&state, false, true);
	assert(state.fault);
	reg_vibr_off_result(&state, true, true);
	assert(!state.fault && reg_vibr_start(&state, 0, 1) == REG_VIBR_OK);
	puts("PASS only accepted explicit off acknowledges fault"); passed++;

	reg_vibr_quiesce(&state);
	assert(state.blocked && !state.active);
	assert(reg_vibr_start(&state, 0, 1) == REG_VIBR_BLOCKED);
	state.blocked = false;
	assert(!state.active && !reg_vibr_due(&state, ULLONG_MAX));
	puts("PASS suspend teardown and resume do not resurrect activation"); passed++;

	before = state;
	assert(reg_vibr_start(&state, LLONG_MAX, 1) == REG_VIBR_INVALID);
	assert(state.active == before.active && state.deadline_ns == before.deadline_ns);
	puts("PASS signed kernel time overflow rejected"); passed++;

	memset(&shared, 0, sizeof(shared));
	for (i = 0; i < 4; i++)
		assert(!pthread_create(&threads[i], NULL, stress, NULL));
	for (i = 0; i < 4; i++)
		assert(!pthread_join(threads[i], NULL));
	assert(!shared.active);
	puts("PASS serialized concurrent request cancellation stress"); passed++;

	printf("TOTAL %d/12\n", passed);
	return passed == 12 ? 0 : 1;
}
