// SPDX-License-Identifier: GPL-2.0
/*
 * Stock-oracle reconstruction of GQ5012BF1 custom_ldo_wl2868.ko.
 *
 * This intentionally preserves the vendor driver's raw-I2C ABI and quirks.
 * It is not a Linux regulator-framework driver.
 */
#include <linux/build_bug.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define WL2864C_REG_ENABLE       0x0e
#define WL2864C_VOUT_FIRST       0x03
#define WL2864C_MAX_LDO          7
#define WL2864C_FORCED_I2C_ADDR  0x2f
#define WL2864C_ID               0x01
#define WL2868C_ID               0x82

/* Exact stock .bss object size and all stock-accessed offsets. */
struct wl2864c_state {
	struct i2c_client *client;     /* 0x00 */
	u8 unknown_08_2f[0x28];        /* 0x08 */
	struct gpio_desc *reset;       /* 0x30 */
	struct gpio_desc *vin1;        /* 0x38 */
	int vin2_gpio;                 /* 0x40 */
	u8 chip_id;                    /* 0x44 */
	u8 unknown_45_67[0x23];        /* 0x45 */
	int read_pos;                  /* 0x68 */
	u8 probe_ready;                /* 0x6c */
	u8 unknown_6d_6f[3];           /* 0x6d */
};

static_assert(sizeof(struct wl2864c_state) == 0x70);
static_assert(offsetof(struct wl2864c_state, client) == 0x00);
static_assert(offsetof(struct wl2864c_state, reset) == 0x30);
static_assert(offsetof(struct wl2864c_state, vin1) == 0x38);
static_assert(offsetof(struct wl2864c_state, vin2_gpio) == 0x40);
static_assert(offsetof(struct wl2864c_state, chip_id) == 0x44);
static_assert(offsetof(struct wl2864c_state, read_pos) == 0x68);
static_assert(offsetof(struct wl2864c_state, probe_ready) == 0x6c);

static struct wl2864c_state wl2864c_data;

loff_t wl2864c_llseek(struct file *file, loff_t offset, int whence);
static int wl2864c_open(struct inode *inode, struct file *file);
static ssize_t wl2864c_read(struct file *file, char __user *user,
			    size_t count, loff_t *pos);
static ssize_t wl2864c_write(struct file *file, const char __user *user,
			     size_t count, loff_t *pos);

static const struct of_device_id wl2864c_dt_match[] = {
	{ .compatible = "will,wl2864c_pmu" },
	{ }
};

static const struct i2c_device_id wl2864c_id[] = {
	{ "wl2864c", 0 },
	{ }
};

static const struct file_operations wl2864c_fops = {
	.owner = THIS_MODULE,
	.llseek = wl2864c_llseek,
	.read = wl2864c_read,
	.write = wl2864c_write,
	.open = wl2864c_open,
};

static __always_inline int wl2864c_read_reg(u8 reg, u8 *value)
{
	struct i2c_msg msg[2];
	int ret;

	msg[0].addr = wl2864c_data.client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg;
	msg[1].addr = wl2864c_data.client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = value;
	ret = i2c_transfer(wl2864c_data.client->adapter, msg, 2);
	if (ret < 0)
		dev_info(&wl2864c_data.client->dev,
			 "i2c transfer failed (%d)\n", ret);
	return ret;
}

static __always_inline int wl2864c_write_reg(u8 reg, u8 value)
{
	struct i2c_msg msg;
	u8 data[2] = { reg, value };
	int ret;

	msg.addr = wl2864c_data.client->addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = data;
	ret = i2c_transfer(wl2864c_data.client->adapter, &msg, 1);
	if (ret < 0)
		dev_info(&wl2864c_data.client->dev,
			 "i2c transfer failed (%d)\n", ret);
	return ret;
}

int wl2864c_vin2_power(int power)
{
	pr_err("%s: power=%d\n", __func__, power);
	power = !!power;
	gpiod_set_raw_value(gpio_to_desc(wl2864c_data.vin2_gpio), power);
	return 0;
}

