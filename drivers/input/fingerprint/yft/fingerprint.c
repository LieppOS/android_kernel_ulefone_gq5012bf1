// SPDX-License-Identifier: GPL-2.0
/*
 * Stock-oracle-led reconstruction of the GQ5012BF1 YFT fingerprint glue.
 * Byte-identical to stock fingerprint.ko except .note.gnu.build-id.
 * spi.h + compat.h: stock __COUNTER__ is 21 above the minimal include set;
 * any include set adding 21 is byte-equivalent (the exact set is not unique).
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/wait.h>
#include <linux/spi/spi.h>
#include <linux/compat.h>

struct pinctrl *yft_finger_pinctrl;
struct pinctrl_state *yft_finger_reset_high;
struct pinctrl_state *yft_finger_reset_low;
struct pinctrl_state *yft_finger_spi0_mi_as_spi0_mi;
struct pinctrl_state *yft_finger_spi0_mi_as_gpio;
struct pinctrl_state *yft_finger_spi0_mo_as_spi0_mo;
struct pinctrl_state *yft_finger_spi0_mo_as_gpio;
struct pinctrl_state *yft_finger_spi0_clk_as_spi0_clk;
struct pinctrl_state *yft_finger_spi0_clk_as_gpio;
struct pinctrl_state *yft_finger_spi0_cs_as_spi0_cs;
struct pinctrl_state *yft_finger_spi0_cs_as_gpio;
struct pinctrl_state *yft_finger_eint_pull_down;
struct pinctrl_state *yft_finger_eint_pull_up;
struct pinctrl_state *yft_finger_eint_pull_dis;
struct pinctrl_state *yft_finger_power_on;
struct pinctrl_state *yft_finger_power_off;

static struct platform_device *yft_finger_plat;
DECLARE_WAIT_QUEUE_HEAD(finger_init_waiter);

int yft_finger_get_gpio_info(struct platform_device *pdev)
{
	struct device_node *node;
	int ret;

	node = of_find_compatible_node(NULL, NULL, "mediatek,yft_finger");
	printk("node.name <%s> full name <%s>\n", node->name, node->full_name);
	wake_up_interruptible(&finger_init_waiter);

	yft_finger_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(yft_finger_pinctrl)) {
		ret = PTR_ERR(yft_finger_pinctrl);
		dev_err(&pdev->dev, "yft_finger cannot find pinctrl and ret = (%d)\n", ret);
		return ret;
	}

	printk("[%s] yft_finger_pinctrl+++++++++++++++++\n", pdev->name);

	yft_finger_reset_high = pinctrl_lookup_state(yft_finger_pinctrl,
						     "finger_reset_en1");
	if (IS_ERR(yft_finger_reset_high)) {
		ret = PTR_ERR(yft_finger_reset_high);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_reset_high!\n");
		return ret;
	}

	yft_finger_reset_low = pinctrl_lookup_state(yft_finger_pinctrl,
						    "finger_reset_en0");
	if (IS_ERR(yft_finger_reset_low)) {
		ret = PTR_ERR(yft_finger_reset_low);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_reset_low!\n");
		return ret;
	}

	pr_err("yft_finger_get_gpio_info: no need finger_power_en");

	yft_finger_spi0_mi_as_spi0_mi = pinctrl_lookup_state(yft_finger_pinctrl,
							      "finger_spi0_mi_as_spi0_mi");
	if (IS_ERR(yft_finger_spi0_mi_as_spi0_mi)) {
		ret = PTR_ERR(yft_finger_spi0_mi_as_spi0_mi);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_spi0_mi_as_spi0_mi!\n");
		return ret;
	}

	yft_finger_spi0_mi_as_gpio = pinctrl_lookup_state(yft_finger_pinctrl,
							   "finger_spi0_mi_as_gpio");
	if (IS_ERR(yft_finger_spi0_mi_as_gpio)) {
		ret = PTR_ERR(yft_finger_spi0_mi_as_gpio);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_spi0_mi_as_gpio!\n");
		return ret;
	}

	yft_finger_spi0_mo_as_spi0_mo = pinctrl_lookup_state(yft_finger_pinctrl,
							      "finger_spi0_mo_as_spi0_mo");
	if (IS_ERR(yft_finger_spi0_mo_as_spi0_mo)) {
		ret = PTR_ERR(yft_finger_spi0_mo_as_spi0_mo);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_spi0_mo_as_spi0_mo!\n");
		return ret;
	}

	yft_finger_spi0_mo_as_gpio = pinctrl_lookup_state(yft_finger_pinctrl,
							   "finger_spi0_mo_as_gpio");
	if (IS_ERR(yft_finger_spi0_mo_as_gpio)) {
		ret = PTR_ERR(yft_finger_spi0_mo_as_gpio);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_spi0_mo_as_gpio!\n");
		return ret;
	}

	yft_finger_spi0_clk_as_spi0_clk = pinctrl_lookup_state(yft_finger_pinctrl,
								"finger_spi0_clk_as_spi0_clk");
	if (IS_ERR(yft_finger_spi0_clk_as_spi0_clk)) {
		ret = PTR_ERR(yft_finger_spi0_clk_as_spi0_clk);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_spi0_clk_as_spi0_clk!\n");
		return ret;
	}

	yft_finger_spi0_clk_as_gpio = pinctrl_lookup_state(yft_finger_pinctrl,
							    "finger_spi0_clk_as_gpio");
	if (IS_ERR(yft_finger_spi0_clk_as_gpio)) {
		ret = PTR_ERR(yft_finger_spi0_clk_as_gpio);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_spi0_clk_as_gpio!\n");
		return ret;
	}

	yft_finger_spi0_cs_as_spi0_cs = pinctrl_lookup_state(yft_finger_pinctrl,
							      "finger_spi0_cs_as_spi0_cs");
	if (IS_ERR(yft_finger_spi0_cs_as_spi0_cs)) {
		ret = PTR_ERR(yft_finger_spi0_cs_as_spi0_cs);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_spi0_cs_as_spi0_cs!\n");
		return ret;
	}

	yft_finger_spi0_cs_as_gpio = pinctrl_lookup_state(yft_finger_pinctrl,
							   "finger_spi0_cs_as_gpio");
	if (IS_ERR(yft_finger_spi0_cs_as_gpio)) {
		ret = PTR_ERR(yft_finger_spi0_cs_as_gpio);
		dev_err(&pdev->dev, " Cannot find yft_finger pinctrl yft_finger_spi0_cs_as_gpio!\n");
		return ret;
	}

	yft_finger_eint_pull_down = pinctrl_lookup_state(yft_finger_pinctrl,
							  "finger_eint_pull_down");
	if (IS_ERR(yft_finger_eint_pull_down)) {
		ret = PTR_ERR(yft_finger_eint_pull_down);
		dev_err(&pdev->dev, " Cannot find fp pinctrl yft_finger_eint_pull_down!\n");
		return ret;
	}

	yft_finger_eint_pull_up = pinctrl_lookup_state(yft_finger_pinctrl,
							"finger_eint_pull_up");
	if (IS_ERR(yft_finger_eint_pull_up)) {
		ret = PTR_ERR(yft_finger_eint_pull_up);
		dev_err(&pdev->dev, " Cannot find fp pinctrl yft_finger_eint_pull_up!\n");
		return ret;
	}

	yft_finger_eint_pull_dis = pinctrl_lookup_state(yft_finger_pinctrl,
							 "finger_eint_pull_dis");
	if (IS_ERR(yft_finger_eint_pull_dis)) {
		ret = PTR_ERR(yft_finger_eint_pull_dis);
		dev_err(&pdev->dev, " Cannot find fp pinctrl yft_finger_eint_pull_dis!\n");
		return ret;
	}

	printk("yft_finger get gpio info ok--------\n");
	return 0;
}

int yft_finger_probe_isok(bool probe_isok)
{
	return probe_isok;
}
EXPORT_SYMBOL(yft_finger_probe_isok);

int yft_finger_set_power(int enable)
{
	pr_err("yft_finger_set_power: no need gpio power");
	return 0;
}
EXPORT_SYMBOL(yft_finger_set_power);

void yft_finger_power_deinit(void)
{
	yft_finger_power_on = NULL;
	yft_finger_power_off = NULL;
	if (yft_finger_pinctrl)
		devm_pinctrl_put(yft_finger_pinctrl);
}
EXPORT_SYMBOL(yft_finger_power_deinit);

int yft_finger_set_reset(int value)
{
	if (IS_ERR(yft_finger_reset_low) || IS_ERR(yft_finger_reset_high)) {
		pr_err("err: yft_finger_reset_low or yft_finger_reset_high is error!!!");
		return -1;
	}

	if (value == 0)
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_reset_low);
	else if (value == 1)
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_reset_high);
	return 0;
}
EXPORT_SYMBOL(yft_finger_set_reset);

int yft_finger_set_irq(int value)
{
	if (IS_ERR(yft_finger_eint_pull_down) ||
	    IS_ERR(yft_finger_eint_pull_up) ||
	    IS_ERR(yft_finger_eint_pull_dis)) {
		pr_err("err: yft_finger_int_as_gpio is error!!!!");
		return -1;
	}

	if (value == 0)
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_eint_pull_down);
	else if (value == 1)
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_eint_pull_up);
	else if (value == 2)
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_eint_pull_dis);
	return 0;
}
EXPORT_SYMBOL(yft_finger_set_irq);

unsigned int yft_finger_get_irqnum(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "mediatek,yft_finger");
	return irq_of_parse_and_map(node, 0);
}
EXPORT_SYMBOL(yft_finger_get_irqnum);

unsigned int yft_finger_get_irq_gpio(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "mediatek,yft_finger");
	return of_get_named_gpio_flags(node, "int-gpio", 0, NULL);
}
EXPORT_SYMBOL(yft_finger_get_irq_gpio);

unsigned int yft_finger_get_reset_gpio(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "mediatek,yft_finger");
	return of_get_named_gpio_flags(node, "reset-gpio", 0, NULL);
}
EXPORT_SYMBOL(yft_finger_get_reset_gpio);

int yft_finger_set_spi_mode(int mode)
{
	if (IS_ERR(yft_finger_spi0_clk_as_gpio) ||
	    IS_ERR(yft_finger_spi0_cs_as_gpio) ||
	    IS_ERR(yft_finger_spi0_mi_as_gpio) ||
	    IS_ERR(yft_finger_spi0_mo_as_gpio) ||
	    IS_ERR(yft_finger_spi0_clk_as_spi0_clk) ||
	    IS_ERR(yft_finger_spi0_cs_as_spi0_cs) ||
	    IS_ERR(yft_finger_spi0_mi_as_spi0_mi) ||
	    IS_ERR(yft_finger_spi0_mo_as_spi0_mo)) {
		pr_err("err: yft_finger_reset_low or yft_finger_reset_high is error!!!");
		return -1;
	}

	if (mode == 0) {
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_spi0_clk_as_gpio);
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_spi0_cs_as_gpio);
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_spi0_mi_as_gpio);
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_spi0_mo_as_gpio);
	} else if (mode == 1) {
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_spi0_clk_as_spi0_clk);
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_spi0_cs_as_spi0_cs);
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_spi0_mi_as_spi0_mi);
		pinctrl_select_state(yft_finger_pinctrl, yft_finger_spi0_mo_as_spi0_mo);
	}
	return 0;
}
EXPORT_SYMBOL(yft_finger_set_spi_mode);

void yft_waite_for_finger_dts_paser(void)
{
	wait_event_interruptible_timeout(finger_init_waiter, yft_finger_plat,
					 3 * HZ);
}
EXPORT_SYMBOL(yft_waite_for_finger_dts_paser);

int yft_get_max_finger_spi_cs_number(void)
{
	return 2;
}

int yft_finger_plat_probe(struct platform_device *pdev)
{
	yft_finger_plat = pdev;
	printk("yft_finger_plat_probe entry\n");
	yft_finger_get_gpio_info(pdev);
	return 0;
}

int yft_finger_plat_remove(struct platform_device *pdev)
{
	yft_finger_plat = NULL;
	return 0;
}

static struct of_device_id yft_finger_match[] = {
	{ .compatible = "mediatek,yft_finger" },
	{ }
};
MODULE_DEVICE_TABLE(of, yft_finger_match);

static struct platform_driver yft_finger_pdrv = {
	.probe = yft_finger_plat_probe,
	.remove = yft_finger_plat_remove,
	.driver = {
		.name = "yft_finger",
		.owner = THIS_MODULE,
		.of_match_table = yft_finger_match,
	},
};

static int __init yft_finger_init(void)
{
	int ret;

	ret = platform_driver_register(&yft_finger_pdrv);
	if (ret) {
		printk("failed to register driver");
		return -ENODEV;
	}
	return 0;
}

static void __exit yft_finger_exit(void)
{
	platform_driver_unregister(&yft_finger_pdrv);
}

module_init(yft_finger_init);
module_exit(yft_finger_exit);

MODULE_AUTHOR("Jay_zhou");
MODULE_DESCRIPTION("for yft fingerprint driver");
MODULE_LICENSE("GPL");
