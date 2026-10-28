// SPDX-License-Identifier: GPL-2.0
/* Stock-oracle reconstruction for GQ5012BF1. */
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include "include/mt-plat/mtk_pwm.h"

#define PWM_NO 3
#define NS_40MS 40000000LL
#define NS_50MS 50000000LL
#define NS_200MS 200000000LL
#define NS_500MS 500000000LL
#define NS_1S 1000000000LL

enum camp_mode { CAMP_OFF, CAMP_LOW, CAMP_NORMAL, CAMP_HIGH, CAMP_BLINK, CAMP_SOS };
enum leds_mode { LEDS_ALL_OFF, LEDS_RED_ON, LEDS_BLUE_ON, LEDS_ALL_ON,
	LEDS_RED_FLASH, LEDS_BLUE_FLASH, LEDS_RED_BLUE_FLASH };

struct ln2403_data {
	struct mutex lock;                 /* 0x00 */
	ktime_t gpio_low_ns;               /* 0x30 */
	ktime_t gpio_high_ns;              /* 0x38 */
	int sos_count;                     /* 0x40 */
	int gpio_ctrl_gpio;                /* 0x44 */
	int camplight_mode;                /* 0x48 */
	int ln2403_en_gpio;                /* 0x4c */
	int ln2403_power_gpio;             /* 0x50 */
	int leds_power_gpio;               /* 0x54 */
	int leds_red_gpio;                 /* 0x58 */
	int leds_blue_gpio;                /* 0x5c */
	int leds_flash_state;              /* 0x60 */
	int leds_mode;                     /* 0x64 */
	struct hrtimer redblue_timer;      /* 0x68 */
	struct wakeup_source *redblue_ws;  /* 0xb0 */
	u8 gpio_timer_enabled;             /* 0xb8 */
	u8 pwm_enabled;                    /* 0xb9 */
	u8 _pad[6];
	ktime_t gpio_interval;             /* 0xc0 */
	struct hrtimer gpio_timer;         /* 0xc8 */
	struct wakeup_source *ln2403_ws;   /* 0x110 */
};

static struct ln2403_data *ln2403_chip_data;
static struct pinctrl *ln2403_pinctrl;
static struct pinctrl_state *ln2403_pwmoff_low;
static struct pinctrl_state *ln2403_pwmon;
static struct pinctrl_state *ln2403_pwmoff_high;
static int red_blue_flash_state;
static int camplight_duty;

static const char * const ln2403_mode_str[] = {
	"OFF", "LOW", "NORMAL", "HIGH", "BLINK", "SOS"
};
static const char * const ln2403_leds_str[] = {
	"ALL_OFF", "RED_ON", "BLUE_ON", "ALL_ON", "RED_FLASH",
	"BLUE_FLASH", "RED_BLUE_FLASH"
};

static inline void raw_gpio_set(int gpio, int value)
{
	gpiod_set_raw_value(gpio_to_desc(gpio), value);
}

static void set_leds(int red, int blue)
{
	raw_gpio_set(ln2403_chip_data->leds_power_gpio, !!(red | blue));
	raw_gpio_set(ln2403_chip_data->leds_red_gpio, red);
	raw_gpio_set(ln2403_chip_data->leds_blue_gpio, blue);
}

static enum hrtimer_restart redblue_led_timer_func(struct hrtimer *timer)
{
	struct ln2403_data *d = ln2403_chip_data;
	int state;

	switch (d->leds_mode) {
	case LEDS_RED_FLASH:
		state = !d->leds_flash_state;
		d->leds_flash_state = state;
		set_leds(state, 0);
		break;
	case LEDS_BLUE_FLASH:
		state = !d->leds_flash_state;
		d->leds_flash_state = state;
		set_leds(0, state);
		break;
	case LEDS_RED_BLUE_FLASH:
		switch (red_blue_flash_state) {
		case 0: set_leds(1, 0); red_blue_flash_state = 1; break;
		case 1: set_leds(0, 0); red_blue_flash_state = 2; break;
		case 2: set_leds(0, 1); red_blue_flash_state = 3; break;
		case 3: set_leds(0, 0); red_blue_flash_state = 0; break;
		default: break;
		}
		break;
	default:
		break;
	}
	hrtimer_forward(timer, timer->base->get_time(), ns_to_ktime(NS_40MS));
	return HRTIMER_RESTART;
}

