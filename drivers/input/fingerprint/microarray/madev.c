// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) MicroArray
 * MicroArray Fprint Driver Code for TEE environment
 * madev.c
 * Version: v4.0.06 (MA_DRV_VERSION 0x4006)
 *
 * GQ5012BF1 (Ulefone Armor 29 Pro Thermal, MT6878) stock variant: the
 * MicroArray v4.0.06 madev.c (public donor: HelloVolla UT-kernel-volla-mt6768
 * 981578e37858) with the ODM changes the stock microarray_fp_tee.ko proves:
 * TrustKernel tee_spi_transfer() chip identification (three ID sequences),
 * YFT fingerprint.ko power/IRQ glue, no fb notifier, sysfs chip_id, a reduced
 * ioctl set and a 48 KiB transfer clamp.
 */

#include "madev.h"

static unsigned int irq, ret;
static unsigned int ma_drv_reg;
static unsigned int ma_speed;
static unsigned int is_screen_on;
static unsigned short chip_id;
static unsigned int MA_DRV_VERSION = 0x4006;
static DECLARE_WAIT_QUEUE_HEAD(gWaitq);
static DECLARE_WAIT_QUEUE_HEAD(U1_Waitq);
static DECLARE_WAIT_QUEUE_HEAD(U2_Waitq);
struct wakeup_source *gProcessWakeLock;

struct work_struct gWork;
struct workqueue_struct *gWorkq;

static DEFINE_MUTEX(dev_lock);
static DEFINE_MUTEX(ioctl_lock);

static struct fprint_dev *sdev;
static struct fprint_spi *smas;

static u8 *stxb;
static u8 *srxb;

#define IMAGE_DMA_SIZE	(48 * 1024)

static void mas_work(struct work_struct *pws)
{
	smas->f_irq = 1;
	wake_up(&gWaitq);
}

static irqreturn_t mas_interrupt(int irq, void *dev_id)
{
	queue_work(gWorkq, &gWork);
	return IRQ_HANDLED;
}

/*---------------------------------- fops ------------------------------------*/

int mas_sync(u8 *txb, u8 *rxb, int len)
{
	int ret = 0;

	mas_select_transfer(smas->spi, len);
	smas->xfer.tx_nbits = SPI_NBITS_SINGLE;
	smas->xfer.tx_buf = txb;
	smas->xfer.rx_nbits = SPI_NBITS_SINGLE;
	smas->xfer.rx_buf = rxb;
	smas->xfer.delay.value = 1;
	smas->xfer.delay.unit = SPI_DELAY_UNIT_USECS;
	smas->xfer.len = len;
	smas->xfer.bits_per_word = 8;
	smas->xfer.speed_hz = smas->spi->max_speed_hz;
	spi_message_init(&smas->msg);
	spi_message_add_tail(&smas->xfer, &smas->msg);
	yft_finger_set_spi_mode(1);
	mutex_lock(&dev_lock);
	ret = spi_sync(smas->spi, &smas->msg);
	mutex_unlock(&dev_lock);
	return ret;
}

static ssize_t mas_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	int val, ret = 0;

	if (count > IMAGE_DMA_SIZE) {
		MALOGW("data too long");
		return -1;
	}
	ret = mas_sync(stxb, srxb, count);
	if (ret) {
		MALOGW("mas_sync failed.");
		return -2;
	}
	ret = copy_to_user(buf, srxb, count);
	if (!ret) {
		val = count;
	} else {
		val = -3;
		MALOGW("copy_to_user failed.");
	}
	return val;
}

static void mas_set_input(void)
{
	struct input_dev *input = NULL;

	input = input_allocate_device();
	if (!input) {
		MALOGW("input_allocate_device failed.");
		return;
	}
	set_bit(EV_KEY, input->evbit);
	set_bit(249, input->keybit);
	set_bit(KEY_POWER, input->keybit);
	input->name = MA_CHR_DEV_NAME;
	input->id.bustype = BUS_SPI;
	ret = input_register_device(input);
	if (ret) {
		input_free_device(input);
		MALOGW("failed to register input device.");
		return;
	}
	smas->input = input;
}

