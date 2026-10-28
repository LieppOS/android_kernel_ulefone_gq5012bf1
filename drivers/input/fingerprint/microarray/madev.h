/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MicroArray Fprint Driver Code - madev.h
 * Based on MicroArray v4.0.06 (public: HelloVolla UT-kernel-volla-mt6768
 * 981578e37858, drivers/input/fingerprint/microarray_fp/madev.h), reduced to
 * what the GQ5012BF1 stock microarray_fp_tee.ko uses.
 */
#ifndef __MADEV_H_
#define __MADEV_H_

#define MTK

#define MALOGD_LEVEL	KERN_EMERG

#define MA_CHR_FILE_NAME	"madev0"
#define MA_CHR_DEV_NAME		"madev"
#define MA_EINT_NAME		"mediatek,finger_print-eint"

#define SPI_SPEED	(6*1000000)

#include <linux/poll.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/freezer.h>

#include <asm/ioctl.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
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
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/ioctl.h>
#include <linux/regulator/consumer.h>
#include <linux/power_supply.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_wakeup.h>
#include <linux/mm.h>
#include "ioctl_cmd.h"
#include "mtk-settings.h"

struct fprint_spi {
	u8 do_what;
	u8 f_wake;
	int value;
	volatile u8 f_irq;
	volatile u8 u1_flag;
	volatile u8 u2_flag;
	volatile u8 f_repo;
	spinlock_t spi_lock;
	struct spi_device *spi;
	struct list_head dev_entry;
	struct spi_message msg;
	struct spi_transfer xfer;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *workq;
	struct wakeup_source wl;
};

struct fprint_dev {
	dev_t idd;
	int major;
	int minor;
	struct cdev *chd;
	struct class *cls;
	struct device *dev;
};

/* mtk-settings.c */
extern void mas_select_transfer(struct spi_device *spi, int len);
extern void ma_spi_change(struct spi_device *spi, unsigned int speed, int flag);
extern int mas_get_platform(void);
extern int mas_remove_platform(void);
extern int mas_switch_power(unsigned int on_off);
extern int mas_finger_set_gpio_info(struct device *dev, int cmd);
extern void mas_enable_spi_clock(struct spi_device *spi);
extern void mas_disable_spi_clock(struct spi_device *spi);
extern unsigned int mas_get_irq(void);

/* fingerprint.ko (YFT glue) */
extern void yft_waite_for_finger_dts_paser(void);
extern int yft_finger_set_reset(int enable);
extern int yft_finger_set_spi_mode(int enable);
extern int yft_finger_set_irq(int enable);

/* spi-mt65xx.ko */
extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);

/* tkcore.ko (TrustKernel, public core/tee_fp.h ABI) */
extern int tee_spi_transfer(void *conf, int conf_size, const char *txbuf,
			    char *rxbuf, int len);

#define MALOG_TAG "MAFP_"
#define MALOGE(x) printk(KERN_ERR "%s%s: error log! the function %s is failed, ret = %d\n", MALOG_TAG, __func__, x, ret)
#define MALOGF(x) printk(MALOGD_LEVEL "%s%s: debug log! %s!\n", MALOG_TAG, __func__, x)
#define MALOGD(x) MALOGF(x)
#define MALOGW(x) printk(KERN_WARNING "%s%s: warning log! the function %s's ret = %d\n", MALOG_TAG, __func__, x, ret)

#endif /* __MADEV_H_ */
