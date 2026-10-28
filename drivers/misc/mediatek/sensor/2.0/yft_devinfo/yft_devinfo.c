// SPDX-License-Identifier: GPL-2.0
/*
 * LieppOS reconstruction of the Ulefone GQ5012BF1 stock yft_devinfo.ko
 * ("YFT DEVICE INFO", author "Jay Zhou").
 *
 * No public or local source for this module exists; the implementation below
 * was recovered by full reverse engineering of the stock vendor_dlkm binary
 *   sha256 0d5e547e3e6c313c88695b2c8f9aae04398e3c8822f16d57dff6aea011f821fe
 * (see workspace/phase4-yft-devinfo/RED.md).
 *
 * The module is the shared Ulefone/YFT device-information provider consumed by
 * focaltech_touch_spi_ft3680.ko, hynitron.ko, hf_manager.ko, imgsensor.ko,
 * sh366003_fg.ko, spi_tiny_co5300_lcd.ko and aw36518.ko.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>

#include "hf_sensor_io.h"

/* ------------------------------------------------------------------ */
/* device type ids                                                     */
/* ------------------------------------------------------------------ */
#define YFT_CAMERA		12
#define YFT_TOUCHPANEL		13
#define YFT_ACCSENSOR		14
#define YFT_MSENSOR		15
#define YFT_ALSPSSENSOR		16
#define YFT_SARSENSOR		17
#define YFT_BAROSENSOR		18
#define YFT_FUELGAUGE		19
#define YFT_TINYLCD		20

#define YFT_NAME_LEN		20
#define YFT_CAM_NAME_LEN	48
#define YFT_SENSOR_NAME_LEN	16

struct yft_devices_data {
	struct list_head list;
	struct list_head info_list;
	struct mutex de_mutex;
};

struct yft_device_type_info {
	struct list_head list;
	struct list_head info_list;
	int (*info_print)(struct seq_file *m);
	int (*set_used)(char *name, int used);
	int type;
	char *used_name;
	struct mutex info_mutex;
};

struct yft_device_info {
	struct list_head list;
	char name[YFT_NAME_LEN];
	struct list_head node;
	void *pdrv;
	struct i2c_driver *i2c_drv;
	struct spi_driver *spi_drv;
	int used;
};

struct yft_touchpanel_spi_info {
	struct list_head list;
	char name[YFT_NAME_LEN];
	struct list_head node;
	void *pdrv;
	struct spi_driver *spi_drv;
	int used;
};

struct yft_camera_dev_info {
	struct list_head list;
	char name[YFT_CAM_NAME_LEN];
	struct list_head node;
	void *pdrv;
	char *cam_name;
	int used;
	int camera_id;
};

struct yft_sensor_dev_info {
	struct list_head list;
	char name[YFT_SENSOR_NAME_LEN];
	void *pdrv;
	struct list_head node;
	void *reserved;
	int used;
	struct sensor_info *info;
};

struct yftdisp_info {
	struct list_head list;
	char *info;
};

/* ------------------------------------------------------------------ */
/* exported firmware-version strings                                   */
/* ------------------------------------------------------------------ */
char touch_fw_version[30] __read_mostly;
EXPORT_SYMBOL_GPL(touch_fw_version);
char second_touch_fw_version[30] __read_mostly;
EXPORT_SYMBOL_GPL(second_touch_fw_version);
char fuelgauge_fw_version[30] __read_mostly;
EXPORT_SYMBOL_GPL(fuelgauge_fw_version);
char tinylcd_fw_version[30] __read_mostly;
EXPORT_SYMBOL_GPL(tinylcd_fw_version);

int yft_urxdo_flag;
static int yft_urxdo_gpio = -1;
struct yft_devices_data *yft_devices;
struct list_head yftdisp_info_list;
int yftdebug;

char Cust_emmc_support[9][50] = {
	"H58G66BK8HX096",
	"H58G76BK8HX095",
	"H58GG6AK8HX094",
	"MT62F2G64D8ZA-023",
	"K3KL4L40EM_BGCU",
	"MT62F1536M64D8CL",
	"H58GJ6BK8HX151",
	"K3KL5L50CM_BGCW",
	"K3KLELE0DM_BGCU",
};

char CUSTOM_MODEM[] =
	"m190_tk_md-nlwcg_fr1_6989-c2k5m_gsm4p_w12456819_fdd12345781213171819202526283266"
	"71_tdd3438394041_c2kbc0110_cotsx_qorvo_cta";

static int yft_camera_info_print(struct seq_file *m);
static int yft_camera_set_used(char *name, int used);
static int yft_touchpanel_info_print(struct seq_file *m);
static int yft_touchpanel_set_used(char *name, int used);
static int yft_accsensor_info_print(struct seq_file *m);
static int yft_accsensor_set_used(char *name, int used);
static int yft_msensor_info_print(struct seq_file *m);
static int yft_msensor_set_used(char *name, int used);
static int yft_alspssensor_info_print(struct seq_file *m);
static int yft_alspssensor_set_used(char *name, int used);
static int yft_sarsensor_info_print(struct seq_file *m);
static int yft_sarsensor_set_used(char *name, int used);
static int yft_barosensor_info_print(struct seq_file *m);
static int yft_barosensor_set_used(char *name, int used);
static int yft_fuelgauge_info_print(struct seq_file *m);
static int yft_fuelgauge_set_used(char *name, int used);
static int yft_tinylcd_info_print(struct seq_file *m);
static int yft_tinylcd_set_used(char *name, int used);

static char yft_sar_name_buf[YFT_NAME_LEN];