static enum hrtimer_restart gpio_ctrl_timer_handler(struct hrtimer *timer)
{
	struct ln2403_data *d = ln2403_chip_data;
	ktime_t next;
	int value;

	if (d->camplight_mode == CAMP_SOS) {
		if (d->sos_count > 17) {
			d->sos_count = 0;
			d->gpio_low_ns = ns_to_ktime(NS_200MS);
			d->gpio_high_ns = ns_to_ktime(NS_200MS);
		} else if (d->sos_count <= 5) {
			d->gpio_low_ns = ns_to_ktime(NS_200MS);
			d->gpio_high_ns = ns_to_ktime(NS_200MS);
		} else if (d->sos_count <= 11) {
			d->gpio_low_ns = ns_to_ktime(NS_500MS);
			d->gpio_high_ns = ns_to_ktime(NS_50MS);
		} else {
			d->gpio_high_ns = ns_to_ktime(NS_200MS);
			if (d->sos_count == 17)
				d->gpio_low_ns = ns_to_ktime(NS_1S);
		}
		d->sos_count++;
	}

	value = gpiod_get_raw_value(gpio_to_desc(d->gpio_ctrl_gpio));
	if (value) {
		if (ktime_to_ns(d->gpio_low_ns)) {
			raw_gpio_set(d->gpio_ctrl_gpio, 0);
			next = d->gpio_low_ns;
		} else {
			next = d->gpio_interval;
		}
	} else {
		if (ktime_to_ns(d->gpio_high_ns)) {
			raw_gpio_set(d->gpio_ctrl_gpio, 1);
			next = d->gpio_high_ns;
		} else {
			next = d->gpio_interval;
		}
	}
	d->gpio_interval = next;
	hrtimer_forward(timer, timer->base->get_time(), next);
	return HRTIMER_RESTART;
}

static void ln2403_gpio_ctrl_apply(int enable)
{
	struct ln2403_data *d = ln2403_chip_data;
	bool on = !!enable;

	if (d->gpio_timer_enabled == on)
		return;
	mutex_lock(&d->lock);
	if (enable) {
		hrtimer_init(&d->gpio_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		d->gpio_timer.function = gpio_ctrl_timer_handler;
		d->gpio_interval = d->gpio_high_ns;
		hrtimer_start(&d->gpio_timer, d->gpio_interval, HRTIMER_MODE_REL);
		pr_info("%s %d Start timer, kt %lld\n", __func__, __LINE__,
			ktime_to_ns(d->gpio_interval));
	} else {
		hrtimer_cancel(&d->gpio_timer);
		d->gpio_low_ns = 0;
		d->gpio_high_ns = 0;
		pr_info("%s %d cancel timer\n", __func__, __LINE__);
	}
	mutex_unlock(&d->lock);
	d->gpio_timer_enabled = on;
}

static void ln2403_pwm_work(int enable, int percents, int data_width)
{
	struct pwm_spec_config conf = { 0 };
	int thresh;

	pr_info("%s: enable=%d, ln2403_chip_data->pwm_enabled=%d\n", __func__,
		enable & 1, ln2403_chip_data->pwm_enabled);
	if (enable) {
		if (!IS_ERR(ln2403_pwmon))
			pinctrl_select_state(ln2403_pinctrl, ln2403_pwmon);
		thresh = percents * data_width / 100;
		pr_info("%s: enable, percents=%d, thresh=%d, data_width=%d\n",
			__func__, percents, thresh, data_width);
		conf.pwm_no = PWM_NO;
		conf.mode = 0;
		conf.clk_div = 0;
		conf.clk_src = 0;
		conf.PWM_MODE_OLD_REGS.DATA_WIDTH = data_width;
		conf.PWM_MODE_OLD_REGS.THRESH = thresh;
		pwm_set_spec_config(&conf);
	} else {
		pr_info("%s: disable\n", __func__);
		if (ln2403_chip_data->pwm_enabled)
			mt_pwm_disable(PWM_NO, 0);
		if (!IS_ERR(ln2403_pwmoff_high))
			pinctrl_select_state(ln2403_pinctrl, ln2403_pwmoff_high);
	}
	ln2403_chip_data->pwm_enabled = !!enable;
}

static ssize_t mt_camplight_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (!dev) {
		pr_info("mt_camplight_mode_show. dev is null!!\n");
		return 0;
	}
	return scnprintf(buf, PAGE_SIZE, "%s\n",
		ln2403_mode_str[ln2403_chip_data->camplight_mode]);
}

