/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 LieppOS contributors */
#ifndef LIEPPOS_OIS_DIAG_PROTOCOL_H
#define LIEPPOS_OIS_DIAG_PROTOCOL_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

/* No caller-selectable address, register, length or register-data payload. */
#define OIS_DIAG_PROTOCOL 2
#define OIS_DIAG_BUS 4
#define OIS_DIAG_ADDR 0x0a
#define OIS_DIAG_MODEL_REG 0x7000
#define OIS_DIAG_ACK "I_ACCEPT_UNIDENTIFIED_DEVICE_ID_POINTER_WRITE"

struct ois_diag_result {
	unsigned short model_word;
	unsigned int valid_mask;
};

/* Return the number of I2C messages completed, or a negative errno. */
typedef int (*ois_diag_transfer_fn)(void *context,
				  const unsigned char pointer[2],
				  unsigned char response[2]);

static inline int ois_diag_gate(int ready, int enabled, int privileged,
				int attempted, const char *ack)
{
	if (!ready)
		return -ESHUTDOWN;
	if (!enabled)
		return -EACCES;
	if (!privileged)
		return -EPERM;
	if (attempted)
		return -EALREADY;
	if (strcmp(ack, OIS_DIAG_ACK) && strcmp(ack, OIS_DIAG_ACK "\n"))
		return -EINVAL;
	return 0;
}

static inline int ois_diag_pointer_allowed(const unsigned char pointer[2])
{
	return pointer[0] == (OIS_DIAG_MODEL_REG >> 8) &&
		pointer[1] == (OIS_DIAG_MODEL_REG & 0xff);
}

/*
 * One candidate firmware/model identity word, NOT an immutable silicon ID.
 * Return raw data only: no automatic detection verdict or fallback register.
 */
static inline int ois_diag_collect(ois_diag_transfer_fn transfer, void *context,
				   struct ois_diag_result *result)
{
	const unsigned char pointer[2] = {
		OIS_DIAG_MODEL_REG >> 8, OIS_DIAG_MODEL_REG & 0xff,
	};
	unsigned char response[2] = { 0, 0 };
	int ret;

	memset(result, 0, sizeof(*result));
	ret = transfer(context, pointer, response);
	if (ret < 0)
		return ret;
	/* Unlike the OEM helper, reject partial/nonexact message counts. */
	if (ret != 2)
		return -EIO;
	result->model_word = ((unsigned short)response[0] << 8) | response[1];
	result->valid_mask = 1;
	return 0;
}
#endif