/* ------------------------------------------------------------------ */
/* urxdo (CTS board strap) gpio                                        */
/* ------------------------------------------------------------------ */
static int yft_parse_urxdo_dts(struct device_node *node)
{
	int urxdo_gpio;
	int ret;

	if (yft_urxdo_gpio >= 0)
		return 0;

	urxdo_gpio = of_get_named_gpio(node, "urxdo_gpio_select", 0);
	if (urxdo_gpio < 0) {
		printk("%s: of_get_named_gpio fail. \n", __func__);
		return -1;
	}
	printk("%s: of_get_named_gpio GPIO is %d.\n", __func__, urxdo_gpio);

	if (gpio_to_desc(urxdo_gpio) == NULL) {
		printk("%s: gpio_desc is null.\n", __func__);
		return -1;
	}
	printk("%s: gpio_desc is not null.\n", __func__);

	if (gpio_is_valid(urxdo_gpio))
		printk("%s: gpio number %d is valid. \n", __func__, urxdo_gpio);

	ret = gpio_request(urxdo_gpio, "urxdo_gpio_select");
	if (ret) {
		printk("%s: gpio_request fail. \n", __func__);
		return -1;
	}

	ret = gpiod_direction_input(gpio_to_desc(urxdo_gpio));
	if (ret) {
		printk("%s: gpio_direction_output failed. \n", __func__);
		gpio_free(urxdo_gpio);
		return -1;
	}

	yft_urxdo_gpio = urxdo_gpio;

	if (gpiod_get_raw_value(gpio_to_desc(urxdo_gpio)) == 0) {
		yft_urxdo_flag = 1;
		printk("urxdo_1 %s: urxdo_gpio is <%d> status is <%d> flag = %d!\n",
		       __func__, urxdo_gpio,
		       gpiod_get_raw_value(gpio_to_desc(urxdo_gpio)),
		       yft_urxdo_flag);
	} else {
		yft_urxdo_flag = 0;
		printk("urxdo_2 %s: urxdo_gpio is <%d> status is <%d> flag = %d!\n",
		       __func__, urxdo_gpio,
		       gpiod_get_raw_value(gpio_to_desc(urxdo_gpio)),
		       yft_urxdo_flag);
	}
	return 0;
}

int get_urxdo_select_gpio(void)
{
	struct device_node *node = NULL;
	int ret;

	printk("%s: enter. \n", __func__);

	node = of_find_compatible_node(NULL, NULL, "mediatek,yft_devices");
	if (node == NULL) {
		printk("%s: cannot get the node: 'mediatek,yft_devices'.\n", __func__);
		return -19;
	}

	ret = yft_parse_urxdo_dts(node);
	if (ret) {
		printk("%s: yft_parse_dts fail. \n", __func__);
		return -1;
	}

	printk("%s: end. \n", __func__);
	return 0;
}

/* ------------------------------------------------------------------ */
/* device type registry helpers                                        */
/* ------------------------------------------------------------------ */
static struct yft_device_type_info *find_device_type_info(int type)
{
	struct yft_device_type_info *pdevice;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type == type)
			return pdevice;
	}
	return NULL;
}

/*
 * Shared type-registry constructor.  The stock binary has no standalone
 * symbol for this helper but does carry a single lockdep key named
 * "init_device_type_info.__key" in .bss which every *_device_add()
 * mutex_init() site references -- i.e. the helper existed in the vendor
 * source and was inlined into all of its callers.
 */
static inline struct yft_device_type_info *init_device_type_info(int type,
		char *used_name, int used,
		int (*info_print)(struct seq_file *m),
		int (*set_used)(char *name, int used))
{
	struct yft_device_type_info *pdevice;

	pdevice = kmalloc(sizeof(struct yft_device_type_info), GFP_KERNEL);
	if (!pdevice)
		return NULL;
	/* Stock zeroes only the tail (0x20..0x6f): the two list heads are
	 * initialised by INIT_LIST_HEAD() immediately afterwards (volatile
	 * WRITE_ONCE() stores, hence unpaired in the stock codegen).
	 */
	memset((char *)pdevice + offsetof(struct yft_device_type_info, info_print), 0,
	       sizeof(struct yft_device_type_info) -
	       offsetof(struct yft_device_type_info, info_print));
	INIT_LIST_HEAD(&pdevice->list);
	INIT_LIST_HEAD(&pdevice->info_list);
	pdevice->type = type;
	if (used_name && used)
		pdevice->used_name = used_name;
	pdevice->info_print = info_print;
	pdevice->set_used = set_used;
	mutex_init(&pdevice->info_mutex);
	list_add(&pdevice->list, &yft_devices->list);
	return pdevice;
}

/* ------------------------------------------------------------------ */
/* camera                                                              */
/* ------------------------------------------------------------------ */
int yft_camera_device_add(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_camera_dev_info *info;
	int ret = 0;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_CAMERA)
			continue;
		if (used && pdevice->used_name) {
			printk("~~~~add camera error , duplicated current used lcm \n");
		}
		goto found;
	}

		pdevice = init_device_type_info(YFT_CAMERA, name, used,
						yft_camera_info_print,
						yft_camera_set_used);
		if (!pdevice) {
			printk("cam_alloc type info failed ~~~~ \n");
			ret = -1;
			goto err;
		}
	
found:

	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			list_del(&info->list);
			list_del(&info->node);
			kfree(info);
			ret = 0;
			goto err;
		}
	}

	info = kmalloc(sizeof(struct yft_camera_dev_info), GFP_KERNEL);
	if (!info) {
		printk("cam_alloc type info failed ~~~~ \n");
		ret = -2;
		goto err;
	}
	memset(info, 0, sizeof(struct yft_camera_dev_info));
	INIT_LIST_HEAD(&info->list);
	INIT_LIST_HEAD(&info->node);
	strcpy(info->name, name);
	info->cam_name = name;
	info->used = used;
	list_add(&info->list, &yft_devices->info_list);
	list_add(&info->node, &pdevice->info_list);
	return 0;

err:
	printk("%s: error return: %x: ---\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(yft_camera_device_add);

/* ------------------------------------------------------------------ */
/* touch panel (i2c)                                                   */
/* ------------------------------------------------------------------ */
int yft_touchpanel_device_add(struct i2c_driver *driver, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;
	int ret = 0;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_TOUCHPANEL)
			continue;
		if (used && pdevice->used_name) {
			printk("~~~~add tp error , duplicated current used lcm \n");
		}
		goto found;
	}

		pdevice = init_device_type_info(YFT_TOUCHPANEL,
						(char *)driver, used,
						yft_touchpanel_info_print,
						yft_touchpanel_set_used);
		if (!pdevice) {
			printk("tp_alloc type info failed ~~~~ \n");
			ret = -1;
			goto err;
		}
	
found:

	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(driver->driver.name, info->name)) {
			list_del(&info->list);
			list_del(&info->node);
			kfree(info);
			ret = 0;
			goto err;
		}
	}

	info = kmalloc(sizeof(struct yft_device_info), GFP_KERNEL);
	if (!info) {
		printk("tp_alloc type info failed ~~~~ \n");
		ret = -2;
		goto err;
	}
	memset(info, 0, sizeof(struct yft_device_info));
	INIT_LIST_HEAD(&info->list);
	INIT_LIST_HEAD(&info->node);
	strcpy(info->name, driver->driver.name);
	info->i2c_drv = driver;
	info->used = used;
	list_add(&info->list, &yft_devices->info_list);
	list_add(&info->node, &pdevice->info_list);
	return 0;