static long mas_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	switch (cmd) {
	case TIMEOUT_WAKELOCK:
		__pm_wakeup_event(gProcessWakeLock, 5000);
		break;
	case SLEEP:
		smas->f_irq = 0;
		retval = wait_event_freezable(gWaitq, smas->f_irq != 0);
		break;
	case WAKEUP:
		smas->f_irq = 1;
		wake_up(&gWaitq);
		break;
	case ENABLE_CLK:
		break;
	case DISABLE_CLK:
		break;
	case ENABLE_INTERRUPT:
		enable_irq(irq);
		break;
	case DISABLE_INTERRUPT:
		disable_irq(irq);
		break;
	case TAP_DOWN:
		input_report_key(smas->input, 249, 1);
		input_sync(smas->input);
		break;
	case TAP_UP:
		input_report_key(smas->input, 249, 0);
		input_sync(smas->input);
		break;
	case SINGLE_TAP:
		input_report_key(smas->input, 249, 1);
		input_sync(smas->input);
		input_report_key(smas->input, 249, 0);
		input_sync(smas->input);
		break;
	case LONG_TAP:
		break;
	case SET_MODE:
		mutex_lock(&ioctl_lock);
		retval = copy_from_user(&ma_drv_reg, (unsigned int *)arg, sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case GET_MODE:
		mutex_lock(&ioctl_lock);
		retval = copy_to_user((unsigned int *)arg, &ma_drv_reg, sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case MA_IOC_GVER:
		mutex_lock(&ioctl_lock);
		retval = copy_to_user((unsigned int *)arg, &MA_DRV_VERSION, sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case SCREEN_ON:
		mas_switch_power(1);
		break;
	case SCREEN_OFF:
		mas_switch_power(0);
		break;
	case SET_SPI_SPEED:
		retval = copy_from_user(&ma_speed, (unsigned int *)arg, sizeof(unsigned int));
		ma_spi_change(smas->spi, ma_speed, 0);
		break;
	case WAIT_FACTORY_CMD:
		smas->u2_flag = 0;
		retval = wait_event_freezable(U2_Waitq, smas->u2_flag != 0);
		break;
	case WAKEUP_FINGERPRINTD:
		smas->u2_flag = 1;
		wake_up(&U2_Waitq);
		break;
	case WAIT_FINGERPRINTD_RESPONSE:
		smas->u1_flag = 0;
		wait_event_freezable(U1_Waitq, smas->u1_flag != 0);
		mutex_lock(&ioctl_lock);
		retval = copy_to_user((unsigned int *)arg, &ma_drv_reg, sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case WAKEUP_FACTORY_TEST_SEND_FINGERPRINTD_RESPONSE:
		mutex_lock(&ioctl_lock);
		retval = copy_from_user(&ma_drv_reg, (unsigned int *)arg, sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		msleep(4);
		smas->u1_flag = 1;
		wake_up(&U1_Waitq);
		break;
	case WAIT_SCREEN_STATUS_CHANGE:
		mutex_lock(&ioctl_lock);
		retval = copy_to_user((unsigned int *)arg, &is_screen_on, sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case SYNC:
		mas_switch_power(0);
		msleep(5);
		mas_switch_power(1);
		break;
	default:
		retval = -EINVAL;
		MALOGW("mas_ioctl no such cmd");
	}
	return retval;
}

static long mas_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	retval = filp->f_op->unlocked_ioctl(filp, cmd, arg);
	return retval;
}

static ssize_t mas_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int val = 0;

	if (count == 6) {
		u8 tmp[6] = { 0 };

		ret = copy_from_user(tmp, buf, count);
	} else {
		if (count > IMAGE_DMA_SIZE) {
			MALOGW("write data too long");
			return -1;
		}
		memset(stxb, 0, IMAGE_DMA_SIZE);
		ret = copy_from_user(stxb, buf, count);
		if (ret) {
			MALOGW("copy form user failed");
			val = -2;
		} else {
			val = count;
		}
	}
	return val;
}

void *kernel_memaddr = NULL;

int mas_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long page;

	if (!kernel_memaddr) {
		kernel_memaddr = kmalloc(68 * 1024, GFP_KERNEL);
		if (!kernel_memaddr)
			return -1;
	}
	page = virt_to_phys((void *)kernel_memaddr) >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (remap_pfn_range(vma, vma->vm_start, page, (vma->vm_end - vma->vm_start),
			    vma->vm_page_prot))
		return -1;
	return 0;
}

/*---------------------------------- fops ------------------------------------*/
static const struct file_operations sfops = {
	.owner = THIS_MODULE,
	.write = mas_write,
	.read = mas_read,
	.unlocked_ioctl = mas_ioctl,
	.mmap = mas_mmap,
	.compat_ioctl = mas_compat_ioctl,
};
/*---------------------------------- fops end ---------------------------------*/

static int init_file_node(void)
{
	int ret;

	ret = alloc_chrdev_region(&sdev->idd, 0, 1, MA_CHR_DEV_NAME);
	if (ret < 0) {
		MALOGW("alloc_chrdev_region error!");
		return ret;
	}

	sdev->chd = cdev_alloc();
	if (!sdev->chd) {
		MALOGW("cdev_alloc error!");
		ret = -ENOMEM;
		goto err_region;
	}
	sdev->chd->owner = THIS_MODULE;
	sdev->chd->ops = &sfops;

	ret = cdev_add(sdev->chd, sdev->idd, 1);
	if (ret) {
		MALOGE("cdev_add");
		goto err_cdev;
	}

	sdev->cls = class_create(THIS_MODULE, MA_CHR_DEV_NAME);
	if (IS_ERR(sdev->cls)) {
		ret = PTR_ERR(sdev->cls);
		sdev->cls = NULL;
		MALOGE("class_create");
		goto err_cdev;
	}

	sdev->dev = device_create(sdev->cls, NULL, sdev->idd, NULL, MA_CHR_FILE_NAME);
	if (IS_ERR(sdev->dev)) {
		ret = PTR_ERR(sdev->dev);
		sdev->dev = NULL;
		MALOGE("device_create");
		goto err_class;
	}

	return 0;

err_class:
	class_destroy(sdev->cls);
	sdev->cls = NULL;
err_cdev:
	cdev_del(sdev->chd);
	sdev->chd = NULL;
err_region:
	unregister_chrdev_region(sdev->idd, 1);
	sdev->idd = 0;
	MALOGE("init_file_node");
	return ret;
}

static int deinit_file_node(void)
{
	if (!sdev)
		return 0;

	if (sdev->dev) {
		device_destroy(sdev->cls, sdev->idd);
		sdev->dev = NULL;
	}
	if (sdev->cls) {
		class_destroy(sdev->cls);
		sdev->cls = NULL;
	}
	if (sdev->chd) {
		cdev_del(sdev->chd);
		sdev->chd = NULL;
	}
	if (sdev->idd) {
		unregister_chrdev_region(sdev->idd, 1);
		sdev->idd = 0;
	}
	return 0;
}

static int init_interrupt(void)
{
	const char *tname = MA_EINT_NAME;

	irq = mas_get_irq();
	if (irq <= 0) {
		MALOGE("mas_get_irq");
		return irq ? (int)irq : -ENODEV;
	}

	ret = request_irq(irq, mas_interrupt, IRQF_TRIGGER_RISING, tname, NULL);
	if (ret) {
		MALOGE("request_irq");
		irq = 0;
		return ret;
	}

	ret = enable_irq_wake(irq);
	if (ret)
		MALOGE("enable_irq_wake");

	return 0;
}

static int deinit_interrupt(void)
{
	if (!irq)
		return 0;

	free_irq(irq, NULL);
	irq = 0;
	return 0;
}

static int init_vars(void)
{
	sdev = kzalloc(sizeof(struct fprint_dev), GFP_KERNEL);
	smas = kzalloc(sizeof(struct fprint_spi), GFP_KERNEL);
	srxb = kmalloc(IMAGE_DMA_SIZE, GFP_KERNEL);
	stxb = kmalloc(IMAGE_DMA_SIZE, GFP_KERNEL);

	if (!sdev || !smas || !srxb || !stxb) {
		MALOGW("smas kmalloc failed.");
		kfree(sdev);
		kfree(smas);
		kfree(stxb);
		kfree(srxb);
		sdev = NULL;
		smas = NULL;
		stxb = NULL;
		srxb = NULL;
		return -ENOMEM;
	}

	gProcessWakeLock = wakeup_source_register(NULL, "microarray_process_wakelock");

	INIT_WORK(&gWork, mas_work);
	gWorkq = create_singlethread_workqueue("mas_workqueue");
	if (!gWorkq) {
		MALOGW("create_single_workqueue error!");
		return -ENOMEM;
	}
	return 0;
}

static int deinit_vars(void)
{
	if (gWorkq) {
		destroy_workqueue(gWorkq);
		gWorkq = NULL;
	}
	if (gProcessWakeLock) {
		wakeup_source_unregister(gProcessWakeLock);
		gProcessWakeLock = NULL;
	}

	kfree(sdev);
	kfree(smas);
	kfree(stxb);
	kfree(srxb);
	sdev = NULL;
	smas = NULL;
	stxb = NULL;
	srxb = NULL;
	return 0;
}

static int init_spi(struct spi_device *spi)
{
	smas->spi = spi;
	smas->spi->max_speed_hz = SPI_SPEED;
	spi_setup(spi);
	INIT_LIST_HEAD(&smas->dev_entry);
	return 0;
}

static int deinit_spi(struct spi_device *spi)
{
	smas->spi = NULL;
	return 0;
}

/*
 * init_connect - identify the sensor through the TrustKernel SPI proxy.
 * smt_conf is the legacy MediaTek mt_chip_conf handed to the secure world.
 */
int init_connect(void)
{
	static struct mt_chip_conf smt_conf = {
		.setuptime = 10,
		.holdtime = 10,
		.high_time = 10,
		.low_time = 10,
		.cs_idletime = 10,
		.ulthgh_thrsh = 0,
		.sample_sel = POSEDGE,
		.cs_pol = ACTIVE_LOW,
		.cpol = SPI_CPOL_0,
		.cpha = SPI_CPHA_0,
		.tx_mlsb = SPI_MSB,
		.rx_mlsb = SPI_MSB,
		.tx_endian = SPI_LENDIAN,
		.rx_endian = SPI_LENDIAN,
		.com_mod = FIFO_TRANSFER,
		.pause = PAUSE_MODE_DISABLE,
		.finish_intr = FINISH_INTR_EN,
		.deassert = DEASSERT_DISABLE,
		.ulthigh = ULTRA_HIGH_DISABLE,
		.tckdly = TICK_DLY0,
	};
	u8 tx_reset[4] = { 0x8c, 0xff, 0xff, 0xff };
	u8 tx_rdid[4] = { 0x00, 0xff, 0xff, 0xff };
	u8 tx_a[5] = { 0x70, 0x00, 0x00, 0x00, 0x00 };
	u8 tx_b[5] = { 0x90, 0x08, 0x00, 0x00, 0x00 };
	u8 tx_c[5] = { 0x90, 0x0b, 0x00, 0x00, 0x00 };
	u8 outid[4] = { 0 };
	u8 inid[5] = { 0 };

	if (tee_spi_transfer(&smt_conf, sizeof(struct mt_chip_conf), tx_reset, tx_reset, 4)) {
		printk("TEE RESET SPI transfer failed\n");
		return -1;
	}
	msleep(100);
	if (tee_spi_transfer(&smt_conf, sizeof(struct mt_chip_conf), tx_rdid, outid, 4))
		goto xfer_failed;
	printk("[MAFP] outid[3]=[0x%x, 0x%x,0x%x,0x%x]\n", outid[0], outid[1], outid[2], outid[3]);
	if ((outid[3] & ~0x04) == 0x41) {
		printk("[MAFP] chip is ok1\n");
		chip_id = outid[3];
		return chip_id;
	}
	if (tee_spi_transfer(&smt_conf, sizeof(struct mt_chip_conf), tx_a, inid, 5))
		goto xfer_failed;
	msleep(3);
	if (tee_spi_transfer(&smt_conf, sizeof(struct mt_chip_conf), tx_b, inid, 5))
		goto xfer_failed;
	if (inid[3] == 0x20 || inid[3] == 0x1a) {
		printk("[MAFP] chip is ok2\n");
		chip_id = inid[3];
		return chip_id;
	}
	if (tee_spi_transfer(&smt_conf, sizeof(struct mt_chip_conf), tx_c, inid, 5))
		goto xfer_failed;
	if (inid[3] == 0x4a) {
		printk("[MAFP] chip is ok3\n");
		chip_id = inid[3];
		return chip_id;
	}
	printk("[%s], chip_id is 0x%x\n", __func__, chip_id);
	return chip_id;

xfer_failed:
	printk("TEE SPI transfer failed\n");
	return -1;
}

int deinit_connect(void)
{
	return 0;
}

/* stock: the fb notifier registration is gone, only the screen state init remains */
static int init_notifier_call(void)
{
	is_screen_on = 1;
	return 0;
}

int mas_plat_probe(struct platform_device *pdev)
{
	ret = mas_finger_set_gpio_info(&pdev->dev, 1);
	if (ret)
		MALOGE("mas_plat_probe do mas_finger_set_gpio_info");
	return ret;
}

int mas_plat_remove(struct platform_device *pdev)
{
	mas_finger_set_gpio_info(&pdev->dev, 0);
	return 0;
}

static ssize_t chip_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	MALOGD("start");
	return snprintf(buf, PAGE_SIZE, "chip_id:0x%04x\n", chip_id);
}
static DEVICE_ATTR_RO(chip_id);

static struct attribute *madev_attrs[] = {
	&dev_attr_chip_id.attr,
	NULL,
};

static struct attribute_group madev_attrs_group = {
	.attrs = madev_attrs,
};

int mas_probe(struct spi_device *spi)
{
	MALOGD("start");

	spi->mode = SPI_MODE_0;
	yft_waite_for_finger_dts_paser();

	ret = mas_finger_set_gpio_info(&spi->dev, 1);
	if (ret) {
		MALOGE("mas_probe do mas_finger_set_gpio_info");
		return ret;
	}

	ret = init_vars();
	if (ret)
		goto err_gpio;

	ret = init_interrupt();
	if (ret)
		goto err_vars;

	ret = init_spi(spi);
	if (ret)
		goto err_irq;
	init_connect();

	ret = init_file_node();
	if (ret)
		goto err_spi;

	ret = sysfs_create_group(&spi->dev.kobj, &madev_attrs_group);
	if (ret) {
		MALOGE("sysfs_create_group");
		goto err_file;
	}

	mas_set_input();
	ret = init_notifier_call();
	if (ret) {
		ret = -ENODEV;
		goto err_sysfs;
	}

	MALOGD("end");
	return 0;

err_sysfs:
	sysfs_remove_group(&spi->dev.kobj, &madev_attrs_group);
err_file:
	deinit_file_node();
err_spi:
	deinit_spi(spi);
err_irq:
	deinit_interrupt();
err_vars:
	deinit_vars();
err_gpio:
	mas_finger_set_gpio_info(&spi->dev, 0);
	return ret;
}

void mas_remove(struct spi_device *spi)
{
	sysfs_remove_group(&spi->dev.kobj, &madev_attrs_group);
	deinit_file_node();
	deinit_interrupt();
	deinit_vars();
	mas_finger_set_gpio_info(&spi->dev, 0);
}

static int __init mas_init(void)
{
	int ret = 0;

	MALOGF("start");
	ret = mas_get_platform();
	if (ret)
		MALOGE("mas_get_platform");
	return ret;
}

static void __exit mas_exit(void)
{
}

module_init(mas_init);
module_exit(mas_exit);

MODULE_AUTHOR("Microarray");
MODULE_DESCRIPTION("Driver for microarray fingerprint sensor");
MODULE_LICENSE("GPL");
