/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MicroArray Fprint Driver Code - mtk-settings.h
 * Based on MicroArray v4.0.06 (public: HelloVolla UT-kernel-volla-mt6768
 * 981578e37858, drivers/input/fingerprint/microarray_fp/mtk-settings.h),
 * reduced to what the GQ5012BF1 stock microarray_fp_tee.ko uses.
 */
#ifndef __MTK_SETTINGS_H_
#define __MTK_SETTINGS_H_

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include "madev.h"
#include <linux/spi/spi.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/power_supply.h>
#include <linux/pinctrl/consumer.h>
#include <linux/clk.h>

#define MA_DRV_NAME		"madev"
#define MA_DTS_NAME		"mediatek,madev_finger"
#define MA_EINT_DTS_NAME	"mediatek,yft_finger"

extern int mas_plat_probe(struct platform_device *pdev);
extern int mas_plat_remove(struct platform_device *pdev);
extern int mas_probe(struct spi_device *spi);
extern void mas_remove(struct spi_device *spi);

/* legacy MediaTek SPI controller_data (secure-world SPI proxy configuration) */
enum spi_sample_sel { POSEDGE, NEGEDGE };
enum spi_cs_pol { ACTIVE_LOW, ACTIVE_HIGH };
enum spi_cpol { SPI_CPOL_0, SPI_CPOL_1 };
enum spi_cpha { SPI_CPHA_0, SPI_CPHA_1 };
enum spi_mlsb { SPI_LSB, SPI_MSB };
enum spi_endian { SPI_LENDIAN, SPI_BENDIAN };
enum spi_transfer_mode { FIFO_TRANSFER, DMA_TRANSFER, OTHER1, OTHER2 };
enum spi_pause_mode { PAUSE_MODE_DISABLE, PAUSE_MODE_ENABLE };
enum spi_finish_intr { FINISH_INTR_DIS, FINISH_INTR_EN };
enum spi_deassert_mode { DEASSERT_DISABLE, DEASSERT_ENABLE };
enum spi_ulthigh { ULTRA_HIGH_DISABLE, ULTRA_HIGH_ENABLE };
enum spi_tckdly { TICK_DLY0, TICK_DLY1, TICK_DLY2, TICK_DLY3 };

struct mt_chip_conf {
	u32 setuptime;
	u32 holdtime;
	u32 high_time;
	u32 low_time;
	u32 cs_idletime;
	u32 ulthgh_thrsh;
	enum spi_sample_sel sample_sel;
	enum spi_cs_pol cs_pol;
	enum spi_cpol cpol;
	enum spi_cpha cpha;
	enum spi_mlsb tx_mlsb;
	enum spi_mlsb rx_mlsb;
	enum spi_endian tx_endian;
	enum spi_endian rx_endian;
	enum spi_transfer_mode com_mod;
	enum spi_pause_mode pause;
	enum spi_finish_intr finish_intr;
	enum spi_deassert_mode deassert;
	enum spi_ulthigh ulthigh;
	enum spi_tckdly tckdly;
};

#endif /* __MTK_SETTINGS_H_ */
