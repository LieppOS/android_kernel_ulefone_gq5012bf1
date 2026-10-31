// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 LieppOS contributors
/* Experimental offline-built diagnostic. Not an OIS activation driver. */
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

#include "protocol.h"

#define OIS_DIAG_DT_PATH "/soc/i2c@11c23000/camera-ois-main@e"

static bool enable_sample;
static DEFINE_MUTEX(diag_lock);
static bool ready;
static bool attempted;
static int last_error;
static struct ois_diag_result result;

/* Enforce immutability in code, not just in sysfs permission bits. */
static int enable_set(const char *value, const struct kernel_param *parameter)
{
	int ret;

	mutex_lock(&diag_lock);
	ret = ready ? -EPERM : param_set_bool(value, parameter);
	mutex_unlock(&diag_lock);
	return ret;
}

static const struct kernel_param_ops enable_ops = {
	.set = enable_set,
	.get = param_get_bool,
};
module_param_cb(enable_sample, &enable_ops, &enable_sample, 0400);
MODULE_PARM_DESC(enable_sample,
		 "Immutable opt-in; still requires an explicit post-load sample request");

/* Caller holds the client device lock. No power or binding changes. */
static int validate_target(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	if (client->adapter->nr != OIS_DIAG_BUS ||
	    client->addr != OIS_DIAG_ADDR || client->flags != 0)
		return -EXDEV;
	if (!dev->of_node ||
	    strcmp(of_node_full_name(dev->of_node), OIS_DIAG_DT_PATH) ||
	    !of_device_is_compatible(dev->of_node, "mediatek,dw9781c"))
		return -ENODEV;
	if (!dev->driver || strcmp(dev->driver->name, "dw9781c"))
		return -ENODEV;
	return 0;
}

static int transfer_word(void *context, const unsigned char pointer[2],
			 unsigned char response[2])
{
	struct i2c_client *client = context;
	unsigned char selector[2] = { pointer[0], pointer[1] };
	struct i2c_msg messages[2] = {
		{
			.addr = OIS_DIAG_ADDR,
			.flags = 0,
			.len = sizeof(selector),
			.buf = selector,
		},
		{
			.addr = OIS_DIAG_ADDR,
			.flags = I2C_M_RD,
			.len = 2,
			.buf = response,
		},
	};

	/* Defense in depth: even an internal caller cannot select other words. */
	if (selector[0] != 0x70 || (selector[1] != 1 && selector[1] != 2))
		return -EINVAL;
	return i2c_transfer(client->adapter, messages, ARRAY_SIZE(messages));
}

static int sample_set(const char *value, const struct kernel_param *parameter)
{
	struct i2c_client *client;
	struct device *dev;
	int ret;

	(void)parameter;
	mutex_lock(&diag_lock);
	ret = ois_diag_gate(ready, enable_sample, capable(CAP_SYS_RAWIO),
			    attempted, value);
	if (ret)
		goto out;

	/* DT registered name remains 4-000e although the driver uses 0x0a. */
	dev = bus_find_device_by_name(&i2c_bus_type, NULL, "4-000e");
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}
	device_lock(dev);
	client = i2c_verify_client(dev);
	if (!client) {
		ret = -ENODEV;
		goto unlock;
	}
	ret = validate_target(client);
	if (ret)
		goto unlock;

	/* No resume/power-on callback: refuse disabled or suspended runtime PM. */
	ret = pm_runtime_get_if_active(dev, true);
	if (ret <= 0) {
		ret = ret < 0 ? ret : -EHOSTDOWN;
		goto unlock;
	}
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		ret = -EOPNOTSUPP;
		goto put_pm;
	}

	/* Consume permanently BEFORE the first possible bus access, even on NACK. */
	attempted = true;
	ret = ois_diag_collect(transfer_word, client, &result);
put_pm:
	/* Balance the usage ref without scheduling idle/suspend or callbacks. */
	pm_runtime_put_noidle(dev);
unlock:
	device_unlock(dev);
	put_device(dev);
out:
	last_error = ret;
	mutex_unlock(&diag_lock);
	return ret;
}

static int status_get(char *buffer, const struct kernel_param *parameter)
{
	int length;

	(void)parameter;
	mutex_lock(&diag_lock);
	/* Reading status reports cached results only; it never touches the bus. */
	length = scnprintf(buffer, PAGE_SIZE,
			   "ready=%u enabled=%u attempted=%u last_error=%d valid_mask=0x%x fw_version=0x%04x fw_date=0x%04x identity_proven=0 mechanical_ois_proven=0\n",
			   ready, enable_sample, attempted, last_error, result.valid_mask,
			   result.fw_version, result.fw_date);
	mutex_unlock(&diag_lock);
	return length;
}

static const struct kernel_param_ops sample_ops = {
	.set = sample_set,
};

static const struct kernel_param_ops status_ops = {
	.get = status_get,
};

module_param_cb(sample, &sample_ops, NULL, 0200);
MODULE_PARM_DESC(sample,
		 "One-shot fixed FW metadata request; requires exact risk-acknowledgement token");
module_param_cb(status, &status_ops, NULL, 0400);
MODULE_PARM_DESC(status, "Cached result only; no I2C access on read");

static int __init ois_diag_init(void)
{
	/* Parsing sample= during insertion cannot reach a transfer: ready is false. */
	mutex_lock(&diag_lock);
	ready = true;
	mutex_unlock(&diag_lock);
	pr_info("ois_diag: passive; no power or I2C operations performed at init\n");
	return 0;
}
module_init(ois_diag_init);

/* Deliberately no unload path. Keep until an independently approved reboot. */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LieppOS contributors");
MODULE_DESCRIPTION("Guarded, passive-by-default DW9781C FW metadata diagnostic");
