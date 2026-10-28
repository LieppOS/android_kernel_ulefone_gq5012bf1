// SPDX-License-Identifier: GPL-2.0+
// Driver for Awinic AW2013 3-channel LED driver

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#define AW2013_MAX_LEDS 3

/* Reset and ID register */
#define AW2013_RSTR 0x00
#define AW2013_RSTR_RESET 0x55
#define AW2013_RSTR_CHIP_ID 0x33

/* Global control register */
#define AW2013_GCR 0x01
#define AW2013_GCR_ENABLE BIT(0)

/* LED channel enable register */
#define AW2013_LCTR 0x30
#define AW2013_LCTR_LE(x) BIT((x))

/* LED channel control registers */
#define AW2013_LCFG(x) (0x31 + (x))
#define AW2013_LCFG_IMAX_MASK (BIT(0) | BIT(1)) // Should be 0-3
#define AW2013_LCFG_MD BIT(4)
#define AW2013_LCFG_FI BIT(5)
#define AW2013_LCFG_FO BIT(6)

/* LED channel PWM registers */
#define AW2013_REG_PWM(x) (0x34 + (x))

/* LED channel timing registers */
#define AW2013_LEDT0(x) (0x37 + (x) * 3)
#define AW2013_LEDT0_T1(x) ((x) << 4) // Should be 0-7
#define AW2013_LEDT0_T2(x) (x) // Should be 0-5

#define AW2013_LEDT1(x) (0x38 + (x) * 3)
#define AW2013_LEDT1_T3(x) ((x) << 4) // Should be 0-7
#define AW2013_LEDT1_T4(x) (x) // Should be 0-7

#define AW2013_LEDT2(x) (0x39 + (x) * 3)
#define AW2013_LEDT2_T0(x) ((x) << 4) // Should be 0-8
#define AW2013_LEDT2_REPEAT(x) (x) // Should be 0-15

#define AW2013_REG_MAX 0x77

#define AW2013_TIME_STEP 130 /* ms */

/* MediaTek /chosen "atag,boot" payload, as consumed by log_store. */
#define KERNEL_POWER_OFF_CHARGING_BOOT 8
#define LOW_POWER_OFF_CHARGING_BOOT 9

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

struct aw2013;

struct aw2013_led {
	struct aw2013 *chip;
	struct led_classdev cdev;
	u32 num;
	unsigned int imax;
	u32 fixed_brightness;
};

struct aw2013 {
	struct mutex mutex; /* held when writing to registers */
	struct i2c_client *client;
	struct aw2013_led leds[AW2013_MAX_LEDS];
	struct regmap *regmap;
	int num_leds;
	bool enabled;
};

static struct aw2013 *gftk_leds;
static int aw2013_pwd_gpio;

unsigned int led_aw2103_get_boot_mode(void);
int led_aw2103_control(void);

/*
 * Recovered signature: the stock KCFI type id preceding this function is
 * 0x837de525 == (u32)xxHash64("_ZTSFjvE") == unsigned int (void).
 * (int (void) would be 0x36b1c5a6, which is what led_aw2103_control and
 * init_module carry.)
 */
unsigned int led_aw2103_get_boot_mode(void)
{
	struct device_node *np_chosen;
	struct tag_bootmode *tag;

	np_chosen = of_find_node_by_path("/chosen");
	if (!np_chosen) {
		printk(KERN_NOTICE "log_store: warning: not find node: '/chosen'\n");

		np_chosen = of_find_node_by_path("/chosen@0");
		if (!np_chosen) {
			printk(KERN_NOTICE
			       "log_store: warning: not find node: '/chosen@0'\n");
			return 0;
		}
	}

	tag = (struct tag_bootmode *)of_get_property(np_chosen, "atag,boot",
						     NULL);
	if (!tag) {
		printk(KERN_NOTICE
		       "log_store: error: not find tag: 'atag,boot';\n");
		return 0;
	}

	printk(KERN_NOTICE "log_store: bootmode: 0x%x boottype: 0x%x.\n",
	       tag->bootmode, tag->boottype);

	return tag->bootmode;
}

