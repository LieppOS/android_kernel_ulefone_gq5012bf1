// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) MicroArray
 * MicroArray Fprint Driver Code
 * mtk-settings.c
 * Version: v4.0.06 (MA_DRV_VERSION 0x4006)
 *
 * GQ5012BF1 (Ulefone Armor 29 Pro Thermal, MT6878) stock variant: the
 * MicroArray v4.0.06 settings file (public donor: HelloVolla
 * UT-kernel-volla-mt6768 981578e37858) with the ODM changes the stock
 * microarray_fp_tee.ko proves: pinctrl power/reset replaced by the "vfp"
 * regulator and the YFT fingerprint.ko glue, the platform driver and board
 * info removed, IRQ from the "mediatek,yft_finger" node, and the SPI
 * mode/speed helpers emptied (the TrustKernel SPI proxy owns the bus setup).
 */

#include "mtk-settings.h"

static int ret;
static struct regulator *fp_mt6373_ldo;
static bool fp_mt6373_enabled;
struct device_node *node;

struct spi_device_id sdev_id[] = {
	{ MA_DRV_NAME, 0 },
	{ }
};

static const struct of_device_id madev_dt_ids[] = {
	{ .compatible = MA_DTS_NAME, },
	{},
};
MODULE_DEVICE_TABLE(of, madev_dt_ids);

struct spi_driver sdrv = {
	.driver = {
		.name = MA_DRV_NAME,
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
		.of_match_table = madev_dt_ids,
	},
	.probe = mas_probe,
	.remove = mas_remove,
	.id_table = sdev_id,
};

void mas_select_transfer(struct spi_device *spi, int len)
{
}

void ma_spi_change(struct spi_device *spi, unsigned int speed, int flag)
{
}

int __init mas_get_platform(void)
{
	ret = spi_register_driver(&sdrv);
	if (ret)
		MALOGE("spi_register_driver");
	return ret;
}

int mas_remove_platform(void)
{
	spi_unregister_driver(&sdrv);
	return 0;
}

int mas_switch_power(unsigned int on_off)
{
	ret = 0;
	printk(KERN_ERR "yftwang %s: regulator_enable enable mt6377_ldo start!!\n", __func__);
	if (on_off) {
		ret = regulator_enable(fp_mt6373_ldo);
		if (ret < 0) {
			printk(KERN_ERR "yftwang %s: regulator_enable enable mt6377_ldo fail!!\n", __func__);
			return -1;
		}
	} else {
		ret = regulator_disable(fp_mt6373_ldo);
		if (ret < 0) {
			printk(KERN_ERR "yftwang %s: regulator_disable disable mt6377_ldo fail!!\n", __func__);
			return -1;
		}
	}
	return 0;
}

int mas_finger_set_gpio_info(struct device *dev, int cmd)
{
	ret = 0;

	if (!cmd) {
		if (!fp_mt6373_ldo)
			return 0;

		if (fp_mt6373_enabled) {
			ret = regulator_disable(fp_mt6373_ldo);
			if (ret < 0)
				printk(KERN_ERR "yftwang %s: regulator_disable mt6377_ldo fail=%d!!\n",
				       __func__, ret);
			fp_mt6373_enabled = false;
		}
		regulator_put(fp_mt6373_ldo);
		fp_mt6373_ldo = NULL;
		yft_finger_set_reset(0);
		yft_finger_set_spi_mode(0);
		yft_finger_set_irq(0);
		return ret;
	}

	if (!fp_mt6373_ldo) {
		fp_mt6373_ldo = regulator_get(dev, "vfp");
		if (IS_ERR(fp_mt6373_ldo)) {
			ret = PTR_ERR(fp_mt6373_ldo);
			fp_mt6373_ldo = NULL;
			printk(KERN_ERR "yftwang %s: regulator_get mt6377_ldo fail=%d!!\n",
			       __func__, ret);
			return ret;
		}

		ret = regulator_set_voltage(fp_mt6373_ldo, 2800000, 2800000);
		if (ret < 0) {
			printk(KERN_ERR "yftwang %s: regulator_set_voltage mt6377_ldo fail!!\n",
			       __func__);
			regulator_put(fp_mt6373_ldo);
			fp_mt6373_ldo = NULL;
			return ret;
		}
	}

	if (!fp_mt6373_enabled) {
		ret = regulator_enable(fp_mt6373_ldo);
		if (ret < 0) {
			printk(KERN_ERR "yftwang %s: regulator_enable mt6377_ldo fail=%d!!\n",
			       __func__, ret);
			regulator_put(fp_mt6373_ldo);
			fp_mt6373_ldo = NULL;
			return ret;
		}
		fp_mt6373_enabled = true;
	}

	yft_finger_set_reset(1);
	yft_finger_set_spi_mode(1);
	yft_finger_set_irq(1);
	return 0;
}

void mas_enable_spi_clock(struct spi_device *spi)
{
	mt_spi_enable_master_clk(spi);
}

void mas_disable_spi_clock(struct spi_device *spi)
{
	mt_spi_disable_master_clk(spi);
}

unsigned int mas_get_irq(void)
{
	struct device_node *np;
	unsigned int irq;

	np = of_find_compatible_node(NULL, NULL, MA_EINT_DTS_NAME);
	if (!np)
		return 0;

	irq = irq_of_parse_and_map(np, 0);
	of_node_put(np);

	return irq;
}