err:
	printk("%s: error return: %x: ---\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(yft_touchpanel_device_add);

/* ------------------------------------------------------------------ */
/* touch panel (spi)                                                   */
/* ------------------------------------------------------------------ */
int yft_spitouchpanel_device_add(struct spi_driver *driver, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;
	int ret = 0;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_TOUCHPANEL)
			continue;
		if (used && pdevice->used_name) {
			printk("~~~~add tp error , duplicated current used lcm \n");
		}
		goto found;
	}

		pdevice = init_device_type_info(YFT_TOUCHPANEL,
						(char *)driver, used,
						yft_touchpanel_info_print,
						yft_touchpanel_set_used);
		if (!pdevice) {
			printk("tp_alloc type info failed ~~~~ \n");
			ret = -1;
			goto err;
		}
	
found:

	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(driver->driver.name, info->name)) {
			list_del(&info->list);
			list_del(&info->node);
			kfree(info);
			ret = 0;
			goto err;
		}
	}

	info = kmalloc(sizeof(struct yft_device_info), GFP_KERNEL);
	if (!info) {
		printk("tp_alloc type info failed ~~~~ \n");
		ret = -2;
		goto err;
	}
	memset(info, 0, sizeof(struct yft_device_info));
	INIT_LIST_HEAD(&info->list);
	INIT_LIST_HEAD(&info->node);
	strcpy(info->name, driver->driver.name);
	info->spi_drv = driver;
	info->used = used;
	list_add(&info->list, &yft_devices->info_list);
	list_add(&info->node, &pdevice->info_list);
	return 0;

err:
	printk("%s: error return: %x: ---\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(yft_spitouchpanel_device_add);

int yft_touchpanel_spi_device_add(struct spi_driver *driver, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_touchpanel_spi_info *info;
	int ret = 0;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_TOUCHPANEL)
			continue;
		if (used && pdevice->used_name) {
			printk("~~~~add tp error , duplicated current used lcm \n");
		}
		goto found;
	}

		pdevice = init_device_type_info(YFT_TOUCHPANEL,
						(char *)driver, used,
						yft_touchpanel_info_print,
						yft_touchpanel_set_used);
		if (!pdevice) {
			printk("tp_alloc type info failed ~~~~ \n");
			ret = -1;
			goto err;
		}
	
found:

	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(driver->driver.name, info->name)) {
			list_del(&info->list);
			list_del(&info->node);
			kfree(info);
			ret = 0;
			goto err;
		}
	}

	info = kmalloc(sizeof(struct yft_touchpanel_spi_info), GFP_KERNEL);
	if (!info) {
		printk("tp_alloc type info failed ~~~~ \n");
		ret = -2;
		goto err;
	}
	memset(info, 0, sizeof(struct yft_touchpanel_spi_info));
	INIT_LIST_HEAD(&info->list);
	INIT_LIST_HEAD(&info->node);
	strcpy(info->name, driver->driver.name);
	info->spi_drv = driver;
	info->used = used;
	list_add(&info->list, &yft_devices->info_list);
	list_add(&info->node, &pdevice->info_list);
	return 0;

err:
	printk("%s: error return: %x: ---\n", __func__, ret);
	return ret;
}

/* ------------------------------------------------------------------ */
/* sensors                                                             */
/* ------------------------------------------------------------------ */
#define YFT_SENSOR_DEVICE_ADD(fn, TYPE, print_fn, used_fn, dupmsg, errmsg)    \
int fn(struct sensor_info *sensor, int used)				      \
{									      \
	struct yft_device_type_info *pdevice;				      \
	struct yft_sensor_dev_info *info;				      \
	int ret = 0;							      \
									      \
	pdevice = find_device_type_info(TYPE);				      \
	if (pdevice == NULL) {						      \
		pdevice = init_device_type_info(TYPE,			      \
				(char *)sensor, used,			      \
				print_fn, used_fn);			      \
		if (!pdevice) {						      \
			printk(errmsg);					      \
			ret = -1;					      \
			goto err;					      \
		}							      \
	} else if (used && pdevice->used_name) {			      \
		printk(dupmsg);						      \
	}								      \
									      \
	list_for_each_entry(info, &pdevice->info_list, node) {		      \
		if (!strcmp(sensor->name, info->name)) {			      \
			list_del(&info->list);				      \
			list_del(&info->node);				      \
			kfree(info);					      \
			ret = 0;					      \
			goto err;					      \
		}							      \
	}								      \
									      \
	info = kmalloc(sizeof(struct yft_sensor_dev_info), GFP_KERNEL);	      \
	if (!info) {							      \
		printk(errmsg);						      \
		ret = -2;						      \
		goto err;						      \
	}								      \
	memset(info, 0, sizeof(struct yft_sensor_dev_info));		      \
	INIT_LIST_HEAD(&info->list);					      \
	INIT_LIST_HEAD(&info->node);					      \
	strcpy(info->name, sensor->name);				      \
	info->info = sensor;						      \
	info->used = used;						      \
	list_add(&info->list, &yft_devices->info_list);			      \
	list_add(&info->node, &pdevice->info_list);			      \
	return 0;							      \
									      \
err:									      \
	printk("%s: error return: %x: ---\n", __func__, ret);		      \
	return ret;							      \
}									      \
EXPORT_SYMBOL(fn)

YFT_SENSOR_DEVICE_ADD(yft_accsensor_device_add, YFT_ACCSENSOR,
		      yft_accsensor_info_print, yft_accsensor_set_used,
		      "~~~~add accsensor error , duplicated current used lcm \n",
		      "acc_alloc type info failed ~~~~ \n");

YFT_SENSOR_DEVICE_ADD(yft_msensor_device_add, YFT_MSENSOR,
		      yft_msensor_info_print, yft_msensor_set_used,
		      "~~~~add msensor error , duplicated current used lcm \n",
		      "msensor_alloc type info failed ~~~~ \n");

YFT_SENSOR_DEVICE_ADD(yft_alspssensor_device_add, YFT_ALSPSSENSOR,
		      yft_alspssensor_info_print, yft_alspssensor_set_used,
		      "~~~~add alsps error , duplicated current used lcm \n",
		      "alsps_alloc type info failed ~~~~ \n");

