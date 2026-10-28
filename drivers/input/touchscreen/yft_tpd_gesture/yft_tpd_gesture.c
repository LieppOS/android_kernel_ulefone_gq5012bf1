// SPDX-License-Identifier: GPL-2.0
/*
 * YFT touch gesturewake driver
 *
 * Reconstructed for LieppOS from the Ulefone GQ5012BF1 stock
 * yft_tpd_gesture.ko oracle
 * (SHA256 e7d4e7cfe4defdd69ff0e1be50c7a3de173b5c717e47e969cf0f9dec13baf9bc).
 *
 * Provides the gesture-wakeup state/API imported by
 * focaltech_touch_spi_ft3680.ko:
 *
 *      tpgesture_status   EXPORT_SYMBOL_GPL
 *      tpgesture_value    EXPORT_SYMBOL_GPL
 *      tpgesture_hander   EXPORT_SYMBOL
 *
 * Declaration/definition ORDER in this file is load-bearing: it reproduces the
 * stock LLVM global/function emission order (see the RED document). Do not
 * reorder without re-running tools/yft-compare.sh.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

#define TOUCH_IOC_MAGIC		'A'

#define TPD_GET_GESTURE		_IOW(TOUCH_IOC_MAGIC, 3, int)
#define TPD_GET_GESTURE_VALUE	_IOR(TOUCH_IOC_MAGIC, 4, char *)

#define TPD_DEVICE		"touch"
#define TPD_DRIVER_NAME		"mtk_yft_tpd"

static int tpd_misc_open(struct inode *inode, struct file *file);
static int tpd_misc_release(struct inode *inode, struct file *file);
static long tpd_unlocked_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg);
static int tpd_probe(struct platform_device *pdev);

/* .data + 0x00 */
static DECLARE_WAIT_QUEUE_HEAD(g_Waitq);

/* .bss + 0x00 */
char tpgesture_status;
__ADDRESSABLE(tpgesture_status);

/* .data..read_mostly + 0x00 */
char tpgesture_value[10] __read_mostly;

/*
 * .bss + 0x04.
 * The stock object sits at .bss+0x4 (not +0x1), i.e. it carries 4-byte
 * alignment; tpgesture_status_value at .bss+0x8 gets 4-byte alignment for
 * free from the 4-byte "off" store.  __aligned(4) reproduces the stock
 * .bss layout exactly (13 bytes: 0x0 / 0x4 / 0x8).
 */
static bool flag_irq __aligned(4);

/* .bss + 0x08 */
static char tpgesture_status_value[5];

void tpgesture_hander(void)
{
	pr_err("%s:[yft-drv] Start.\n", __func__);

	flag_irq = true;
	wake_up(&g_Waitq);

	pr_err("%s:[yft-drv] End.\n", __func__);
}
EXPORT_SYMBOL(tpgesture_hander);
EXPORT_SYMBOL_GPL(tpgesture_status);
EXPORT_SYMBOL_GPL(tpgesture_value);

/* forward reference only; the definition follows the attribute definitions */
static const struct file_operations tpd_fops;

/* .data + 0x18 */
struct miscdevice tpd_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = TPD_DEVICE,
	.fops = &tpd_fops,
};

static ssize_t tpgesture_show(struct device_driver *ddri, char *buf);
static ssize_t tpgesture_status_show(struct device_driver *ddri, char *buf);
static ssize_t tpgesture_status_store(struct device_driver *ddri,
				      const char *buf, size_t count);

/* forward references only; the definitions follow the two helpers */
static struct driver_attribute driver_attr_tpgesture;
static struct driver_attribute driver_attr_tpgesture_status;

int gesture_create_attr(struct device_driver *dev)
{
	int err = 0;

	printk("%s: is start !!\n", __func__);

	if (dev == NULL)
		return -EINVAL;

	err = driver_create_file(dev, &driver_attr_tpgesture);
	if (err) {
		printk("driver_create_file (%s) = %d\n",
		       driver_attr_tpgesture.attr.name, err);
		return err;
	}

	err = driver_create_file(dev, &driver_attr_tpgesture_status);
	if (err) {
		printk("driver_create_file (%s) = %d\n",
		       driver_attr_tpgesture_status.attr.name, err);
		return err;
	}

	return err;
}

int gesture_delete_attr(struct device_driver *dev)
{
	if (dev == NULL)
		return -EINVAL;

	driver_remove_file(dev, &driver_attr_tpgesture);
	driver_remove_file(dev, &driver_attr_tpgesture_status);

	return 0;
}