static ssize_t mt_camplight_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct ln2403_data *d = ln2403_chip_data;
	int mode = 0;

	if (sscanf(buf, "%d", &mode) != 1)
		return count;
	if (mode >= 6) {
		pr_info("%s, set mode error, please reset!\n", __func__);
		return count;
	}
	if (d->camplight_mode == mode)
		return count;

	if (d->gpio_timer_enabled)
		ln2403_gpio_ctrl_apply(0);
	ln2403_pwm_work(0, 0, 0);
	raw_gpio_set(d->ln2403_en_gpio, 0);
	raw_gpio_set(d->ln2403_power_gpio, 0);

	if (mode) {
		if (!d->ln2403_ws->active)
			__pm_stay_awake(d->ln2403_ws);
		switch (mode) {
		case CAMP_LOW:
			ln2403_pwm_work(1, 97, 17);
			raw_gpio_set(d->ln2403_en_gpio, 1);
			break;
		case CAMP_NORMAL:
			ln2403_pwm_work(1, 73, 17);
			raw_gpio_set(d->ln2403_en_gpio, 1);
			break;
		case CAMP_HIGH:
			ln2403_pwm_work(1, 11, 17);
			raw_gpio_set(d->ln2403_en_gpio, 1);
			break;
		case CAMP_BLINK:
			d->gpio_low_ns = ns_to_ktime(NS_50MS);
			d->gpio_high_ns = ns_to_ktime(NS_50MS);
			d->gpio_ctrl_gpio = d->ln2403_en_gpio;
			if (!IS_ERR(ln2403_pwmoff_low))
				pinctrl_select_state(ln2403_pinctrl, ln2403_pwmoff_low);
			ln2403_gpio_ctrl_apply(1);
			break;
		case CAMP_SOS:
			d->gpio_low_ns = ns_to_ktime(NS_200MS);
			d->gpio_high_ns = ns_to_ktime(NS_200MS);
			d->sos_count = 0;
			d->gpio_ctrl_gpio = d->ln2403_en_gpio;
			if (!IS_ERR(ln2403_pwmoff_low))
				pinctrl_select_state(ln2403_pinctrl, ln2403_pwmoff_low);
			ln2403_gpio_ctrl_apply(1);
			break;
		default:
			break;
		}
		raw_gpio_set(d->ln2403_power_gpio, 1);
	} else {
		ln2403_pwm_work(0, 0, 0);
		if (!IS_ERR(ln2403_pwmoff_low))
			pinctrl_select_state(ln2403_pinctrl, ln2403_pwmoff_low);
		if (d->ln2403_ws->active)
			__pm_relax(d->ln2403_ws);
	}
	d->camplight_mode = mode;
	return count;
}

static ssize_t leds_ctl_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int red, blue;
	if (!dev) {
		pr_info("leds_ctl_show. dev is null!!\n");
		return 0;
	}
	red = gpiod_get_raw_value(gpio_to_desc(ln2403_chip_data->leds_red_gpio));
	blue = gpiod_get_raw_value(gpio_to_desc(ln2403_chip_data->leds_blue_gpio));
	pr_info("[%s]camplight, red=%d,blue=%d\n", __func__, red, blue);
	return scnprintf(buf, PAGE_SIZE, "%s\n",
		ln2403_leds_str[ln2403_chip_data->leds_mode]);
}