YFT_SENSOR_DEVICE_ADD(yft_sarsensor_device_add, YFT_SARSENSOR,
		      yft_sarsensor_info_print, yft_sarsensor_set_used,
		      "~~~~add sarsensor error , duplicated current used lcm \n",
		      "sar_alloc type info failed ~~~~ \n");

YFT_SENSOR_DEVICE_ADD(yft_barosensor_device_add, YFT_BAROSENSOR,
		      yft_barosensor_info_print, yft_barosensor_set_used,
		      "~~~~add alsps error , duplicated current used lcm \n",
		      "baro_alloc type info failed ~~~~ \n");

/* ------------------------------------------------------------------ */
/* fuel gauge / tiny lcd                                               */
/* ------------------------------------------------------------------ */
int yft_fuelgauge_device_add(struct i2c_driver *driver, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;
	int ret = 0;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_FUELGAUGE)
			continue;
		if (used && pdevice->used_name) {
			printk("~~~~add tp error , duplicated current used lcm \n");
		}
		goto found;
	}

		pdevice = init_device_type_info(YFT_FUELGAUGE,
						(char *)driver, used,
						yft_fuelgauge_info_print,
						yft_fuelgauge_set_used);
		if (!pdevice) {
			printk("tp_alloc type info failed ~~~~ \n");
			ret = -1;
			goto err;
		}
	
found:

	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(driver->driver.name, info->name)) {
			list_del(&info->list);
			list_del(&info->node);
			kfree(info);
			ret = 0;
			goto err;
		}
	}

	info = kmalloc(sizeof(struct yft_device_info), GFP_KERNEL);
	if (!info) {
		printk("tp_alloc type info failed ~~~~ \n");
		ret = -2;
		goto err;
	}
	memset(info, 0, sizeof(struct yft_device_info));
	INIT_LIST_HEAD(&info->list);
	INIT_LIST_HEAD(&info->node);
	strcpy(info->name, driver->driver.name);
	info->i2c_drv = driver;
	info->used = used;
	list_add(&info->list, &yft_devices->info_list);
	list_add(&info->node, &pdevice->info_list);
	return 0;

err:
	printk("%s: error return: %x: ---\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(yft_fuelgauge_device_add);

int yft_tinylcd_device_add(struct spi_driver *driver, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;
	int ret = 0;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_TINYLCD)
			continue;
		if (used && pdevice->used_name) {
			printk("~~~~add tp error , duplicated current used lcm \n");
		}
		goto found;
	}

		pdevice = init_device_type_info(YFT_TINYLCD,
						(char *)driver, used,
						yft_tinylcd_info_print,
						yft_tinylcd_set_used);
		if (!pdevice) {
			printk("tp_alloc type info failed ~~~~ \n");
			ret = -1;
			goto err;
		}
	
found:

	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(driver->driver.name, info->name)) {
			list_del(&info->list);
			list_del(&info->node);
			kfree(info);
			ret = 0;
			goto err;
		}
	}

	info = kmalloc(sizeof(struct yft_device_info), GFP_KERNEL);
	if (!info) {
		printk("tp_alloc type info failed ~~~~ \n");
		ret = -2;
		goto err;
	}
	memset(info, 0, sizeof(struct yft_device_info));
	INIT_LIST_HEAD(&info->list);
	INIT_LIST_HEAD(&info->node);
	strcpy(info->name, driver->driver.name);
	info->spi_drv = driver;
	info->used = used;
	list_add(&info->list, &yft_devices->info_list);
	list_add(&info->node, &pdevice->info_list);
	return 0;

err:
	printk("%s: error return: %x: ---\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(yft_tinylcd_device_add);

/* ------------------------------------------------------------------ */
/* "set used" entry points                                             */
/* ------------------------------------------------------------------ */
/*
 * Stock dispatches through the per-type callback registered by
 * init_device_type_info(); every yft_set_*_device_used() export is therefore
 * exactly 152 bytes of identical code with only the type id differing, walks
 * the whole type list, and always returns 0.
 */
#define YFT_SET_DEVICE_USED(fn, TYPE)					\
int fn(char *name, int used)						\
{									\
	struct yft_device_type_info *pdevice;				\
									\
	list_for_each_entry(pdevice, &yft_devices->list, list) {	\
		if (pdevice->type == (TYPE) && pdevice->set_used)	\
			pdevice->set_used(name, used);			\
	}								\
	return 0;							\
}									\
EXPORT_SYMBOL(fn)

YFT_SET_DEVICE_USED(yft_set_touch_device_used, YFT_TOUCHPANEL);
YFT_SET_DEVICE_USED(yft_set_camera_device_used, YFT_CAMERA);
YFT_SET_DEVICE_USED(yft_set_accsensor_device_used, YFT_ACCSENSOR);
YFT_SET_DEVICE_USED(yft_set_msensor_device_used, YFT_MSENSOR);
YFT_SET_DEVICE_USED(yft_set_alspssensor_device_used, YFT_ALSPSSENSOR);
YFT_SET_DEVICE_USED(yft_set_sarsensor_device_used, YFT_SARSENSOR);
YFT_SET_DEVICE_USED(yft_set_barosensor_device_used, YFT_BAROSENSOR);
YFT_SET_DEVICE_USED(yft_set_fuelgauge_device_used, YFT_FUELGAUGE);
YFT_SET_DEVICE_USED(yft_set_tinylcd_device_used, YFT_TINYLCD);

/* ------------------------------------------------------------------ */
/* display info list                                                   */
/* ------------------------------------------------------------------ */
/* Return type proven void by KCFI type id _ZTSFvPciE == 0x6a3c623b and by the
 * stock codegen (no w0 set before ret).
 */
void yft_add_display_info(char *info, int len)
{
	struct yftdisp_info *dinfo;

	dinfo = kmalloc(sizeof(struct yftdisp_info), GFP_KERNEL);
	if (!dinfo)
		return;
	dinfo->info = kmalloc(len, GFP_KERNEL);
	if (!dinfo->info) {
		kfree(dinfo);
		return;
	}
	memcpy(dinfo->info, info, len);
	INIT_LIST_HEAD(&dinfo->list);
	list_add(&dinfo->list, &yftdisp_info_list);
}

/* Return type proven void by KCFI type id _ZTSFvP8seq_fileE == 0x9831c2fc. */
void yft_display_info_dump(struct seq_file *m)
{
	struct yftdisp_info *dinfo;

	seq_printf(m, "----INFO DUMP---\t \n");
	list_for_each_entry(dinfo, &yftdisp_info_list, list)
		seq_printf(m, "Info:%s\n", dinfo->info);
}

/* ------------------------------------------------------------------ */
/* bootargs derived information                                        */
/* ------------------------------------------------------------------ */
int lpddr_used_name_ptr(char *name)
{
	struct device_node *of_chosen = NULL;
	char *ptr = NULL;
	char *bootargs = NULL;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(of_chosen, "bootargs", NULL);
		if (!bootargs)
			return -1;
		ptr = strstr(bootargs, "lpddr_used_name=");
		if (ptr == NULL) {
			printk("[%s]  bootargs is null\n", __func__);
			return -1;
		}
		ptr += strlen("lpddr_used_name=");
		strncpy(name, ptr, 16);
		return 0;
	}
	return 0;
}

