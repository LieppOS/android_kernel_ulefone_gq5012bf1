// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 LieppOS contributors
/*
 * Experimental read-only peek used to characterise an unexplained I2C-4
 * responder. Every request is ONE single-message read of 1..4 bytes from
 * the device's current address: no write phase exists in this module. Only
 * I2C-4 and a fixed address allowlist are accepted, with a hard request cap.
 */
#include <linux/capability.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/string.h>

#define PEEK_BUS 4
#define PEEK_PHRASE ":I_ACCEPT_CURRENT_ADDRESS_READ"
#define PEEK_MAX_REQUESTS 16
#define PEEK_MAX_LEN 4

/* 0x0c = HAL-driven AF control, 0x21 = unexplained, 0x22 = OV64B alt id. */
static const unsigned short allowed[] = { 0x0c, 0x21, 0x22 };

struct peek_record {
	unsigned short addr;
	unsigned char len;
	int rc;
	unsigned char data[PEEK_MAX_LEN];
};

static struct peek_record records[PEEK_MAX_REQUESTS];
static unsigned int used;
static bool enable_peek;
static bool ready;
static DEFINE_MUTEX(peek_lock);

static int enable_set(const char *value, const struct kernel_param *parameter)
{
	int ret;

	mutex_lock(&peek_lock);
	ret = ready ? -EPERM : param_set_bool(value, parameter);
	mutex_unlock(&peek_lock);
	return ret;
}

static const struct kernel_param_ops enable_ops = {
	.set = enable_set,
	.get = param_get_bool,
};
module_param_cb(enable_peek, &enable_ops, &enable_peek, 0400);
MODULE_PARM_DESC(enable_peek, "Immutable opt-in");

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

/* Exact format "AA:N<phrase>": two lower-case hex digits, length 1..4. */
static int peek_set(const char *value, const struct kernel_param *parameter)
{
	struct peek_record *record;
	struct i2c_adapter *adapter;
	struct i2c_msg message;
	unsigned short addr;
	int hi, lo, len, i, ret = 0;
	bool ok = false;

	(void)parameter;
	if (strlen(value) != 4 + strlen(PEEK_PHRASE) || value[2] != ':' ||
	    strcmp(value + 4, PEEK_PHRASE))
		return -EINVAL;
	hi = hex_digit(value[0]);
	lo = hex_digit(value[1]);
	len = value[3] - '0';
	if (hi < 0 || lo < 0 || len < 1 || len > PEEK_MAX_LEN)
		return -EINVAL;
	addr = hi << 4 | lo;
	for (i = 0; i < ARRAY_SIZE(allowed); i++)
		ok |= allowed[i] == addr;
	if (!ok)
		return -EXDEV;

	mutex_lock(&peek_lock);
	if (!ready || !enable_peek) {
		ret = -EPERM;
		goto out;
	}
	if (!capable(CAP_SYS_RAWIO)) {
		ret = -EACCES;
		goto out;
	}
	if (used >= PEEK_MAX_REQUESTS) {
		ret = -EDQUOT;
		goto out;
	}
	adapter = i2c_get_adapter(PEEK_BUS);
	if (!adapter) {
		ret = -ENODEV;
		goto out;
	}
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		ret = -EOPNOTSUPP;
		goto put;
	}
	record = &records[used++];
	record->addr = addr;
	record->len = len;
	message.addr = addr;
	message.flags = I2C_M_RD;
	message.len = len;
	message.buf = record->data;
	record->rc = i2c_transfer(adapter, &message, 1);
put:
	i2c_put_adapter(adapter);
out:
	mutex_unlock(&peek_lock);
	return ret;
}

static int status_get(char *buffer, const struct kernel_param *parameter)
{
	int i, j, length;

	(void)parameter;
	mutex_lock(&peek_lock);
	length = scnprintf(buffer, PAGE_SIZE, "protocol=1 bus=%d used=%u/%d\n",
			   PEEK_BUS, used, PEEK_MAX_REQUESTS);
	for (i = 0; i < used; i++) {
		length += scnprintf(buffer + length, PAGE_SIZE - length,
				    "%d 0x%02x rc=%d", i, records[i].addr,
				    records[i].rc);
		for (j = 0; j < records[i].len && records[i].rc == 1; j++)
			length += scnprintf(buffer + length, PAGE_SIZE - length,
					    " %02x", records[i].data[j]);
		length += scnprintf(buffer + length, PAGE_SIZE - length, "\n");
	}
	mutex_unlock(&peek_lock);
	return length;
}

static const struct kernel_param_ops peek_ops = {
	.set = peek_set,
};

static const struct kernel_param_ops status_ops = {
	.get = status_get,
};

module_param_cb(peek, &peek_ops, NULL, 0200);
MODULE_PARM_DESC(peek, "\"AA:N" PEEK_PHRASE "\" current-address read, I2C-4 allowlist");
module_param_cb(status, &status_ops, NULL, 0400);
MODULE_PARM_DESC(status, "Cached results only");

static int __init ois_i2cpeek_init(void)
{
	mutex_lock(&peek_lock);
	ready = true;
	mutex_unlock(&peek_lock);
	pr_info("ois_i2cpeek: passive; no I2C operations performed at init\n");
	return 0;
}
module_init(ois_i2cpeek_init);

/* Deliberately no unload path. */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LieppOS contributors");
MODULE_VERSION("1");
MODULE_DESCRIPTION("Allowlisted read-only current-address peek on I2C-4");