static ssize_t leds_ctl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct ln2403_data *d = ln2403_chip_data;
	int mode = 0;

	if (sscanf(buf, "%d", &mode) != 1)
		return count;
	pr_info("[%s]camplight. mode=%d\n", __func__, mode);
	hrtimer_cancel(&d->redblue_timer);
	d->leds_flash_state = 0;
	d->leds_mode = mode;
	red_blue_flash_state = 3;
	if ((unsigned int)mode > LEDS_RED_BLUE_FLASH)
		return count;
	if (mode >= LEDS_RED_FLASH) {
		if (!d->redblue_ws->active)
			__pm_stay_awake(d->redblue_ws);
		hrtimer_start(&d->redblue_timer, ns_to_ktime(NS_40MS), HRTIMER_MODE_REL);
	} else if (mode == LEDS_ALL_OFF) {
		if (d->redblue_ws->active)
			__pm_relax(d->redblue_ws);
		set_leds(0, 0);
	} else if (mode == LEDS_RED_ON) {
		if (!d->redblue_ws->active)
			__pm_stay_awake(d->redblue_ws);
		set_leds(1, 0);
	} else if (mode == LEDS_BLUE_ON) {
		if (!d->redblue_ws->active)
			__pm_stay_awake(d->redblue_ws);
		set_leds(0, 1);
	} else {
		if (!d->redblue_ws->active)
			__pm_stay_awake(d->redblue_ws);
		set_leds(1, 1);
	}
	return count;
}

static ssize_t camplight_set_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (!dev) {
		pr_info("camplight_set_brightness_show. dev is null!!\n");
		return 0;
	}
	pr_info("%s, camplight_duty=%d\n", __func__, camplight_duty);
	return scnprintf(buf, PAGE_SIZE, "%d\n", camplight_duty);
}

static ssize_t camplight_set_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct ln2403_data *d = ln2403_chip_data;
	if (sscanf(buf, "%d", &camplight_duty) != 1)
		return count;
	pr_info("%s, 1 camplight_duty=%d\n", __func__, camplight_duty);
	if (camplight_duty) {
		if (!d->ln2403_ws->active)
			__pm_stay_awake(d->ln2403_ws);
		if (camplight_duty >= 94)
			camplight_duty = 94;
		camplight_duty = 100 - camplight_duty;
		ln2403_pwm_work(1, camplight_duty, 100);
		raw_gpio_set(d->ln2403_power_gpio, 1);
		raw_gpio_set(d->ln2403_en_gpio, 1);
	} else {
		raw_gpio_set(d->ln2403_en_gpio, 0);
		raw_gpio_set(d->ln2403_power_gpio, 0);
		ln2403_pwm_work(0, 0, 0);
		if (!IS_ERR(ln2403_pwmoff_low))
			pinctrl_select_state(ln2403_pinctrl, ln2403_pwmoff_low);
		if (d->ln2403_ws->active)
			__pm_relax(d->ln2403_ws);
	}
	return count;
}

static DEVICE_ATTR(camplight_mode, 0644, mt_camplight_mode_show, mt_camplight_mode_store);
static DEVICE_ATTR(leds_ctl, 0644, leds_ctl_show, leds_ctl_store);
static DEVICE_ATTR(camplight_set_brightness, 0644,
	camplight_set_brightness_show, camplight_set_brightness_store);

static struct device_attribute * const mt_sysfs_attributes[] = {
	&dev_attr_camplight_mode,
	&dev_attr_leds_ctl,
	&dev_attr_camplight_set_brightness,
	NULL,
};

static int ln2403_get_gpio(struct device_node *node, const char *name)
{
	int gpio = of_get_named_gpio_flags(node, name, 0, NULL);
	if (gpio < 0) {
		pr_info("%s: of_get_named_gpio fail. \n", __func__);
		return -1;
	}
	pr_info("%s: of_get_named_gpio GPIO is %d.\n", __func__, gpio);
	if (!gpio_to_desc(gpio)) {
		pr_info("%s: gpio_desc is null.\n", __func__);
		return -1;
	}
	pr_info("%s: gpio_desc is not null.\n", __func__);
	if (gpio <= 511)
		pr_info("%s: gpio number %d is valid. \n", __func__, gpio);
	if (gpio_request(gpio, name)) {
		pr_info("%s: gpio_request fail. \n", __func__);
		return -1;
	}
	return gpio;
}

