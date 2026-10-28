// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 * Copyright 2026 The LieppOS Contributors
 *
 * C51C: bounded, driver-owned timeout. No dependence on a userspace timer or
 * on the optional LED one-shot trigger. Regulator operations may sleep and
 * therefore never run in the hrtimer or regulator-notifier callbacks.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/timekeeping.h>
#include <linux/workqueue.h>

#include "regulator-vibrator-state.h"

#define VIB_DEVICE "regulator_vibrator"

struct reg_vibr_config {
	unsigned int min_volt;
	unsigned int max_volt;
	struct regulator *reg;
};

struct reg_vibr {
	/* Lock order: LED led_access (if needed), then lock. Never the reverse. */
	struct mutex lock;
	struct reg_vibr_state state;
	struct hrtimer timer;
	struct work_struct off_work;
	atomic_t fault_pending;
	bool powered; /* Exactly one successful regulator_enable reference. */
	bool stopping;
	bool led_registered;
	bool notifier_registered;
	int last_error;
	struct led_classdev vibr_cdev;
	struct reg_vibr_config vibr_conf;
	struct notifier_block oc_handle;
};

static int mt_vibra_init_config(struct device *dev,
			      struct reg_vibr_config *config)
{
	int ret;

	config->reg = devm_regulator_get(dev, "vib");
	if (IS_ERR(config->reg))
		return PTR_ERR(config->reg);
	ret = of_property_read_u32(dev->of_node, "max-volt", &config->max_volt);
	if (ret)
		return ret;
	ret = of_property_read_u32(dev->of_node, "min-volt", &config->min_volt);
	if (ret)
		return ret;
	if (config->min_volt > config->max_volt)
		return -EINVAL;
	return regulator_set_voltage(config->reg, config->min_volt,
				     config->max_volt);
}

/* Requires lock; never disable a reference we did not successfully acquire. */
static int vibr_power_off(struct reg_vibr *vibr)
{
	int ret;

	if (vibr->powered) {
		ret = regulator_disable(vibr->vibr_conf.reg);
		if (ret)
			return ret;
		vibr->powered = false;
	}
	ret = regulator_is_enabled(vibr->vibr_conf.reg);
	if (ret < 0)
		return ret;
	/* Another consumer/always-on rail cannot be forced off safely here. */
	return ret ? -EBUSY : 0;
}

static void vibr_retry(struct reg_vibr *vibr)
{
	if (!vibr->stopping)
		hrtimer_start(&vibr->timer, ms_to_ktime(REG_VIBR_RETRY_MS),
			      HRTIMER_MODE_REL);
}

static int vibr_stop_locked(struct reg_vibr *vibr, bool explicit_off)
{
	int ret;

	/* Callback only queues work: it never needs lock, so cancel is safe. */
	hrtimer_cancel(&vibr->timer);
	reg_vibr_cancel(&vibr->state);
	ret = vibr_power_off(vibr);
	reg_vibr_off_result(&vibr->state, !ret, explicit_off);
	if (ret) {
		vibr->last_error = ret;
		vibr_retry(vibr);
	} else if (explicit_off) {
		vibr->last_error = 0;
	}
	return ret;
}

static enum hrtimer_restart vibr_expire(struct hrtimer *timer)
{
	struct reg_vibr *vibr = container_of(timer, struct reg_vibr, timer);

	queue_work(system_wq, &vibr->off_work);
	return HRTIMER_NORESTART;
}

static void vibr_off_work(struct work_struct *work)
{
	struct reg_vibr *vibr = container_of(work, struct reg_vibr, off_work);

	mutex_lock(&vibr->lock);
	if (atomic_xchg(&vibr->fault_pending, 0)) {
		reg_vibr_fail(&vibr->state);
		vibr->last_error = -EIO;
	}
	if (vibr->stopping || vibr->state.blocked)
		goto out;
	/* Old queued expiry must not turn off a newly rearmed request. */
	if (vibr->state.active &&
	    !reg_vibr_due(&vibr->state, ktime_get_boottime_ns()))
		goto out;
	if (vibr->state.active || vibr->powered || vibr->state.fault)
		vibr_stop_locked(vibr, false);
out:
	mutex_unlock(&vibr->lock);
}

static int vibr_request(struct reg_vibr *vibr, unsigned int duration_ms)
{
	enum reg_vibr_start_result result;
	int ret = 0;

	mutex_lock(&vibr->lock);
	if (atomic_xchg(&vibr->fault_pending, 0)) {
		reg_vibr_fail(&vibr->state);
		vibr->last_error = -EIO;
		queue_work(system_wq, &vibr->off_work);
	}
	if (!duration_ms) {
		ret = vibr_stop_locked(vibr, true);
		goto out;
	}
	result = reg_vibr_start(&vibr->state, ktime_get_boottime_ns(),
				duration_ms);
	if (result != REG_VIBR_OK) {
		ret = result == REG_VIBR_INVALID ? -EINVAL :
		      result == REG_VIBR_BLOCKED ? -ESHUTDOWN :
		      vibr->last_error ? vibr->last_error : -EIO;
		goto out;
	}
	/* Quiesce the old callback before publishing/arming the new expiry. */
	hrtimer_cancel(&vibr->timer);
	hrtimer_start(&vibr->timer, ns_to_ktime(vibr->state.deadline_ns),
		      HRTIMER_MODE_ABS);
	if (!vibr->powered) {
		ret = regulator_enable(vibr->vibr_conf.reg);
		if (!ret)
			vibr->powered = true;
	}
	if (!ret && atomic_xchg(&vibr->fault_pending, 0))
		ret = -EIO;
	if (!ret && reg_vibr_due(&vibr->state, ktime_get_boottime_ns()))
		ret = -ETIME;
	if (ret) {
		int stop_ret;

		reg_vibr_fail(&vibr->state);
		vibr->last_error = ret;
		stop_ret = vibr_stop_locked(vibr, false);
		if (stop_ret)
			ret = stop_ret;
	}
out:
	mutex_unlock(&vibr->lock);
	return ret;
}

