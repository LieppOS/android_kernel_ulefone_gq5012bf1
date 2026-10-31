/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 LieppOS contributors */
#include <assert.h>
#include <stdio.h>
#include "../protocol.h"

struct mock {
	int returns[2];
	unsigned int calls;
	unsigned char data[2][2];
};

static int transfer(void *context, const unsigned char pointer[2],
		    unsigned char response[2])
{
	struct mock *mock = context;
	unsigned int i = mock->calls++;

	assert(i < 2);
	assert(pointer[0] == 0x70);
	assert(pointer[1] == i + 1);
	/* Deliberately poison even failed responses: caller must not expose them. */
	response[0] = mock->data[i][0];
	response[1] = mock->data[i][1];
	return mock->returns[i];
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
	assert(ois_diag_gate(1, 1, 1, 0, ack) == 0);
	assert(ois_diag_gate(1, 1, 1, 0, OIS_DIAG_ACK "\n") == 0);
}

static void success(unsigned char first, unsigned char second)
{
	struct mock mock = {
		.returns = { 2, 2 },
		.data = { { first, second }, { second, first } },
	};
	struct ois_diag_result result;

	memset(&result, 0xa5, sizeof(result));
	assert(ois_diag_collect(transfer, &mock, &result) == 0);
	assert(mock.calls == 2);
	assert(result.valid_mask == 3);
	assert(result.fw_version == ((unsigned int)first << 8 | second));
	assert(result.fw_date == ((unsigned int)second << 8 | first));
	/* Zero/all-ones remain raw values, never an automatic identity verdict. */
}

static void failures(void)
{
	const int returns[] = { -EREMOTEIO, -EAGAIN, -ETIMEDOUT, 0, 1, 3 };
	unsigned int i, stage;

	for (stage = 0; stage < 2; stage++) {
		for (i = 0; i < sizeof(returns) / sizeof(returns[0]); i++) {
			struct mock mock = {
				.returns = { 2, 2 },
				.data = { { 0x12, 0x34 }, { 0x56, 0x78 } },
			};
			struct ois_diag_result result;
			int expected = returns[i] < 0 ? returns[i] : -EIO;

			mock.returns[stage] = returns[i];
			memset(&result, 0xa5, sizeof(result));
			assert(ois_diag_collect(transfer, &mock, &result) == expected);
			assert(mock.calls == stage + 1); /* Never retry a failed word. */
			assert(result.valid_mask == stage);
			assert(result.fw_version == (stage ? 0x1234 : 0));
			assert(result.fw_date == 0);
		}
	}
}

int main(void)
{
	gates();
	success(0x12, 0x34);
	success(0, 0);
	success(0xff, 0xff);
	failures();
	puts("PASS: 10 gate cases, 3 raw-value/endian cases, 12 failure cases; fixed two-word protocol");
	return 0;
}