static int aw2013_chip_init(struct aw2013 *chip)
{
	int i, ret;

	ret = regmap_write(chip->regmap, AW2013_GCR, AW2013_GCR_ENABLE);
	if (ret) {
		dev_err(&chip->client->dev, "Failed to enable the chip: %d\n",
			ret);
		return ret;
	}

	for (i = 0; i < chip->num_leds; i++) {
		ret = regmap_update_bits(chip->regmap,
					 AW2013_LCFG(chip->leds[i].num),
					 AW2013_LCFG_IMAX_MASK,
					 chip->leds[i].imax);
		if (ret) {
			dev_err(&chip->client->dev,
				"Failed to set maximum current for led %d: %d\n",
				chip->leds[i].num, ret);
			return ret;
		}
	}

	return ret;
}

static void aw2013_chip_disable(struct aw2013 *chip)
{
	if (!chip->enabled)
		return;

	regmap_write(chip->regmap, AW2013_GCR, 0);

	chip->enabled = false;
}

static int aw2013_chip_enable(struct aw2013 *chip)
{
	int ret;

	if (chip->enabled)
		return 0;

	chip->enabled = true;

	ret = aw2013_chip_init(chip);
	if (ret)
		aw2013_chip_disable(chip);

	return ret;
}

static bool aw2013_chip_in_use(struct aw2013 *chip)
{
	int i;

	for (i = 0; i < chip->num_leds; i++)
		if (chip->leds[i].cdev.brightness)
			return true;

	return false;
}

static int aw2013_brightness_set(struct led_classdev *cdev,
				 enum led_brightness brightness)
{
	struct aw2013_led *led = container_of(cdev, struct aw2013_led, cdev);
	int ret, num;

	mutex_lock(&led->chip->mutex);

	if (aw2013_chip_in_use(led->chip)) {
		ret = aw2013_chip_enable(led->chip);
		if (ret)
			goto error;
	}

	num = led->num;

	if (brightness)
		brightness = led->fixed_brightness;

	ret = regmap_write(led->chip->regmap, AW2013_REG_PWM(num), brightness);
	if (ret)
		goto error;

	if (brightness) {
		ret = regmap_update_bits(led->chip->regmap, AW2013_LCTR,
					 AW2013_LCTR_LE(num), 0xFF);
	} else {
		ret = regmap_update_bits(led->chip->regmap, AW2013_LCTR,
					 AW2013_LCTR_LE(num), 0);
		if (ret)
			goto error;
		ret = regmap_update_bits(led->chip->regmap, AW2013_LCFG(num),
					 AW2013_LCFG_MD, 0);
	}
	if (ret)
		goto error;

	if (!aw2013_chip_in_use(led->chip))
		aw2013_chip_disable(led->chip);

error:
	mutex_unlock(&led->chip->mutex);

	return ret;
}

int led_aw2103_control(void)
{
	union power_supply_propval val = { 0 };
	struct power_supply *psy;
	int boot_mode;

	boot_mode = led_aw2103_get_boot_mode();
	printk("leds-rgb-aw2013[%s]  led_aw2103_boot_mode=%d\n", __func__,
	       boot_mode);

	if (boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT ||
	    boot_mode == LOW_POWER_OFF_CHARGING_BOOT) {
		psy = power_supply_get_by_name("3rd-gauge");
		if (!psy) {
			printk("%s: get power supply failed\n", __func__);
			return -1;
		}

		power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY,
					  &val);
		printk("leds-rgb-aw2013[%s]  val=%d\n", __func__, val.intval);

		if (val.intval >= 90) {
			gftk_leds->leds[1].cdev.brightness = 6;
			aw2013_brightness_set(&gftk_leds->leds[1].cdev, 6);
		} else if (val.intval <= 15) {
			gftk_leds->leds[0].cdev.brightness = 6;
			aw2013_brightness_set(&gftk_leds->leds[0].cdev, 6);
		} else {
			gftk_leds->leds[2].cdev.brightness = 6;
			aw2013_brightness_set(&gftk_leds->leds[2].cdev, 6);
		}
	}

	return 0;
}

