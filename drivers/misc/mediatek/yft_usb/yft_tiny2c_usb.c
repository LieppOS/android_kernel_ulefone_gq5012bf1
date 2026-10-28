// SPDX-License-Identifier: GPL-2.0
/* Stock-oracle reconstruction for GQ5012BF1 yft_tiny2c_usb. */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct tiny2c_usb_chip {
	int gpio_1v8;
	int gpio_3v3;
	int gpio_5v;
	int gpio_io3v3;
	enum of_gpio_flags gpio_flags;
};

static int chip_id;
static struct tiny2c_usb_chip *tiny2c_usb_chip_data;
extern int yft_usb_flag;

static void tiny2c_usb_gpio_release(struct tiny2c_usb_chip *data)
{
	if (!data)
		return;

	if (gpio_is_valid(data->gpio_io3v3))
		gpio_free(data->gpio_io3v3);
	if (gpio_is_valid(data->gpio_5v))
		gpio_free(data->gpio_5v);
	if (gpio_is_valid(data->gpio_3v3))
		gpio_free(data->gpio_3v3);
	if (gpio_is_valid(data->gpio_1v8))
		gpio_free(data->gpio_1v8);

	data->gpio_io3v3 = 0;
	data->gpio_5v = 0;
	data->gpio_3v3 = 0;
	data->gpio_1v8 = 0;
}

static void tiny2c_usb_power_init(struct tiny2c_usb_chip *data, int en)
{
	if (!data)
		return;

	pr_info("%s: enter. en=%d\n", __func__, en);

	if (gpio_is_valid(data->gpio_io3v3))
		gpio_set_value(data->gpio_io3v3, en);
	if (gpio_is_valid(data->gpio_5v))
		gpio_set_value(data->gpio_5v, en);
	if (gpio_is_valid(data->gpio_3v3))
		gpio_set_value(data->gpio_3v3, en);
	if (gpio_is_valid(data->gpio_1v8))
		gpio_set_value(data->gpio_1v8, en);
}

static ssize_t tiny2c_usb_mode_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct tiny2c_usb_chip *data = tiny2c_usb_chip_data;
	int gpio_1v8, gpio_3v3, gpio_5v, gpio_io3v3;

	if (!dev || !data) {
		pr_info("tiny2c_usb_mode_show: no device data\n");
		return -ENODEV;
	}

	gpio_1v8 = gpio_is_valid(data->gpio_1v8) ?
		gpio_get_value(data->gpio_1v8) : -1;
	gpio_3v3 = gpio_is_valid(data->gpio_3v3) ?
		gpio_get_value(data->gpio_3v3) : -1;
	gpio_5v = gpio_is_valid(data->gpio_5v) ?
		gpio_get_value(data->gpio_5v) : -1;
	gpio_io3v3 = gpio_is_valid(data->gpio_io3v3) ?
		gpio_get_value(data->gpio_io3v3) : -1;

	return scnprintf(buf, PAGE_SIZE,
		"gpio_1v8=%d,gpio_3v3=%d,gpio_5v=%d,gpio_io3v3:%d\n",
		gpio_1v8, gpio_3v3, gpio_5v, gpio_io3v3);
}

static ssize_t tiny2c_usb_mode_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct tiny2c_usb_chip *data = tiny2c_usb_chip_data;
	int mode = 0;

	if (!data)
		return -ENODEV;

	if (sscanf(buf, "%d", &mode) != 1)
		return -EINVAL;

	if (mode == 1) {
		pr_info("%s: power on: %d\n", __func__, mode);
		tiny2c_usb_power_init(data, 1);
		yft_usb_flag = 1;
	} else if (mode == 0) {
		pr_info("%s: power off: %d\n", __func__, mode);
		tiny2c_usb_power_init(data, 0);
		yft_usb_flag = 0;
	} else {
		pr_info("%s: fail: %d\n", __func__, mode);
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR(tiny2c_usb_mode, 0644,
		   tiny2c_usb_mode_show, tiny2c_usb_mode_store);

static ssize_t sensor_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	if (!dev) {
		pr_info("tiny2c_usb_mode_show. dev is null!!\n");
		return 0;
	}
	return scnprintf(buf, PAGE_SIZE, "0x%x\n", chip_id);
}
static DEVICE_ATTR(sensor_id, 0444, sensor_id_show, NULL);

static struct device_attribute *mt_sysfs_attributes[] = {
	&dev_attr_tiny2c_usb_mode,
	&dev_attr_sensor_id,
	NULL,
};