static int regulator_vibrator_set(struct led_classdev *cdev,
				  enum led_brightness brightness)
{
	struct reg_vibr *vibr = container_of(cdev, struct reg_vibr, vibr_cdev);

	/* Legacy boolean ABI is retained, but no longer enables indefinitely. */
	return vibr_request(vibr, brightness ? REG_VIBR_MAX_MS : 0);
}

static struct reg_vibr *vibr_from_dev(struct device *dev)
{
	return container_of(dev_get_drvdata(dev), struct reg_vibr, vibr_cdev);
}

static ssize_t timeout_abi_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "1\n");
}
static DEVICE_ATTR_RO(timeout_abi);

static ssize_t max_duration_ms_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", REG_VIBR_MAX_MS);
}
static DEVICE_ATTR_RO(max_duration_ms);

static ssize_t last_error_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct reg_vibr *vibr = vibr_from_dev(dev);
	int error;

	mutex_lock(&vibr->lock);
	error = vibr->last_error;
	mutex_unlock(&vibr->lock);
	return sysfs_emit(buf, "%d\n", error);
}
static DEVICE_ATTR_RO(last_error);

static ssize_t duration_ms_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct reg_vibr *vibr = vibr_from_dev(dev);
	unsigned long long now;
	unsigned long long remaining = 0;

	mutex_lock(&vibr->lock);
	now = ktime_get_boottime_ns();
	if (vibr->state.active && now < vibr->state.deadline_ns)
		remaining = DIV_ROUND_UP_ULL(vibr->state.deadline_ns - now,
					     NSEC_PER_MSEC);
	mutex_unlock(&vibr->lock);
	/* Requested remaining time, NOT physical motion/current feedback. */
	return sysfs_emit(buf, "%llu\n", remaining);
}

static ssize_t duration_ms_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct reg_vibr *vibr = vibr_from_dev(dev);
	unsigned int duration;
	int ret;

	ret = kstrtouint(buf, 10, &duration);
	if (ret)
		return ret;
	if (duration > REG_VIBR_MAX_MS)
		return -EINVAL;
	/* Detach any periodic trigger, with LED lock before request lock. */
	mutex_lock(&vibr->vibr_cdev.led_access);
	led_trigger_remove(&vibr->vibr_cdev);
	/*
	 * Trigger deactivation can queue a final LED-core OFF callback. Drain it
	 * before arming: otherwise it could cancel this newer timed request.
	 */
	flush_work(&vibr->vibr_cdev.set_brightness_work);
	ret = vibr_request(vibr, duration);
	mutex_unlock(&vibr->vibr_cdev.led_access);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(duration_ms);

static struct attribute *vibr_attrs[] = {
	&dev_attr_timeout_abi.attr,
	&dev_attr_max_duration_ms.attr,
	&dev_attr_duration_ms.attr,
	&dev_attr_last_error.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vibr);

static int regulator_oc_event(struct notifier_block *nb,
			      unsigned long event, void *data)
{
	struct reg_vibr *vibr = container_of(nb, struct reg_vibr, oc_handle);

	/* Never acquire request lock or call back into regulator from notifier. */
	if ((event & (REGULATOR_EVENT_OVER_CURRENT | REGULATOR_EVENT_FAIL)) &&
	    !READ_ONCE(vibr->stopping)) {
		atomic_set(&vibr->fault_pending, 1);
		queue_work(system_wq, &vibr->off_work);
	}
	return NOTIFY_OK;
}

/* Terminal teardown: stop all producers before cancelling work/freeing state. */
static int vibr_release(struct reg_vibr *vibr)
{
	int ret, attempt;

	mutex_lock(&vibr->lock);
	WRITE_ONCE(vibr->stopping, true);
	reg_vibr_quiesce(&vibr->state);
	hrtimer_cancel(&vibr->timer);
	mutex_unlock(&vibr->lock);
	if (vibr->notifier_registered) {
		regulator_unregister_notifier(vibr->vibr_conf.reg, &vibr->oc_handle);
		vibr->notifier_registered = false;
	}
	if (vibr->led_registered) {
		led_classdev_unregister(&vibr->vibr_cdev);
		vibr->led_registered = false;
	}
	hrtimer_cancel(&vibr->timer);
	cancel_work_sync(&vibr->off_work);
	/*
	 * All producers are gone. Retry transient OFF failures, but do not hang
	 * removal indefinitely or pretend persistent hardware failure is OFF.
	 */
	for (attempt = 0; attempt < 3; attempt++) {
		mutex_lock(&vibr->lock);
		ret = vibr_stop_locked(vibr, false);
		mutex_unlock(&vibr->lock);
		if (!ret)
			break;
		if (attempt < 2)
			msleep(REG_VIBR_RETRY_MS);
	}
	return ret;
}

