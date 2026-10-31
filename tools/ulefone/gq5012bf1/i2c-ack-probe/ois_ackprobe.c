// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 LieppOS contributors
/*
 * Experimental address-ACK probe for locating a possible DW9781C OIS
 * controller. Each probe is ONE single-message, one-byte I2C read: there is
 * no write phase, so no register pointer or register value is sent. Only a
 * fixed table of (bus, address) pairs can be probed, each bus at most once.
 * Positive controls are the camera AF drivers the stock HAL itself drives at
 * 7-bit 0x0c; they only ACK while their camera is streaming.
 */
#include <linux/bitmap.h>
#include <linux/capability.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/string.h>

#define ACKPROBE_PHRASE ":I_ACCEPT_ADDRESS_ONLY_READ_PROBE"
#define ACKSCAN_PHRASE ":I_ACCEPT_ADDRESS_ONLY_READ_SCAN"
#define ACKSCAN_FIRST 0x08
#define ACKSCAN_LAST 0x77
#define ACKPROBE_MAX 4

struct ackprobe_bus {
	int nr;
	unsigned short addr[ACKPROBE_MAX];
	bool done;
	int rc[ACKPROBE_MAX];
	unsigned char byte[ACKPROBE_MAX];
	/* Inventory scan: addresses owned by bound drivers are never touched. */
	unsigned short skip[ACKPROBE_MAX + 1];
	bool scanned;
	int scan_error;
	DECLARE_BITMAP(ack, 128);
	DECLARE_BITMAP(other, 128);
};

/* addr[0] is always the HAL-driven AF positive control (7-bit 0x0c). */
static struct ackprobe_bus buses[] = {
	{ .nr = 4, .addr = { 0x0c, 0x72, 0x0e, 0x0a },
	  .skip = { 0x0c, 0x10, 0x20, 0x50, 0x51 } },
	{ .nr = 7, .addr = { 0x0c, 0x72, 0x0e, 0x0a },
	  .skip = { 0x0c, 0x20, 0x51 } },
	{ .nr = 2, .addr = { 0x0c, 0x72, 0x0e, 0x0a },
	  .skip = { 0x0c, 0x10, 0x50 } },
};

static bool enable_probe;
static bool ready;
static DEFINE_MUTEX(probe_lock);

static int enable_set(const char *value, const struct kernel_param *parameter)
{
	int ret;

	mutex_lock(&probe_lock);
	ret = ready ? -EPERM : param_set_bool(value, parameter);
	mutex_unlock(&probe_lock);
	return ret;
}

static const struct kernel_param_ops enable_ops = {
	.set = enable_set,
	.get = param_get_bool,
};
module_param_cb(enable_probe, &enable_ops, &enable_probe, 0400);
MODULE_PARM_DESC(enable_probe, "Immutable opt-in; probes still need an explicit request");

static int probe_one(struct i2c_adapter *adapter, unsigned short addr,
		     unsigned char *byte)
{
	struct i2c_msg message = {
		.addr = addr,
		.flags = I2C_M_RD,
		.len = 1,
		.buf = byte,
	};

	return i2c_transfer(adapter, &message, 1);
}

static int probe_set(const char *value, const struct kernel_param *parameter)
{
	struct ackprobe_bus *bus = NULL;
	struct i2c_adapter *adapter;
	int i, ret = 0;

	(void)parameter;
	/* Exact "<single digit bus><phrase>", no trailing newline accepted. */
	if (strlen(value) != 1 + strlen(ACKPROBE_PHRASE) ||
	    strcmp(value + 1, ACKPROBE_PHRASE))
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(buses); i++)
		if (value[0] == '0' + buses[i].nr)
			bus = &buses[i];
	if (!bus)
		return -EXDEV;

	mutex_lock(&probe_lock);
	if (!ready || !enable_probe) {
		ret = -EPERM;
		goto out;
	}
	if (!capable(CAP_SYS_RAWIO)) {
		ret = -EACCES;
		goto out;
	}
	if (bus->done) {
		ret = -EALREADY;
		goto out;
	}
	adapter = i2c_get_adapter(bus->nr);
	if (!adapter) {
		ret = -ENODEV;
		goto out;
	}
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		ret = -EOPNOTSUPP;
		goto put;
	}
	/* Consume before the first bus access, whatever the outcome. */
	bus->done = true;
	for (i = 0; i < ACKPROBE_MAX; i++) {
		bus->byte[i] = 0;
		bus->rc[i] = probe_one(adapter, bus->addr[i], &bus->byte[i]);
	}