int get_lpddr_emmc_flash_type(void)
{
	struct device_node *of_chosen = NULL;
	char *ptr = NULL;
	char *bootargs = NULL;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(of_chosen, "bootargs", NULL);
		ptr = strstr(bootargs, "flash_type=");
		if (ptr == NULL) {
			printk("[%s]  bootargs is null\n", __func__);
			return -1;
		}
		ptr += strlen("flash_type=");
		return simple_strtol(ptr, NULL, 10);
	}
	return 0;
}

int get_lpddr_emmc_used_index(void)
{
	struct device_node *of_chosen = NULL;
	char *ptr = NULL;
	char *bootargs = NULL;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(of_chosen, "bootargs", NULL);
		ptr = strstr(bootargs, "lpddr_used_index=");
		if (ptr == NULL) {
			printk("[%s]  bootargs is null\n", __func__);
			return -1;
		}
		ptr += strlen("lpddr_used_index=");
		return simple_strtol(ptr, NULL, 10);
	}
	return 0;
}

int get_secureboot_value(void)
{
	struct device_node *of_chosen = NULL;
	char *ptr = NULL;
	char *bootargs = NULL;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(of_chosen, "bootargs", NULL);
		ptr = strstr(bootargs, "yft_boot_secureboot=");
		if (ptr == NULL) {
			printk("[%s]  bootargs is null\n", "get_secureboot_value");
			return -1;
		}
		ptr += strlen("yft_boot_secureboot=");
		return simple_strtol(ptr, NULL, 10);
	}
	return 0;
}

int get_secure_boot_state(void)
{
	struct device_node *of_chosen = NULL;
	char *ptr = NULL;
	char *bootargs = NULL;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(of_chosen, "bootargs", NULL);
		ptr = strstr(bootargs, "yft_boot_secureboot=");
		if (ptr == NULL) {
			printk("[%s]  bootargs is null\n", "get_secureboot_value");
			return -1;
		}
		ptr += strlen("yft_boot_secureboot=");
		return simple_strtol(ptr, NULL, 10);
	}
	return 0;
}

int get_ufs_lifetime_state(char *lifetime)
{
	struct device_node *of_chosen = NULL;
	char *ptr = NULL;
	char *bootargs = NULL;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(of_chosen, "bootargs", NULL);
		ptr = strstr(bootargs, "PreEOLInfo =");
		if (ptr == NULL) {
			printk("[%s]  bootargs is null\n", __func__);
			return -1;
		}
		strncpy(lifetime, ptr, 55);
		lifetime[55] = '\0';
		printk("[%s]  ufs_lifetime=%s\n", __func__, lifetime);
		return 0;
	}
	return 0;
}

int yft_get_lcm_for_lk(char *lcm_name, char *ext_lcm_name)
{
	struct device_node *of_chosen = NULL;
	char *ptr = NULL;
	char *bootargs = NULL;
	int i = 0;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen == NULL) {
		printk("[%s]  of_chosen fail\n", __func__);
		return 0;
	}

	bootargs = (char *)of_get_property(of_chosen, "bootargs", NULL);
	ptr = strstr(bootargs, "lcmname=");
	if (ptr == NULL) {
		printk("[%s]  bootargs is null\n", __func__);
		return -1;
	}
	ptr += strlen("lcmname=");
	for (i = 0; i < 60; i++) {
		if (ptr[i] == ' ')
			break;
	}
	strncpy(lcm_name, ptr, i + 1);
	printk("[%s]  lcm_1=%s,len=%d\n", __func__, lcm_name, i);

	ptr = strstr(bootargs, "yft_ext_lcmname=");
	if (ptr == NULL) {
		printk("[%s]  bootargs is null\n", __func__);
		return -1;
	}
	ptr += strlen("yft_ext_lcmname=");
	for (i = 0; i < 60; i++) {
		if (ptr[i] == ' ')
			break;
	}
	strncpy(ext_lcm_name, ptr, i + 1);
	printk("[%s]  yft_ext_lcmname=%s,len=%d\n", __func__, ext_lcm_name, i);
	return 0;
}

/* ------------------------------------------------------------------ */
/* /proc dumps                                                         */
/* ------------------------------------------------------------------ */
int yft_device_dump(struct seq_file *m)
{
	struct yft_device_type_info *pdevice;
	struct yftdisp_info *dinfo;
	struct device_node *root;
	const char *model = NULL;
	char ext_lcm_name[60] = { 0 };
	char lcm_name[60] = { 0 };

	root = of_find_node_by_path("/");
	of_property_read_string(root, "model", &model);
	seq_printf(m, "base:%s \tBBChip:%s\n", "alps-release-u0.mp1.rc", model);

	seq_printf(m, " \t\t DEVICE DUMP--begin\n");
	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->info_print)
			pdevice->info_print(m);
		else
			printk(" error !!! this type [%d] has no dump fun \n",
			       pdevice->type);
	}

	yft_get_lcm_for_lk(lcm_name, ext_lcm_name);
	seq_printf(m, "---------LCM USAGE-------- \n");
	seq_printf(m, "    %s\t:     used\n", lcm_name);
	seq_printf(m, "    %s\t:     used\n", ext_lcm_name);
	seq_printf(m, "modem: %s\n", CUSTOM_MODEM);

	seq_printf(m, "----INFO DUMP---\t \n");
	list_for_each_entry(dinfo, &yftdisp_info_list, list)
		seq_printf(m, "Info:%s\n", dinfo->info);
	return 0;
}
EXPORT_SYMBOL(yft_device_dump);

