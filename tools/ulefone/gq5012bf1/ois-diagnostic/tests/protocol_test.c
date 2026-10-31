/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 LieppOS contributors */
#include <assert.h>
#include <stdio.h>
#include "../protocol.h"

struct mock {
	int returned;
	unsigned int calls;
	unsigned char data[2];
};

static int transfer(void *context, const unsigned char pointer[2],
		    unsigned char response[2])
{
	struct mock *mock = context;

	assert(mock->calls++ == 0);
	assert(pointer[0] == 0x70 && pointer[1] == 0);
	assert(ois_diag_pointer_allowed(pointer));
	/* Poison failed responses too: no failed read may expose these bytes. */
	response[0] = mock->data[0];
	response[1] = mock->data[1];
	return mock->returned;
}

static void gates(void)
{
	const char *ack = OIS_DIAG_ACK;

	assert(ois_diag_gate(0, 1, 1, 0, ack) == -ESHUTDOWN);
	assert(ois_diag_gate(1, 0, 1, 0, ack) == -EACCES);
	assert(ois_diag_gate(1, 1, 0, 0, ack) == -EPERM);
	assert(ois_diag_gate(1, 1, 1, 1, ack) == -EALREADY);
	assert(ois_diag_gate(1, 1, 1, 0, "") == -EINVAL);
	assert(ois_diag_gate(1, 1, 1, 0, "1") == -EINVAL);
	assert(ois_diag_gate(1, 1, 1, 0, OIS_DIAG_ACK " ") == -EINVAL);
	assert(ois_diag_gate(1, 1, 1, 0, OIS_DIAG_ACK "\n\n") == -EINVAL);
	assert(ois_diag_gate(1, 1, 1, 0,
			     "I_ACCEPT_REGISTER_POINTER_BUS_WRITES") == -EINVAL);
	assert(ois_diag_gate(1, 1, 1, 0, ack) == 0);
	assert(ois_diag_gate(1, 1, 1, 0, OIS_DIAG_ACK "\n") == 0);
}

static void selectors(void)
{
	const unsigned char allowed[2] = { 0x70, 0 };
	const unsigned char forbidden[][2] = {
		{ 0x70, 1 }, { 0x70, 2 }, { 0xd0, 0x60 }, { 0, 0 },
	};
	unsigned int i;

	assert(ois_diag_pointer_allowed(allowed));
	for (i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); i++)
		assert(!ois_diag_pointer_allowed(forbidden[i]));
}

static void success(unsigned char first, unsigned char second)
{
	struct mock mock = {
		.returned = 2,
		.data = { first, second },
	};
	struct ois_diag_result result;

	memset(&result, 0xa5, sizeof(result));
	assert(ois_diag_collect(transfer, &mock, &result) == 0);
	assert(mock.calls == 1);
	assert(result.valid_mask == 1);
	assert(result.model_word == ((unsigned int)first << 8 | second));
	/* Even 0x9781 is only raw data here, not a physical-identity verdict. */
}

static void failures(void)
{
	const int returns[] = {
		-ENXIO, -EREMOTEIO, -EAGAIN, -ETIMEDOUT, -EIO, 0, 1, 3,
	};
	unsigned int i;

	for (i = 0; i < sizeof(returns) / sizeof(returns[0]); i++) {
		struct mock mock = {
			.returned = returns[i],
			.data = { 0x97, 0x81 },
		};
		struct ois_diag_result result;
		int expected = returns[i] < 0 ? returns[i] : -EIO;

		memset(&result, 0xa5, sizeof(result));
		assert(ois_diag_collect(transfer, &mock, &result) == expected);
		assert(mock.calls == 1); /* No retry, second word or fallback. */
		assert(result.valid_mask == 0);
		assert(result.model_word == 0);
	}
}

int main(void)
{
	gates();
	selectors();
	success(0x97, 0x81);
	success(0x12, 0x34);
	success(0, 0);
	success(0xff, 0xff);
	failures();
	puts("PASS: 11 gates, 5 selectors, 4 raw/endian cases, 8 failures; one fixed model word only");
	return 0;
}