static int camplight_probe(struct platform_device *pdev)
{
	struct device_node *node;
	struct ln2403_data *d;
	int ret, i;

	pr_info("camplight_probe start\n");
	d = kzalloc(sizeof(*d), GFP_KERNEL);
	ln2403_chip_data = d;
	if (!d) {
		pr_info("no memory for ln2403_chip_data\n");
		return -ENOMEM;
	}
	pr_info("%s: enter. \n", "ln2403_parse_dts");
	node = of_find_compatible_node(NULL, NULL, "mediatek,yft_camplight");
	if (!node) {
		pr_info("%s: cannot get the node: 'mediatek,yft_camplight'.\n", "ln2403_parse_dts");
		return -ENODEV;
	}
	d->ln2403_en_gpio = ln2403_get_gpio(node, "ln2403-en-gpio");
	if (d->ln2403_en_gpio < 0) return -1;
	d->ln2403_power_gpio = ln2403_get_gpio(node, "ln2403-power-gpio");
	if (d->ln2403_power_gpio < 0) return -1;
	d->leds_power_gpio = ln2403_get_gpio(node, "leds-power-gpio");
	if (d->leds_power_gpio < 0) return -1;
	d->leds_red_gpio = ln2403_get_gpio(node, "leds-red-gpio");
	if (d->leds_red_gpio < 0) return -1;
	d->leds_blue_gpio = ln2403_get_gpio(node, "leds-blue-gpio");
	if (d->leds_blue_gpio < 0) return -1;

	ln2403_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(ln2403_pinctrl))
		return -1;
	ret = 0;
	ln2403_pwmoff_high = pinctrl_lookup_state(ln2403_pinctrl, "ln2403_pwmoff_high");
	if (IS_ERR(ln2403_pwmoff_high)) ret = PTR_ERR(ln2403_pwmoff_high);
	ln2403_pwmoff_low = pinctrl_lookup_state(ln2403_pinctrl, "ln2403_pwmoff_low");
	if (IS_ERR(ln2403_pwmoff_low)) ret = PTR_ERR(ln2403_pwmoff_low);
	ln2403_pwmon = pinctrl_lookup_state(ln2403_pinctrl, "ln2403_pwmon");
	if (IS_ERR(ln2403_pwmon)) ret = PTR_ERR(ln2403_pwmon);
	if (ret)
		return -1;

	for (i = 0; mt_sysfs_attributes[i]; i++) {
		ret = device_create_file(&pdev->dev, mt_sysfs_attributes[i]);
		if (ret) {
			while (i >= 0)
				device_remove_file(&pdev->dev, mt_sysfs_attributes[i--]);
			return ret;
		}
	}

	mutex_init(&d->lock);
	raw_gpio_set(d->ln2403_en_gpio, 0);
	raw_gpio_set(d->ln2403_power_gpio, 0);
	set_leds(0, 0);
	hrtimer_init(&d->redblue_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	d->redblue_timer.function = redblue_led_timer_func;
	d->redblue_ws = wakeup_source_register(NULL, "redblue_led_wake_lock");
	d->pwm_enabled = 0;
	if (!IS_ERR(ln2403_pwmoff_low))
		pinctrl_select_state(ln2403_pinctrl, ln2403_pwmoff_low);
	d->camplight_mode = CAMP_OFF;
	d->ln2403_ws = wakeup_source_register(NULL, "ln2403_wake_lock");
	pr_info("ln2403_probe end\n");
	return 0;
}

static int camplight_remove(struct platform_device *pdev)
{
	pr_info("%s %d.\n", __func__, __LINE__);
	return 0;
}

static const struct of_device_id camplight_of_match[] = {
	{ .compatible = "mediatek,yft_camplight" },
	{ }
};
MODULE_DEVICE_TABLE(of, camplight_of_match);

static struct platform_driver camplight_driver = {
	.probe = camplight_probe,
	.remove = camplight_remove,
	.driver = {
		.name = "yft_camplight",
		.of_match_table = camplight_of_match,
	},
};
module_platform_driver(camplight_driver);

MODULE_DESCRIPTION("Module For PWM LN2403");
MODULE_AUTHOR("yuanliang, <yuanliang@dazhi.sh.cn>");
MODULE_LICENSE("GPL");