int yft_memory_dump(struct seq_file *m)
{
	struct yftdisp_info *dinfo;
	char lpddr_used_name[20] = { 0 };
	int i = 0;

	get_lpddr_emmc_used_index();
	seq_printf(m, "Memory DUMP--begin\n\n");
	seq_printf(m, " EMMC compatible num =%d,type=%x  \n", 9,
		   get_lpddr_emmc_flash_type());
	for (i = 0; i < 9; i++)
		seq_printf(m, " EMMC COMP[%d] =%s\t--support\n", i,
			   Cust_emmc_support[i]);
	seq_printf(m, "\n\n");

	lpddr_used_name_ptr(lpddr_used_name);
	seq_printf(m, " used = %s\n", lpddr_used_name);
	seq_printf(m, " used id = 0x");
	for (i = 0; lpddr_used_name[i] != '\0'; i++)
		seq_printf(m, "%2x", lpddr_used_name[i]);
	seq_printf(m, "\n\n");
	seq_printf(m, " Memory DUMP--end\n\n");

	seq_printf(m, "----INFO DUMP---\t \n");
	list_for_each_entry(dinfo, &yftdisp_info_list, list)
		seq_printf(m, "Info:%s\n", dinfo->info);
	return 0;
}
EXPORT_SYMBOL(yft_memory_dump);

int is_yft_cts_board(void)
{
	return yft_urxdo_flag;
}
EXPORT_SYMBOL(is_yft_cts_board);

int yft_cts_dump(struct seq_file *m)
{
	seq_printf(m, yft_urxdo_flag ? "1" : "0");
	return 0;
}
EXPORT_SYMBOL(yft_cts_dump);

/* ------------------------------------------------------------------ */
/* sysfs attributes                                                    */
/* ------------------------------------------------------------------ */
static ssize_t mt_sar_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if (dev == NULL) {
		printk("mt_sar_name_show. dev is null!!\n");
		return 0;
	}
	return scnprintf(buf, PAGE_SIZE, "%s\n", yft_sar_name_buf);
}
static DEVICE_ATTR(yft_sar_name, 0444, mt_sar_name_show, NULL);

static ssize_t mt_cts_flag_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if (dev == NULL) {
		printk("mt_cts_flag_show. dev is null!!\n");
		return 0;
	}
	return scnprintf(buf, PAGE_SIZE, "%d\n", yft_urxdo_flag ? 1 : 0);
}
static DEVICE_ATTR(yft_cts_flag, 0444, mt_cts_flag_show, NULL);

static ssize_t mt_secure_boot_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	if (dev == NULL) {
		printk("mt_secure_boot_show. dev is null!!\n");
		return 0;
	}
	return scnprintf(buf, PAGE_SIZE, "%d\n", get_secureboot_value());
}
static DEVICE_ATTR(secure_boot, 0444, mt_secure_boot_show, NULL);

static ssize_t mt_ufs_lifetime_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	char ufs_lifetime[56] = { 0 };

	if (dev == NULL) {
		printk("mt_ufs_lifetime_show. dev is null!!\n");
		return 0;
	}
	get_ufs_lifetime_state(ufs_lifetime);
	printk("mt_ufs_lifetime_show. [ufs_lifetime=%s]\n", ufs_lifetime);
	return scnprintf(buf, PAGE_SIZE, "%s\n", ufs_lifetime);
}
static DEVICE_ATTR(ufs_lifetime, 0444, mt_ufs_lifetime_show, NULL);

static struct device_attribute *const mt_sysfs_attributes[] = {
	&dev_attr_yft_cts_flag,
	&dev_attr_yft_sar_name,
	&dev_attr_secure_boot,
	&dev_attr_ufs_lifetime,
	NULL,
};

/* ------------------------------------------------------------------ */
/* per-type printers / used setters                                    */
/* ------------------------------------------------------------------ */
static int yft_camera_info_print(struct seq_file *m)
{
	struct yft_device_type_info *pdevice;
	struct yft_camera_dev_info *info;

	seq_printf(m, "---------CAMERA USAGE-------- \n");
	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_CAMERA)
			continue;
		goto found;
	}
	seq_printf(m, "        \t:   NONE    \n");
	return 0;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		seq_printf(m, "      %20s\t\t:   ", info->name);
		if (!info->used) {
			seq_printf(m, "  supported \n");
			continue;
		}
		seq_printf(m, "  used     \t");
		switch (info->camera_id) {
		case 1:
			seq_printf(m, "  main camera ");
			seq_printf(m, "\n");
			break;
		case 2:
			seq_printf(m, "  sub camera ");
			seq_printf(m, "\n");
			break;
		case 3:
			seq_printf(m, "  main2 camera \n");
			break;
		case 4:
			seq_printf(m, "  sub2 camera \n");
			break;
		case 5:
			seq_printf(m, "  main3 camera \n");
			break;
		default:
			seq_printf(m, " camera unsupportd type %d\n",
				   info->camera_id);
			break;
		}
	}
	return 0;
}

static int yft_camera_set_used(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_camera_dev_info *info;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_CAMERA)
			continue;
		goto found;
	}
	return -1;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			info->used = 1;
			info->camera_id = used;
			break;
		}
	}
	return 0;
}

static int yft_touchpanel_info_print(struct seq_file *m)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;

	seq_printf(m, "--------TOUCHPANEL USAGE-------\t \n");
	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_TOUCHPANEL)
			continue;
		goto found;
	}
	seq_printf(m, "        \t:   NONE    \n");
	return 0;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		seq_printf(m, "      %20s\t:   ", info->name);
		if (!info->used) {
			seq_printf(m, "  supported \n");
			continue;
		}
		seq_printf(m, "  used \t");
		if (!strcmp("hyn_ts", info->name))
			seq_printf(m, "  %20s\t \n", second_touch_fw_version);
		else
			seq_printf(m, "  %20s\t \n", touch_fw_version);
	}
	return 0;
}

static int yft_touchpanel_set_used(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_TOUCHPANEL)
			continue;
		goto found;
	}
	return -1;
found:
	printk("touch type has find break !!\n");
	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			info->used = 1;
			break;
		}
	}
	return 0;
}