static int aw2013_blink_set(struct led_classdev *cdev,
			    unsigned long *delay_on, unsigned long *delay_off)
{
	struct aw2013_led *led = container_of(cdev, struct aw2013_led, cdev);
	int ret, num = led->num;
	unsigned long off = 0, on = 0;

	/* If no blink specified, default to 1 Hz. */
	if (!*delay_off && !*delay_on) {
		*delay_off = 500;
		*delay_on = 500;
	}

	if (!led->cdev.brightness) {
		led->cdev.brightness = LED_FULL;
		ret = aw2013_brightness_set(&led->cdev, led->cdev.brightness);
		if (ret)
			return ret;
	}

	/* Never on - just set to off */
	if (!*delay_on) {
		led->cdev.brightness = LED_OFF;
		return aw2013_brightness_set(&led->cdev, LED_OFF);
	}

	mutex_lock(&led->chip->mutex);

	/* Never off - brightness is already set, disable blinking */
	if (!*delay_off) {
		ret = regmap_update_bits(led->chip->regmap, AW2013_LCFG(num),
					 AW2013_LCFG_MD, 0);
		goto out;
	}

	/* Convert into values the HW will understand. */
	off = min(5, ilog2((*delay_off - 1) / AW2013_TIME_STEP) + 1);
	on = min(7, ilog2((*delay_on - 1) / AW2013_TIME_STEP) + 1);

	*delay_off = BIT(off) * AW2013_TIME_STEP;
	*delay_on = BIT(on) * AW2013_TIME_STEP;

	/* Set timings */
	ret = regmap_write(led->chip->regmap,
			   AW2013_LEDT0(num), AW2013_LEDT0_T2(on));
	if (ret)
		goto out;
	ret = regmap_write(led->chip->regmap,
			   AW2013_LEDT1(num), AW2013_LEDT1_T4(off));
	if (ret)
		goto out;

	/* Finally, enable the LED */
	ret = regmap_update_bits(led->chip->regmap, AW2013_LCFG(num),
				 AW2013_LCFG_MD, 0xFF);
	if (ret)
		goto out;

	ret = regmap_update_bits(led->chip->regmap, AW2013_LCTR,
				 AW2013_LCTR_LE(num), 0xFF);

out:
	mutex_unlock(&led->chip->mutex);

	return ret;
}

static int yft_aw2013_parse_dts(struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node;
	int gpio;
	int gpio_state;
	int ret;

	if (!np)
		goto no_node;

	gpio = of_get_named_gpio(np, "aw2013-pwd-gpio", 0);
	if (gpio < 0) {
		printk("%s: of_get_named_gpio fail. \n", __func__);
		goto fail;
	}
	printk("%s: of_get_named_gpio GPIO is %d.\n", __func__, gpio);

	if (!gpio_to_desc(gpio)) {
		printk("%s: gpio_desc is null.\n", __func__);
		goto fail;
	}
	printk("%s: gpio_desc is not null.\n", __func__);

	if (gpio_is_valid(gpio))
		printk("%s: gpio number %d is valid. \n", __func__, gpio);

	ret = gpio_request(gpio, "aw2013-pwd-gpio");
	if (ret) {
		printk("%s: gpio_request fail. \n", __func__);
		goto fail;
	}

	ret = gpio_direction_output(gpio, 1);
	if (ret) {
		printk("%s: gpio_direction_output failed. \n", __func__);
		goto fail;
	}

	gpio_state = gpio_get_value(gpio);
	printk("%s: gpio_get_value =%d. \n", __func__, gpio_state);

	aw2013_pwd_gpio = gpio;
	printk("aw2013,chip-enable-gpio = %d. \n", gpio);

	return 0;

no_node:
	printk("%s: get gpio num fail. \n", __func__);

fail:
	aw2013_pwd_gpio = -1;
	printk(" yft_parse_dts get gpio fail. aw2013_pwd_gpio\n");

	return -ENODEV;
}