int wl2864c_ldo_vout(int ldo_num, int value)
{
	const int floor[WL2864C_MAX_LDO] = {
		60000, 60000, 120000, 120000, 120000, 120000, 120000,
	};
	const int offset[WL2864C_MAX_LDO] = {
		-6000, -6000, -12000, -12000, -12000, -12000, -12000,
	};
	u8 read_val = 0;
	int ldo_vout;
	int write_val;
	int ret;

	if (ldo_num < 1 || ldo_num > WL2864C_MAX_LDO) {
		pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
		return -1;
	}
	ldo_vout = value / 100;
	pr_err("%s: ldo_vout=%d\n", __func__, ldo_vout);
	if (!ldo_vout)
		return 0;
	write_val = value < floor[ldo_num - 1] ? 0 :
		(ldo_vout + offset[ldo_num - 1]) / 125;
	ret = wl2864c_write_reg(WL2864C_VOUT_FIRST + ldo_num - 1,
				(u8)write_val);
	(void)wl2864c_read_reg(WL2864C_VOUT_FIRST + ldo_num - 1, &read_val);
	pr_err("%s: ldo_num=%d, ldo_vout(write_val:%x, read_val:%u), ret=%d\n",
	       __func__, ldo_num, (u8)write_val, read_val, ret < 0 ? ret : 0);
	if (ret < 0)
		pr_err("%s: ldo_num=%d ldo_vout write fail ret=%d\n",
		       __func__, ldo_num, ret);
	return ret < 0 ? ret : 0;
}

int wl2868c_ldo_vout(int ldo_num, int value)
{
	const int floor[WL2864C_MAX_LDO] = {
		49600, 49600, 150400, 150400, 150400, 150400, 150400,
	};
	const int offset[WL2864C_MAX_LDO] = {
		-4960, -4960, -15040, -15040, -15040, -15040, -15040,
	};
	u8 read_val = 0;
	int ldo_vout;
	int write_val;
	int ret;

	if (ldo_num < 1 || ldo_num > WL2864C_MAX_LDO) {
		pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
		return -1;
	}
	ldo_vout = value / 100;
	pr_err("%s: ldo_vout=%d\n", __func__, ldo_vout);
	if (!ldo_vout)
		return 0;
	write_val = value < floor[ldo_num - 1] ? 0 :
		(ldo_vout + offset[ldo_num - 1]) / 80;
	ret = wl2864c_write_reg(WL2864C_VOUT_FIRST + ldo_num - 1,
				(u8)write_val);
	(void)wl2864c_read_reg(WL2864C_VOUT_FIRST + ldo_num - 1, &read_val);
	pr_err("%s: ldo_num=%d, ldo_vout(write_val:%x, read_val:%u), ret=%d\n",
	       __func__, ldo_num, (u8)write_val, read_val, ret < 0 ? ret : 0);
	if (ret < 0)
		pr_err("%s: ldo_num=%d ldo_vout write fail ret=%d\n",
		       __func__, ldo_num, ret);
	return ret < 0 ? ret : 0;
}

int wl2864c_ldo_en(int ldo_num, int enable)
{
	u8 value = 0;
	int ret;

	if (ldo_num < 1 || ldo_num > WL2864C_MAX_LDO) {
		pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
		return 0;
	}
	ret = wl2864c_read_reg(WL2864C_REG_ENABLE, &value);
	pr_err("%s: ldo_num=%d, ldo_en read val=%x, ret=%d\n",
	       __func__, ldo_num, value, ret < 0 ? ret : 0);
	if (ret < 0) {
		pr_err("%s: ldo_en read fail ret=%d\n", __func__, ret);
		return -ENODEV;
	}
	if (enable)
		value |= BIT(ldo_num - 1);
	else
		value &= ~BIT(ldo_num - 1);
	ret = wl2864c_write_reg(WL2864C_REG_ENABLE, value);
	pr_err("%s: ldo_num=%d, ldo_en write val=%x, ret=%d\n",
	       __func__, ldo_num, value, ret < 0 ? ret : 0);
	if (ret < 0) {
		pr_err("%s: ldo_en write fail ret=%d\n", __func__, ret);
		return ret;
	}
	return enable;
}

int wl2868c_ldo_en(int ldo_num, int enable)
{
	u8 value = 0;
	u8 write_value;
	int ret;

	if (ldo_num < 1 || ldo_num > WL2864C_MAX_LDO) {
		pr_err("%s: ldo_num=%d err\n", __func__, ldo_num);
		return 0;
	}
	ret = wl2864c_read_reg(WL2864C_REG_ENABLE, &value);
	pr_err("%s: ldo_num=%d, ldo_en read val=%x, ret=%d\n",
	       __func__, ldo_num, value, ret < 0 ? ret : 0);
	if (ret < 0) {
		pr_err("%s: ldo_en read fail ret=%d\n", __func__, ret);
		return -ENODEV;
	}
	if (enable)
		value |= BIT(ldo_num - 1);
	else
		value &= ~BIT(ldo_num - 1);
	write_value = value ? value | BIT(7) : 0;
	ret = wl2864c_write_reg(WL2864C_REG_ENABLE, write_value);
	if (ret >= 0)
		ret = 0;
	pr_err("%s: ldo_num=%d, ldo_en write val=%x %x, ret=%d\n",
	       __func__, ldo_num, value, write_value, ret);
	if (ret < 0)
		pr_err("%s: ldo_en write fail ret=%d\n", __func__, ret);
	return ret;
}