static int vib_probe(struct platform_device *pdev)
{
	struct reg_vibr *vibr;
	int enabled, ret;

	vibr = devm_kzalloc(&pdev->dev, sizeof(*vibr), GFP_KERNEL);
	if (!vibr)
		return -ENOMEM;
	mutex_init(&vibr->lock);
	INIT_WORK(&vibr->off_work, vibr_off_work);
	atomic_set(&vibr->fault_pending, 0);
	hrtimer_init(&vibr->timer, CLOCK_BOOTTIME, HRTIMER_MODE_ABS);
	vibr->timer.function = vibr_expire;
	ret = mt_vibra_init_config(&pdev->dev, &vibr->vibr_conf);
	if (ret)
		return ret;
	/* Do not pulse an OFF rail just to initialize consumer reference counts. */
	enabled = regulator_is_enabled(vibr->vibr_conf.reg);
	if (enabled < 0)
		return enabled;
	if (enabled) {
		ret = regulator_enable(vibr->vibr_conf.reg);
		if (ret)
			return ret;
		vibr->powered = true;
	}
	ret = vibr_power_off(vibr);
	if (ret)
		goto fail;
	ret = of_property_read_string(pdev->dev.of_node, "label",
				     &vibr->vibr_cdev.name);
	if (ret)
		goto fail;
	platform_set_drvdata(pdev, vibr);
	vibr->oc_handle.notifier_call = regulator_oc_event;
	ret = regulator_register_notifier(vibr->vibr_conf.reg, &vibr->oc_handle);
	if (ret)
		goto fail;
	vibr->notifier_registered = true;
	vibr->vibr_cdev.brightness_set_blocking = regulator_vibrator_set;
	vibr->vibr_cdev.groups = vibr_groups;
	ret = led_classdev_register(&pdev->dev, &vibr->vibr_cdev);
	if (ret)
		goto fail;
	vibr->led_registered = true;
	return 0;
fail:
	if (vibr_release(vibr))
		dev_crit(&pdev->dev, "probe cleanup cannot verify regulator OFF\n");
	return ret;
}

static int __maybe_unused vib_suspend(struct device *dev)
{
	struct reg_vibr *vibr = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&vibr->vibr_cdev.led_access);
	led_trigger_remove(&vibr->vibr_cdev);
	/* No delayed LED-core callback may touch a suspended regulator bus. */
	flush_work(&vibr->vibr_cdev.set_brightness_work);
	mutex_lock(&vibr->lock);
	reg_vibr_quiesce(&vibr->state);
	ret = vibr_stop_locked(vibr, false);
	if (ret)
		vibr->state.blocked = false; /* Failed suspend: keep retrying OFF. */
	mutex_unlock(&vibr->lock);
	mutex_unlock(&vibr->vibr_cdev.led_access);
	return ret;
}

static int __maybe_unused vib_resume(struct device *dev)
{
	struct reg_vibr *vibr = dev_get_drvdata(dev);

	mutex_lock(&vibr->lock);
	vibr->state.blocked = false; /* Never resurrect a pre-suspend request. */
	if (vibr->state.fault || atomic_read(&vibr->fault_pending))
		queue_work(system_wq, &vibr->off_work);
	mutex_unlock(&vibr->lock);
	return 0;
}

static int vib_remove(struct platform_device *pdev)
{
	int ret = vibr_release(platform_get_drvdata(pdev));

	if (ret)
		dev_crit(&pdev->dev, "remove cannot verify regulator OFF: %d\n", ret);
	return ret;
}

static void vib_shutdown(struct platform_device *pdev)
{
	int ret = vibr_release(platform_get_drvdata(pdev));

	if (ret)
		dev_crit(&pdev->dev, "shutdown cannot verify regulator OFF: %d\n", ret);
}

static SIMPLE_DEV_PM_OPS(vib_pm_ops, vib_suspend, vib_resume);
static const struct of_device_id vibr_of_ids[] = {
	{ .compatible = "regulator-vibrator" },
	{ }
};
MODULE_DEVICE_TABLE(of, vibr_of_ids);

static struct platform_driver vibrator_driver = {
	.probe = vib_probe,
	.remove = vib_remove,
	.shutdown = vib_shutdown,
	.driver = {
		.name = VIB_DEVICE,
		.pm = &vib_pm_ops,
		.of_match_table = vibr_of_ids,
	},
};
module_platform_driver(vibrator_driver);
MODULE_AUTHOR("Mediatek Corporation");
MODULE_DESCRIPTION("Regulator Vibrator Driver with bounded kernel timeout");
MODULE_LICENSE("GPL");