static int aw2013_probe_dt(struct aw2013 *chip)
{
	struct device_node *np = dev_of_node(&chip->client->dev), *child;
	int count, ret = 0, i = 0;
	struct aw2013_led *led;

	count = of_get_available_child_count(np);
	if (!count || count > AW2013_MAX_LEDS)
		return -EINVAL;

	regmap_write(chip->regmap, AW2013_RSTR, AW2013_RSTR_RESET);

	for_each_available_child_of_node(np, child) {
		struct led_init_data init_data = {};
		u32 source;
		u32 imax;
		u32 fixed_brightness;

		ret = of_property_read_u32(child, "reg", &source);
		if (ret != 0 || source >= AW2013_MAX_LEDS) {
			dev_err(&chip->client->dev,
				"Couldn't read LED address: %d\n", ret);
			count--;
			continue;
		}

		led = &chip->leds[i];
		led->num = source;
		led->chip = chip;
		init_data.fwnode = of_fwnode_handle(child);

		if (!of_property_read_u32(child, "led-max-microamp", &imax)) {
			led->imax = min_t(u32, imax / 5000, 3);
		} else {
			led->imax = 1; // 5mA
			dev_info(&chip->client->dev,
				 "DT property led-max-microamp is missing\n");
		}

		if (!of_property_read_u32(child, "led-fixed-brightness",
					  &fixed_brightness))
			led->fixed_brightness = fixed_brightness;
		else
			led->fixed_brightness = LED_FULL;

		led->cdev.brightness_set_blocking = aw2013_brightness_set;
		led->cdev.blink_set = aw2013_blink_set;

		ret = devm_led_classdev_register_ext(&chip->client->dev,
						     &led->cdev, &init_data);
		if (ret < 0) {
			of_node_put(child);
			return ret;
		}

		i++;
	}

	if (!count)
		return -EINVAL;

	chip->num_leds = i;

	return 0;
}

static const struct regmap_config aw2013_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AW2013_REG_MAX,
};

static int aw2013_probe(struct i2c_client *client)
{
	struct aw2013 *chip;
	int ret;
	int gpio;
	int gpio_state;
	unsigned int chipid;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	ret = yft_aw2013_parse_dts(client);
	if (ret)
		return ret;

	gpio = aw2013_pwd_gpio;
	gpio_state = gpio_get_value(gpio);
	printk("aw2013_probe -1: chip-enable-gpio = (%d) gpio_state = (%d)  end. \n",
	       gpio, gpio_state);

	gpio_direction_output(aw2013_pwd_gpio, 1);

	gpio = aw2013_pwd_gpio;
	gpio_state = gpio_get_value(gpio);
	printk("aw2013_probe -2: chip-enable-gpio = (%d) gpio_state = (%d)  end. \n",
	       gpio, gpio_state);

	mutex_init(&chip->mutex);
	mutex_lock(&chip->mutex);

	chip->client = client;
	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &aw2013_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = regmap_read(chip->regmap, AW2013_RSTR, &chipid);
	if (ret) {
		dev_err(&client->dev, "Failed to read chip ID: %d\n",
			ret);
		return ret;
	}

	if (chipid != AW2013_RSTR_CHIP_ID) {
		dev_err(&client->dev, "Chip reported wrong ID: %x\n",
			chipid);
		return -ENODEV;
	}

	ret = aw2013_probe_dt(chip);
	if (ret < 0)
		return ret;

	mutex_unlock(&chip->mutex);

	gftk_leds = chip;
	led_aw2103_control();

	return 0;
}

static void aw2013_remove(struct i2c_client *client)
{
	struct aw2013 *chip = i2c_get_clientdata(client);

	aw2013_chip_disable(chip);
}

static const struct of_device_id aw2013_match_table[] = {
	{ .compatible = "awinic,rgb,aw2013", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, aw2013_match_table);

static struct i2c_driver aw2013_driver = {
	.driver = {
		.name = "leds-rgb-aw2013",
		.of_match_table = of_match_ptr(aw2013_match_table),
	},
	.probe_new = aw2013_probe,
	.remove = aw2013_remove,
};

module_i2c_driver(aw2013_driver);

MODULE_AUTHOR("Nikita Travkin <nikitos.tr@gmail.com>");
MODULE_DESCRIPTION("AW2013 LED driver");
MODULE_LICENSE("GPL v2");