/* Critical inter-module ABI: keep the exact names and int,int prototypes. */
int will_ldo_vout(int ldo_num, int value)
{
	pr_err("%s: wl2864c_data.chip_id%x\n", __func__,
	       wl2864c_data.chip_id);
	if (wl2864c_data.chip_id == WL2868C_ID) {
		wl2868c_ldo_vout(ldo_num, value);
		return 0;
	}
	if (wl2864c_data.chip_id == WL2864C_ID) {
		wl2864c_ldo_vout(ldo_num, value);
		return 0;
	}
	return -1;
}
EXPORT_SYMBOL(will_ldo_vout);

int will_ldo_en(int ldo_num, int enable)
{
	if (wl2864c_data.chip_id == WL2868C_ID) {
		wl2868c_ldo_en(ldo_num, enable);
		return 0;
	}
	if (wl2864c_data.chip_id == WL2864C_ID) {
		wl2864c_ldo_en(ldo_num, enable);
		return 0;
	}
	return -1;
}
EXPORT_SYMBOL(will_ldo_en);

loff_t wl2864c_llseek(struct file *file, loff_t offset, int whence)
{
	if (whence == SEEK_CUR)
		wl2864c_data.read_pos += (int)offset;
	else
		wl2864c_data.read_pos = 0;
	pr_err("%s: wl2864c: update read pos to %02X\n", __func__,
	       wl2864c_data.read_pos);
	return file->f_pos;
}

static int wl2864c_open(struct inode *inode, struct file *file)
{
	if (!wl2864c_data.probe_ready) {
		pr_err("%s: wl2864c: open failed.\n", __func__);
		return -ENODEV;
	}
	wl2864c_data.read_pos = 0;
	return 0;
}

static __always_inline char wl2864c_hex_upper(u8 nibble)
{
	nibble &= 0xf;
	return nibble > 9 ? nibble - 10 + 'A' : nibble + '0';
}

static ssize_t wl2864c_read(struct file *file, char __user *user,
			    size_t count, loff_t *pos)
{
	char *out;
	size_t i;
	size_t out_len = 0;
	int reg = wl2864c_data.read_pos;
	ssize_t result;
	int ret;
	u8 value;

	out = kmalloc(128, GFP_KERNEL);
	if (!out) {
		pr_err("%s: wl2864c: malloc failed %d\n", __func__, 0);
		return -ENOMEM;
	}
	if (count > 20) {
		result = -EINVAL;
		goto out;
	}
	for (i = 0; i < count; i++, reg++) {
		ret = wl2864c_read_reg((u8)reg, &value);
		if (ret < 0) {
			pr_err("%s: wl2864c: read %X failed %d\n",
			       __func__, (u8)reg, ret);
			kfree(out);
			return ret;
		}
		out[out_len++] = wl2864c_hex_upper((u8)reg >> 4);
		out[out_len++] = wl2864c_hex_upper((u8)reg);
		out[out_len++] = ' ';
		out[out_len++] = wl2864c_hex_upper(value >> 4);
		out[out_len++] = wl2864c_hex_upper(value);
		out[out_len++] = ' ';
		pr_err("%s: wl2864c: read REG[%02X %02X]\n",
		       __func__, (u8)reg, value);
	}
	(void)copy_to_user(user, out, out_len);
	result = count;
out:
	kfree(out);
	return result;
}

static __always_inline u8 wl2864c_nibble(u8 c)
{
	if (c >= 'a')
		return c - 'a' + 10;
	if (c >= 'A')
		return c - 'A' + 10;
	if (c >= '0')
		return c - '0';
	return 0;
}

