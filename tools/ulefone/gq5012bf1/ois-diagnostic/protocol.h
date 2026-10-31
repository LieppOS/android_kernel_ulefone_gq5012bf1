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
#define OIS_DIAG_BUS 4
#define OIS_DIAG_ADDR 0x0a
#define OIS_DIAG_ACK "I_ACCEPT_REGISTER_POINTER_BUS_WRITES"

struct ois_diag_result {
	unsigned short fw_version;
	unsigned short fw_date;
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

/* These are FW metadata registers, NOT a documented silicon-ID pair. */
static inline int ois_diag_collect(ois_diag_transfer_fn transfer, void *context,
				   struct ois_diag_result *result)
{
	unsigned int i;

	memset(result, 0, sizeof(*result));
	for (i = 0; i < 2; i++) {
		const unsigned char pointer[2] = { 0x70, (unsigned char)(i + 1) };
		unsigned char response[2] = { 0, 0 };
		unsigned short value;
		int ret;

		ret = transfer(context, pointer, response);
		if (ret < 0)
			return ret;
		/* Unlike the reference driver, reject partial/nonexact transfers. */
		if (ret != 2)
			return -EIO;
		value = ((unsigned short)response[0] << 8) | response[1];
		if (i == 0)
			result->fw_version = value;
		else
			result->fw_date = value;
		result->valid_mask |= 1U << i;
	}
	return 0;
}
#endif