#define YFT_SENSOR_INFO_PRINT(fn, TYPE, banner)				\
static int fn(struct seq_file *m)					\
{									\
	struct yft_device_type_info *pdevice;				\
	struct yft_sensor_dev_info *info;				\
									\
	seq_printf(m, banner);						\
	list_for_each_entry(pdevice, &yft_devices->list, list) {	\
		if (pdevice->type != (TYPE))				\
			continue;					\
		goto found;						\
	}								\
	seq_printf(m, "        \t:   NONE    \n");			\
	return 0;							\
found:									\
	list_for_each_entry(info, &pdevice->info_list, node) {		\
		seq_printf(m, "      %20s\t\t:   ", info->name);	\
		seq_printf(m, info->used ? "  used \t\n"		\
					 : "  supported \n");		\
	}								\
	return 0;							\
}

YFT_SENSOR_INFO_PRINT(yft_accsensor_info_print, YFT_ACCSENSOR,
		      "--------accsensor USAGE-------\t \n")

static int yft_accsensor_set_used(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_sensor_dev_info *info;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_ACCSENSOR)
			continue;
		goto found;
	}
	return -1;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			info->used = 1;
			break;
		}
	}
	return 0;
}

YFT_SENSOR_INFO_PRINT(yft_msensor_info_print, YFT_MSENSOR,
		      "--------msensor USAGE-------\t \n")

static int yft_msensor_set_used(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_sensor_dev_info *info;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_MSENSOR)
			continue;
		goto found;
	}
	return -1;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			info->used = 1;
			break;
		}
	}
	return 0;
}

YFT_SENSOR_INFO_PRINT(yft_alspssensor_info_print, YFT_ALSPSSENSOR,
		      "--------alspssensor USAGE-------\t \n")

static int yft_alspssensor_set_used(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_sensor_dev_info *info;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_ALSPSSENSOR)
			continue;
		goto found;
	}
	return -1;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			info->used = 1;
			break;
		}
	}
	return 0;
}

YFT_SENSOR_INFO_PRINT(yft_sarsensor_info_print, YFT_SARSENSOR,
		      "--------sarsensor USAGE-------\t \n")

static int yft_sarsensor_set_used(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_sensor_dev_info *info;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_SARSENSOR)
			continue;
		goto found;
	}
	return -1;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			info->used = 1;
			strncpy(yft_sar_name_buf, info->name, 6);
			break;
		}
	}
	return 0;
}

YFT_SENSOR_INFO_PRINT(yft_barosensor_info_print, YFT_BAROSENSOR,
		      "--------barosensor USAGE-------\t \n")

static int yft_barosensor_set_used(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_sensor_dev_info *info;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_BAROSENSOR)
			continue;
		goto found;
	}
	return -1;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			info->used = 1;
			break;
		}
	}
	return 0;
}

static int yft_fuelgauge_info_print(struct seq_file *m)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;

	seq_printf(m, "--------FUELGAUGE USAGE-------\t \n");
	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_FUELGAUGE)
			continue;
		goto found;
	}
	seq_printf(m, "        \t:   NONE    \n");
	return 0;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		seq_printf(m, "      %20s\t:   ", info->name);
		if (!info->used) {
			seq_printf(m, "  supported \n");
			continue;
		}
		seq_printf(m, "  used \t");
		seq_printf(m, "  %20s\t \n", fuelgauge_fw_version);
	}
	return 0;
}

static int yft_fuelgauge_set_used(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_FUELGAUGE)
			continue;
		goto found;
	}
	return -1;
found:
	printk("fuelgauge type has find break !!\n");
	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			info->used = 1;
			break;
		}
	}
	return 0;
}

static int yft_tinylcd_info_print(struct seq_file *m)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;

	seq_printf(m, "--------TINY_LCD USAGE-------\t \n");
	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_TINYLCD)
			continue;
		goto found;
	}
	seq_printf(m, "        \t:   NONE    \n");
	return 0;
found:
	list_for_each_entry(info, &pdevice->info_list, node) {
		seq_printf(m, "      %20s\t:   ", info->name);
		if (!info->used) {
			seq_printf(m, "  supported \n");
			continue;
		}
		seq_printf(m, "  used \t");
		seq_printf(m, "  %20s\t \n", tinylcd_fw_version);
	}
	return 0;
}

static int yft_tinylcd_set_used(char *name, int used)
{
	struct yft_device_type_info *pdevice;
	struct yft_device_info *info;

	list_for_each_entry(pdevice, &yft_devices->list, list) {
		if (pdevice->type != YFT_TINYLCD)
			continue;
		goto found;
	}
	return -1;
found:
	printk("tinylcd type has find break !!\n");
	list_for_each_entry(info, &pdevice->info_list, node) {
		if (!strcmp(name, info->name)) {
			info->used = 1;
			break;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* class attributes                                                    */
/* ------------------------------------------------------------------ */
static ssize_t yft_fm_switch_show(struct class *cls,
				  struct class_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", 1);
}

static struct class_attribute mt_sysfs_class_attributes[] = {
	__ATTR(yft_fm_switch, 0444, yft_fm_switch_show, NULL),
	{ },
};

static int init_yft_sysfs(struct device *dev)
{
	int err = 0;
	int idx;

	printk("init_yft_sysfs. begin. \n");
	for (idx = 0; mt_sysfs_attributes[idx] != NULL; idx++) {
		err = device_create_file(dev, mt_sysfs_attributes[idx]);
		if (err)
			goto exit_error;
	}
	return err;

exit_error:
	for (--idx; idx >= 0; idx--)
		device_remove_file(dev, mt_sysfs_attributes[idx]);
	return err;
}

static int init_yft_class_sysfs(struct class *cls)
{
	int err = 0;
	int idx;

	printk("init_yft_class_sysfs. begin. \n");
	for (idx = 0; mt_sysfs_class_attributes[idx].attr.name != NULL; idx++) {
		err = class_create_file(cls, &mt_sysfs_class_attributes[idx]);
		if (err)
			break;
	}
	if (err) {
		for (--idx; idx >= 0; idx--)
			class_remove_file(cls, &mt_sysfs_class_attributes[idx]);
	}
	return err;
}

/* ------------------------------------------------------------------ */
/* /proc/yftinfo, /proc/yftmeminfo, /proc/yftctsinfo                    */
/* ------------------------------------------------------------------ */
static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void c_stop(struct seq_file *m, void *v)
{
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static int c_info_show(struct seq_file *m, void *v)
{
	yft_device_dump(m);
	return 0;
}

static int c_memory_show(struct seq_file *m, void *v)
{
	yft_memory_dump(m);
	return 0;
}

static int c_cts_show(struct seq_file *m, void *v)
{
	yft_cts_dump(m);
	return 0;
}

const struct seq_operations yftinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_info_show,
};

const struct seq_operations memoryinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_memory_show,
};

const struct seq_operations ctsinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_cts_show,
};

static int yftinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &yftinfo_op);
}