static ssize_t wl2864c_write(struct file *file, const char __user *user,
			     size_t count, loff_t *pos)
{
	char *in;
	size_t i;
	int ret;
	u8 reg;
	u8 value;

	if (count > 128)
		return -EINVAL;
	in = memdup_user(user, count);
	if (IS_ERR(in)) {
		pr_err("%s: wl2864c: can't get user data\n", __func__);
		return PTR_ERR(in);
	}
	for (i = 0; i < count; i += 6) {
		reg = (wl2864c_nibble(in[i]) << 4) |
			wl2864c_nibble(in[i + 1]);
		value = (wl2864c_nibble(in[i + 3]) << 4) |
			wl2864c_nibble(in[i + 4]);
		ret = wl2864c_write_reg(reg, value);
		if (ret < 0) {
			pr_err("%s: wl2864c: write failed %d\n", __func__, ret);
			kfree(in);
			return -ENODEV;
		}
		pr_err("%s: wl2864c: write REG[%02X %02X]\n",
		       __func__, reg, value);
	}
	kfree(in);
	return count;
}

static struct miscdevice wl2864c_miscdev = {
	.minor = 250,
	.name = "wl2864c",
	.fops = &wl2864c_fops,
};

static int wl2864c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	int ret;

	wl2864c_data = (struct wl2864c_state) {
		.client = client,
	};
	pr_err("%s: entry\n", __func__);
	pr_err("%s: entry\n", "wl2864c_power_on");

	wl2864c_data.vin1 = devm_gpiod_get(dev, "vin1", GPIOD_OUT_HIGH);
	if (IS_ERR(wl2864c_data.vin1)) {
		dev_err(dev, "failed to request vin1_en GPIO: %d\n",
			(int)PTR_ERR(wl2864c_data.vin1));
	} else {
		dev_info(dev, "wl2868c: vin1_en GPIO set high\n");
		gpiod_set_value(wl2864c_data.vin1, 1);
		devm_gpiod_put(dev, wl2864c_data.vin1);
	}

	wl2864c_data.reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(wl2864c_data.reset)) {
		ret = PTR_ERR(wl2864c_data.reset);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		if (ret) {
			pr_err("%s: wl2864c_power_on failed %d\n", __func__, ret);
			return ret;
		}
	} else {
		gpiod_set_value(wl2864c_data.reset, 1);
		usleep_range(10000, 11000);
		gpiod_set_value(wl2864c_data.reset, 0);
		usleep_range(10000, 11000);
		gpiod_set_value(wl2864c_data.reset, 1);
		usleep_range(10000, 11000);
		devm_gpiod_put(dev, wl2864c_data.reset);
	}

	msleep(1);
	client->addr = WL2864C_FORCED_I2C_ADDR;
	wl2864c_data.chip_id = WL2868C_ID;
	pr_err("%s: ldo_vout=%d\n", "wl2864c_ldo_vout", 0);
	pr_err("%s: ldo_vout=%d\n", "wl2864c_ldo_vout", 0);
	pr_err("%s: ldo_vout=%d\n", "wl2864c_ldo_vout", 0);
	pr_err("%s: ldo_vout=%d\n", "wl2864c_ldo_vout", 0);
	pr_err("%s: ldo_vout=%d\n", "wl2864c_ldo_vout", 0);
	pr_err("%s: ldo_vout=%d\n", "wl2864c_ldo_vout", 0);
	pr_err("%s: ldo_vout=%d\n", "wl2864c_ldo_vout", 0);

	ret = misc_register(&wl2864c_miscdev);
	if (ret < 0) {
		pr_err("%s: failed to register wl2864c device\n", __func__);
		return ret;
	}
	pr_err("%s: wl2864c_probe successed! chip id = %d\n",
	       __func__, wl2864c_data.chip_id);
	wl2864c_data.probe_ready = 1;
	return 0;
}

static void wl2864c_remove(struct i2c_client *client)
{
	misc_deregister(&wl2864c_miscdev);
	wl2864c_data.probe_ready = 0;
	pr_err("%s: deregister wl2864c device ok\n", __func__);
}

static struct i2c_driver wl2864c_i2c_driver = {
	.probe = wl2864c_probe,
	.remove = wl2864c_remove,
	.driver = {
		.name = "wl2864c",
		.owner = THIS_MODULE,
		.of_match_table = wl2864c_dt_match,
	},
	.id_table = wl2864c_id,
};

static int __init wl2864c_init(void)
{
	int ret;

	pr_err("%s: entry\n", __func__);
	ret = (u8)i2c_add_driver(&wl2864c_i2c_driver);
	if (ret)
		pr_err("%s: add driver failed, error=%d\n", __func__, ret);
	else
		pr_err("%s: add driver success\n", __func__);
	return ret;
}

static void __exit wl2864c_exit(void)
{
	i2c_del_driver(&wl2864c_i2c_driver);
}

module_init(wl2864c_init);
module_exit(wl2864c_exit);

MODULE_DESCRIPTION("WL2864 & WL2868 Power IC Driver");
MODULE_LICENSE("GPL v2");