put:
	i2c_put_adapter(adapter);
out:
	mutex_unlock(&probe_lock);
	return ret;
}

static bool scan_skipped(const struct ackprobe_bus *bus, unsigned short addr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bus->skip); i++)
		if (bus->skip[i] && bus->skip[i] == addr)
			return true;
	return false;
}

static int scan_set(const char *value, const struct kernel_param *parameter)
{
	struct ackprobe_bus *bus = NULL;
	struct i2c_adapter *adapter;
	unsigned char byte;
	unsigned short addr;
	int i, rc, ret = 0;

	(void)parameter;
	if (strlen(value) != 1 + strlen(ACKSCAN_PHRASE) ||
	    strcmp(value + 1, ACKSCAN_PHRASE))
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(buses); i++)
		if (value[0] == '0' + buses[i].nr)
			bus = &buses[i];
	if (!bus)
		return -EXDEV;

	mutex_lock(&probe_lock);
	if (!ready || !enable_probe) {
		ret = -EPERM;
		goto out;
	}
	if (!capable(CAP_SYS_RAWIO)) {
		ret = -EACCES;
		goto out;
	}
	if (bus->scanned) {
		ret = -EALREADY;
		goto out;
	}
	adapter = i2c_get_adapter(bus->nr);
	if (!adapter) {
		ret = -ENODEV;
		goto out;
	}
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		ret = -EOPNOTSUPP;
		goto put;
	}
	bus->scanned = true;
	for (addr = ACKSCAN_FIRST; addr <= ACKSCAN_LAST; addr++) {
		if (scan_skipped(bus, addr))
			continue;
		rc = probe_one(adapter, addr, &byte);
		if (rc == 1)
			set_bit(addr, bus->ack);
		else if (rc != -ENXIO)
			set_bit(addr, bus->other);
		if (rc != 1 && rc != -ENXIO)
			bus->scan_error = rc;
	}
put:
	i2c_put_adapter(adapter);
out:
	mutex_unlock(&probe_lock);
	return ret;
}

static int status_get(char *buffer, const struct kernel_param *parameter)
{
	int b, i, length = 0;

	(void)parameter;
	mutex_lock(&probe_lock);
	length += scnprintf(buffer + length, PAGE_SIZE - length,
			    "protocol=1 ready=%u enabled=%u\n", ready, enable_probe);
	for (b = 0; b < ARRAY_SIZE(buses); b++) {
		length += scnprintf(buffer + length, PAGE_SIZE - length,
				    "bus=%d done=%u", buses[b].nr, buses[b].done);
		for (i = 0; i < ACKPROBE_MAX && buses[b].done; i++)
			length += scnprintf(buffer + length, PAGE_SIZE - length,
					    " 0x%02x:%d/0x%02x", buses[b].addr[i],
					    buses[b].rc[i], buses[b].byte[i]);
		length += scnprintf(buffer + length, PAGE_SIZE - length,
				    "\nscan bus=%d done=%u ack=[%*pbl] other=[%*pbl] last_other=%d\n",
				    buses[b].nr, buses[b].scanned, 128, buses[b].ack,
				    128, buses[b].other, buses[b].scan_error);
	}
	mutex_unlock(&probe_lock);
	return length;
}

static const struct kernel_param_ops probe_ops = {
	.set = probe_set,
};

static const struct kernel_param_ops status_ops = {
	.get = status_get,
};

module_param_cb(probe, &probe_ops, NULL, 0200);
MODULE_PARM_DESC(probe, "One-shot per bus: \"<bus>" ACKPROBE_PHRASE "\"");
static const struct kernel_param_ops scan_ops = {
	.set = scan_set,
};

module_param_cb(scan, &scan_ops, NULL, 0200);
MODULE_PARM_DESC(scan, "One-shot per bus: read-only ACK inventory 0x08-0x77 minus skip list");
module_param_cb(status, &status_ops, NULL, 0400);
MODULE_PARM_DESC(status, "Cached results only (rc 1 = ACK, -6 = NACK)");

static int __init ois_ackprobe_init(void)
{
	mutex_lock(&probe_lock);
	ready = true;
	mutex_unlock(&probe_lock);
	pr_info("ois_ackscan: passive; no I2C operations performed at init\n");
	return 0;
}
module_init(ois_ackprobe_init);

/* Deliberately no unload path. */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LieppOS contributors");
MODULE_VERSION("2");
MODULE_DESCRIPTION("Fixed-table, read-only address-ACK probe for OIS localisation");