int tiny2c_usb_i2c_read(struct i2c_client *client, u16 addr, u8 *data)
{
	u8 buf[2];
	struct i2c_msg msg[2] = { };
	int ret;

	if (!client)
		return -ENODEV;
	buf[0] = addr >> 8;
	buf[1] = addr & 0xff;
	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 2;
	msg[0].buf = buf;
	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = buf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_info(&client->dev, "i2c transfer failed (%d)\n", ret);
		return ret;
	}
	*data = buf[0];
	return 0;
}

int tiny2c_usb_read_chipid(struct i2c_client *client)
{
	int retry = 5;
	int ret;
	u8 buf[2] = { 0, 0 };

	pr_info("[%s] zzt_1 addr=0x%x\n", __func__, client->addr);
	while (retry--) {
		ret = tiny2c_usb_i2c_read(client, 0x0000, &buf[0]);
		ret = tiny2c_usb_i2c_read(client, 0x0001, &buf[1]);
		if (ret < 0) {
			pr_info("%s:yft-drv Read_Data fail=%x,,buf:[%x,%x]\n",
				__func__, ret, buf[0], buf[1]);
			continue;
		}
		pr_info("%s:yft-drv Read_Data_ok=%x,,buf:[%x,%x]\n",
			__func__, ret, buf[0], buf[1]);
		chip_id = (buf[1] << 8) | buf[0];
		break;
	}
	return chip_id;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-extra-args"
static int tiny2c_usb_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int i;

	pr_info("[%s]addr=0x%x start...\n", __func__, client->addr);
	msleep(400);
	for (i = 0; i < 3; i++) {
		chip_id = tiny2c_usb_read_chipid(client);
		pr_info("[%s] zzt_1 addr=0x%x\n", __func__, i, chip_id);
		if (chip_id == 0x4c59)
			return 0;
		mdelay(10);
	}
	pr_err("read sensor-id fail\n");
	return -ENODEV;
}
#pragma clang diagnostic pop

static void tiny2c_usb_i2c_remove(struct i2c_client *client)
{
	/* The i2c core owns this client; unregistering it from its own remove
	 * callback drops the device while the caller still holds a reference.
	 */
	pr_info("%s\n", __func__);
}