/* .data + 0x68 / + 0x88 */
static DRIVER_ATTR_RO(tpgesture);
static DRIVER_ATTR_RW(tpgesture_status);

/* .rodata + 0x000 */
static const struct file_operations tpd_fops = {
	.unlocked_ioctl = tpd_unlocked_ioctl,
	.open = tpd_misc_open,
	.release = tpd_misc_release,
};

/* .rodata + 0x110 */
const struct of_device_id touch_of_match[] = {
	{ .compatible = "mediatek,yft_tpd", },
	{},
};

/* .data + 0xa8 */
static struct platform_driver tpd_driver = {
	.probe = tpd_probe,
	.driver = {
		.name = TPD_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = touch_of_match,
	},
};

static ssize_t tpgesture_show(struct device_driver *ddri, char *buf)
{
	printk("show tp gesture value is %s \n", tpgesture_value);

	return scnprintf(buf, PAGE_SIZE, "%s\n", tpgesture_value);
}

static ssize_t tpgesture_status_show(struct device_driver *ddri, char *buf)
{
	printk("show tp gesture status is %d\n", tpgesture_status);

	return scnprintf(buf, PAGE_SIZE, "%d\n", tpgesture_status);
}

static ssize_t tpgesture_status_store(struct device_driver *ddri,
				      const char *buf, size_t count)
{
	if (strncmp(buf, "on", 2) == 0) {
		strcpy(tpgesture_status_value, "on");
		tpgesture_status = 1;
	} else {
		strcpy(tpgesture_status_value, "off");
		tpgesture_status = 0;
	}

	printk("store_tpgesture_status_value status is %s \n",
	       tpgesture_status_value);

	return count;
}

static long tpd_unlocked_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	long err = 0;
	int ioarg = 0;
	char gesture_value[10] = {0};

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));

	if (err) {
		pr_info("tpd: access error: %08X, (%2d, %2d)\n",
			cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch (cmd) {
	case TPD_GET_GESTURE:
		pr_err("%s:[yft-drv] Gesture Enable Start. \n", __func__);
		printk("%s:[yft-drv] Gesture Enable Start. \n", __func__);

		err = copy_from_user(&ioarg, (void __user *)arg, sizeof(ioarg));
		pr_err("[yft-drv] ioarg=%d", ioarg);

		tpgesture_status = ioarg;
		pr_err("[yft-drv] tpgesture_status=%d", tpgesture_status);

		pr_err("%s:[yft-drv] Gesture Enable End. \n", __func__);
		break;

	case TPD_GET_GESTURE_VALUE:
		pr_err("%s:[yft-drv] Gesture Value Start. \n", __func__);

		flag_irq = false;
		wait_event_freezable(g_Waitq, flag_irq);

		pr_err("[yft-drv] Before assign Gesture Value=%s. \n",
		       tpgesture_value);
		strcpy(gesture_value, tpgesture_value);
		pr_err("[yft-drv] After assign Gesture Value=%s. \n",
		       gesture_value);

		err = copy_to_user((void __user *)arg, gesture_value,
				   sizeof(gesture_value));

		pr_err("%s:[yft-drv] Gesture Value End. \n", __func__);
		break;

	default:
		pr_info("tpd: unknown IOCTL: 0x%08x\n", cmd);
		err = -ENOIOCTLCMD;
		break;
	}

	return err;
}

static int tpd_misc_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int tpd_misc_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int tpd_probe(struct platform_device *pdev)
{
	if (misc_register(&tpd_misc_device)) {
		pr_info("mtk_tpd: tpd_misc_device register failed\n");
		return -1;
	}

	return 0;
}

static int __init tpd_device_init(void)
{
	printk("MediaTek touch panel driver init\n");

	if (platform_driver_register(&tpd_driver))
		printk("unable to register touch panel driver.\n");

	if (gesture_create_attr(&tpd_driver.driver))
		printk("gesture_create_attr fail!\n");

	return 0;
}

static void __exit tpd_device_exit(void)
{
	gesture_delete_attr(&tpd_driver.driver);

	printk("MediaTek touch panel driver exit\n");

	misc_deregister(&tpd_misc_device);
	platform_driver_unregister(&tpd_driver);
}

module_init(tpd_device_init);
module_exit(tpd_device_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("YFT touch gesturewake driver");
MODULE_AUTHOR("<@yft.hk>");