static int memoryinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &memoryinfo_op);
}

static int ctsinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ctsinfo_op);
}

static const struct proc_ops proc_yftinfo_operations = {
	.proc_open	= yftinfo_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static const struct proc_ops proc_memoryinfo_operations = {
	.proc_open	= memoryinfo_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static const struct proc_ops proc_ctsinfo_operations = {
	.proc_open	= ctsinfo_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static struct proc_dir_entry *yftinfo_proc;
static struct proc_dir_entry *yftmeminfo_proc;
static struct proc_dir_entry *yftctsinfo_proc;
static struct class *yft_device_class;

int proc_yftinfo_init(void)
{
	printk("%s: entery\n", __func__);

	yftinfo_proc = proc_create("yftinfo", 0666, NULL,
				   &proc_yftinfo_operations);
	if (!yftinfo_proc)
		goto err_info;

	yftmeminfo_proc = proc_create("yftmeminfo", 0666, NULL,
				      &proc_memoryinfo_operations);
	if (!yftmeminfo_proc)
		goto err_mem;

	yftctsinfo_proc = proc_create("yftctsinfo", 0666, NULL,
				      &proc_ctsinfo_operations);
	if (!yftctsinfo_proc)
		goto err_cts;

	return 0;

err_cts:
	proc_remove(yftmeminfo_proc);
	yftmeminfo_proc = NULL;
err_mem:
	proc_remove(yftinfo_proc);
	yftinfo_proc = NULL;
err_info:
	printk("create proc entries for yftinfo failed ~~~~\n");
	return -ENOMEM;
}

static void proc_yftinfo_exit(void)
{
	if (yftctsinfo_proc) {
		proc_remove(yftctsinfo_proc);
		yftctsinfo_proc = NULL;
	}
	if (yftmeminfo_proc) {
		proc_remove(yftmeminfo_proc);
		yftmeminfo_proc = NULL;
	}
	if (yftinfo_proc) {
		proc_remove(yftinfo_proc);
		yftinfo_proc = NULL;
	}
}

static void yft_devices_release(void)
{
	struct yft_device_type_info *type;
	struct yft_device_info *info;
	struct yftdisp_info *disp;

	if (!yft_devices)
		return;

	while (!list_empty(&yft_devices->info_list)) {
		info = list_first_entry(&yft_devices->info_list,
					struct yft_device_info, list);

		list_del(&info->list);
		kfree(info);
	}

	while (!list_empty(&yft_devices->list)) {
		type = list_first_entry(&yft_devices->list,
					struct yft_device_type_info, list);
		list_del(&type->list);
		kfree(type);
	}

	while (!list_empty(&yftdisp_info_list)) {
		disp = list_first_entry(&yftdisp_info_list,
					struct yftdisp_info, list);
		list_del(&disp->list);
		kfree(disp->info);
		kfree(disp);
	}

	kfree(yft_devices);
	yft_devices = NULL;
}

/* ------------------------------------------------------------------ */
/* platform driver                                                     */
/* ------------------------------------------------------------------ */
static int yft_devices_probe(struct platform_device *pdev)
{
	int ret;

	printk("%s: start\n", __func__);

	ret = proc_yftinfo_init();
	if (ret < 0)
		return ret;
	printk("proc_yftinfo_init ok\n");

	yft_devices = kzalloc(sizeof(struct yft_devices_data), GFP_KERNEL);
	if (yft_devices == NULL) {
		printk("%s: error probe becase of mem\n", __func__);
		ret = -ENOMEM;
		goto err_proc;
	}
	INIT_LIST_HEAD(&yft_devices->list);
	INIT_LIST_HEAD(&yft_devices->info_list);
	mutex_init(&yft_devices->de_mutex);
	INIT_LIST_HEAD(&yftdisp_info_list);

	ret = init_yft_sysfs(&pdev->dev);
	if (ret)
		printk("failed to init_sysfs. \n");

	yft_device_class = class_create(THIS_MODULE, "yft_device");
	if (IS_ERR(yft_device_class)) {
		ret = (int)PTR_ERR(yft_device_class);
		yft_device_class = NULL;
		pr_err("create class, err = %d\n", ret);
		goto err_devices;
	}

	ret = init_yft_class_sysfs(yft_device_class);
	if (ret) {
		pr_err("failed to init_class_sysfs, err = %d\n", ret);
		goto err_class;
	}

	printk("%s: end\n", __func__);
	return 0;

err_class:
	class_destroy(yft_device_class);
	yft_device_class = NULL;
err_devices:
	yft_devices_release();
err_proc:
	proc_yftinfo_exit();
	return ret;
}

static int yft_devices_remove(struct platform_device *pdev)
{
	if (yft_device_class) {
		class_destroy(yft_device_class);
		yft_device_class = NULL;
	}

	yft_devices_release();
	proc_yftinfo_exit();
	return 0;
}

struct of_device_id yft_device_of_match[] = {
	{ .compatible = "mediatek,yft_devices", },
	{},
};

static struct platform_driver yft_devices_driver = {
	.probe		= yft_devices_probe,
	.remove		= yft_devices_remove,
	.driver		= {
		.name	= "yft_device",
		.of_match_table = yft_device_of_match,
		.owner	= THIS_MODULE,
	},
};

static int __init yft_devices_init(void)
{
	int ret;

	printk("%s: start\n", __func__);

	yftdebug = 1;
	ret = platform_driver_register(&yft_devices_driver);
	yftdebug = 0;
	if (ret) {
		printk("failed to register driver");
		return -19;
	}

	if (get_urxdo_select_gpio() < 0)
		printk("get_urxdo_select_gpio failed.\n");

	printk("%s: end\n", __func__);
	return 0;
}

static void __exit yft_devices_exit(void)
{
	platform_driver_unregister(&yft_devices_driver);

	if (yft_urxdo_gpio >= 0) {
		gpio_free(yft_urxdo_gpio);
		yft_urxdo_gpio = -1;
	}
}

module_init(yft_devices_init);
module_exit(yft_devices_exit);

MODULE_AUTHOR("Jay Zhou");
MODULE_DESCRIPTION("YFT DEVICE INFO");
MODULE_LICENSE("GPL");