static const struct i2c_device_id tiny2c_usb_i2c_id[] = {
	{ "tiny2c_usb-sensor", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tiny2c_usb_i2c_id);

static const struct of_device_id tiny2c_usb_sensor_of_match[] = {
	{ .compatible = "mediatek,tiny2c_usb" },
	{ }
};

struct i2c_driver tiny2c_usb_i2c_driver = {
	.driver = {
		.name = "tiny2c_usb-sensor",
		.owner = THIS_MODULE,
		.of_match_table = tiny2c_usb_sensor_of_match,
	},
	.probe = tiny2c_usb_i2c_probe,
	.remove = tiny2c_usb_i2c_remove,
	.id_table = tiny2c_usb_i2c_id,
};

static int tiny2c_usb_get_gpio(struct device_node *np, const char *name,
			       const char *label, bool required, int *out)
{
	int gpio;

	gpio = of_get_named_gpio_flags(np, name, 0, NULL);
	if (gpio == -ENOENT && !required) {
		*out = 0;
		return 0;
	}
	if (gpio < 0) {
		pr_err("%s: unable to get %s (%d)\n", __func__, name, gpio);
		return gpio;
	}
	if (!gpio_is_valid(gpio)) {
		pr_err("%s: invalid %s (%d)\n", __func__, name, gpio);
		return -EINVAL;
	}
	if (gpio_request_one(gpio, GPIOF_OUT_INIT_LOW, label)) {
		pr_err("%s: unable to request %s (%d)\n", __func__, name, gpio);
		return -EBUSY;
	}

	*out = gpio;
	return 0;
}

static int tiny2c_usb_parse_dts(struct device_node *np)
{
	struct tiny2c_usb_chip *data = tiny2c_usb_chip_data;
	int ret;

	pr_info("%s: enter. \n", __func__);

	ret = tiny2c_usb_get_gpio(np, "tiny2c_usb_vdd_1v8",
				  "tiny2c_gpio_1v8", true, &data->gpio_1v8);
	if (ret)
		goto err;
	ret = tiny2c_usb_get_gpio(np, "tiny2c_usb_vdd_3v3",
				  "tiny2c_gpio_3v3", true, &data->gpio_3v3);
	if (ret)
		goto err;
	ret = tiny2c_usb_get_gpio(np, "tiny2c_usb_vdd_5v",
				  "tiny2c_gpio_5v", false, &data->gpio_5v);
	if (ret)
		goto err;
	ret = tiny2c_usb_get_gpio(np, "tiny2c_usb_vddio_3v3",
				  "tiny2c_gpio_io3v3", true, &data->gpio_io3v3);
	if (ret)
		goto err;

	pr_info("%s: end. gpio_1v8:%d, gpio_3v3:%d,gpio_5v:%d,gpio_io3v3:%d\n",
		__func__, data->gpio_1v8, data->gpio_3v3,
		data->gpio_5v, data->gpio_io3v3);
	return 0;

err:
	tiny2c_usb_gpio_release(data);
	return ret;
}

static int tiny2c_usb_init_sysfs(struct device *dev)
{
	int i, ret;

	pr_info("tiny2c_usb_init_sysfs. begin. \n");
	for (i = 0; mt_sysfs_attributes[i]; i++) {
		ret = device_create_file(dev, mt_sysfs_attributes[i]);
		if (ret)
			goto err;
	}
	return 0;
err:
	while (--i >= 0)
		device_remove_file(dev, mt_sysfs_attributes[i]);
	return ret;
}

static void tiny2c_usb_remove_sysfs(struct device *dev)
{
	int i;

	for (i = 0; mt_sysfs_attributes[i]; i++)
		device_remove_file(dev, mt_sysfs_attributes[i]);
}

static int tiny2c_usb_probe(struct platform_device *pdev)
{
	struct tiny2c_usb_chip *data;
	int ret;

	pr_info("tiny2c_usb_probe start\n");
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		pr_info("no memory for tiny2c_usb_chip_data\n");
		return -ENOMEM;
	}
	tiny2c_usb_chip_data = data;

	ret = tiny2c_usb_parse_dts(pdev->dev.of_node);
	if (ret) {
		pr_info("tiny2c_usb_parse_dts failed (%d)\n", ret);
		goto err_free;
	}

	tiny2c_usb_power_init(data, 1);
	pr_info("gpio_1v8=%d,gpio_3v3=%d,gpio_5v=%d,gpio_io3v3:%d\n",
		gpio_is_valid(data->gpio_1v8) ? gpio_get_value(data->gpio_1v8) : -1,
		gpio_is_valid(data->gpio_3v3) ? gpio_get_value(data->gpio_3v3) : -1,
		gpio_is_valid(data->gpio_5v) ? gpio_get_value(data->gpio_5v) : -1,
		gpio_is_valid(data->gpio_io3v3) ? gpio_get_value(data->gpio_io3v3) : -1);

	ret = i2c_add_driver(&tiny2c_usb_i2c_driver);
	if (ret) {
		pr_err("register tiny2c_usb driver failed (%d)\n", ret);
		goto err_power;
	}

	tiny2c_usb_power_init(data, 0);

	ret = tiny2c_usb_init_sysfs(&pdev->dev);
	if (ret) {
		pr_info("failed to init_sysfs (%d)\n", ret);
		goto err_i2c;
	}

	pr_info("tiny2c_usb_probe end\n");
	return 0;

err_i2c:
	i2c_del_driver(&tiny2c_usb_i2c_driver);
err_power:
	tiny2c_usb_power_init(data, 0);
err_free:
	tiny2c_usb_gpio_release(data);
	kfree(data);
	tiny2c_usb_chip_data = NULL;
	return ret;
}

static int tiny2c_usb_remove(struct platform_device *pdev)
{
	struct tiny2c_usb_chip *data = tiny2c_usb_chip_data;

	pr_info("%s\n", __func__);

	tiny2c_usb_remove_sysfs(&pdev->dev);
	if (data) {
		tiny2c_usb_power_init(data, 0);
		yft_usb_flag = 0;
	}

	i2c_del_driver(&tiny2c_usb_i2c_driver);
	tiny2c_usb_gpio_release(data);
	kfree(data);
	tiny2c_usb_chip_data = NULL;
	chip_id = 0;
	return 0;
}

static struct of_device_id tiny2c_usb_of_match[] = {
	{ .compatible = "mediatek,yft_tiny2c_usb" },
	{ }
};
MODULE_DEVICE_TABLE(of, tiny2c_usb_of_match);

static struct platform_driver tiny2c_usb_driver = {
	.probe = tiny2c_usb_probe,
	.remove = tiny2c_usb_remove,
	.driver = {
		.name = "yft_tiny2c_usb",
		.owner = THIS_MODULE,
		.of_match_table = tiny2c_usb_of_match,
	},
};
module_platform_driver(tiny2c_usb_driver);

MODULE_DESCRIPTION("Module For tiny2c_usb");
MODULE_AUTHOR("yft-drv");
MODULE_LICENSE("GPL");
