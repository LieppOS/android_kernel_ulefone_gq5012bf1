/* SPDX-License-Identifier: GPL-2.0 */
/*
 * extcon-mtk-usb.c -- reconstruction for Ulefone GQ5012BF1 (MT6878/MT6878T)
 *
 * Target binary: stock vendor //vendor/lib/modules/extcon-mtk-usb.ko
 *   sha256 9e6adf23de6729f9983f680be75c8aca9eabe22aa6efcaafe7704257b37cb2bf
 *   vermagic 6.1.115-android14-11-g945dff7bc1bf SMP preempt mod_unload
 *            modversions aarch64        (.modinfo carries NO srcversion field)
 *   build-compiler Android clang 17.0.2 (r487747c), +pgo +bolt +lto
 *   build-id bdfe8ed88c1df7083329acd2e2999c3c2b043796
 *   depends=tcpc_class,mt6375-charger          description "MediaTek Extcon USB Driver"
 *
 * PROVENANCE OF THIS SOURCE
 * -------------------------
 * The device's own UsMart kernel source drop (Fibo_Fone_P3226, 164 archives)
 * ships NO extcon driver at all: it declares CONFIG_USB_CTRL_MT6800_CHARGER_CON
 * in drivers/extcon/Kconfig+Makefile but provides neither the Kconfig symbol nor
 * the .c file, so that tree's extcon obj-* resolves to nothing. Closure therefore
 * has to come from a donor + binary, not from a Ulefone source diff.
 *
 * Donor: kernel/msm-4.19-style MediaTek v17 driver, taken verbatim from the
 * sibling MT6878 tree
 *   kernel-research/nothing-mt6878/device_modules/drivers/misc/mediatek/extcon/
 *     extcon-mtk-usb.c   (758 lines)
 *     extcon-mtk-usb.h   (struct mtk_extcon_info, struct usb_role_info)
 * That donor matches the stock module's own .symtab names 1:1 for 15 of its 22
 * functions (the stock .ko is NOT stripped: .symtab carries Usmart's real names
 * with exact offsets and sizes), and every donor string/call sequence below is
 * present in the stock .text.
 *
 * Evidence-graded markers used throughout:
 *   [PROVEN_EXACT]        donor code reproduced by the stock build: name, size
 *                         and whole call/string sequence agree with the oracle
 *   [PROVEN_STRUCTURE]  semantics pinned by stock evidence (strings, call order,
 *                         data-object sizes, DT contract) but instruction-level
 *                         layout not asserted
 *   [RECONSTRUCTED]       behaviour inferred; not asserted to match the stock codegen
 *
 * The Ulefone/Usmart delta over the donor -- i.e. what the stock build adds --
 * is six functions and five data objects, all YFT thermal-camera plumbing:
 *   get_otg_select_gpio  yft_parse_dts  show_uvc_insert_status
 *   show_tiny2c_mode     store_tiny2c_mode  (+ probe/shutdown/detect_cable growth)
 *   yft_extcon  yft_otg_select_gpio_num  yft_uvc_switch_gpio_num
 *   yft_uvc_vbus_gpio_num  uvc_insert_status  class_attr_tiny2c_mode
 * In place of the donor's Nothing-specific CONFIG_NT_USB_TS
 * extcon_usb_mode_switch()/g_extcon export -- absent from the stock symtab --
 * Ulefone uses the tiny2c_mode class attribute gated on mt6375-charger's
 * `yft_usb_flag`.
 */

#include <linux/extcon-provider.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/regulator/consumer.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/usb/role.h>
#include <linux/workqueue.h>

/* -------------------------------------------------------------------------
 * Provider ABI shims.
 *
 * The stock module's 63 imports leave exactly four symbols unresolved against
 * GKI 6.1.75: tcpc_class's register_tcp_dev_notifier() and
 * tcpc_dev_get_by_name(), tcpm_inquire_remote_cc(), and mt6375-charger's
 * yft_usb_flag. Everything else (extcon core, regulator, power_supply, gpio,
 * procfs, usb/role.h, workqueue, __class_create/class_create_file_ns) is
 * already exported by the GKI build. Those four come from the sibling
 * LieppOS provider modules (tcpc-class-*, mt6375-charger-provider-abi), which
 * is also how the stock module declares itself: depends=tcpc_class,mt6375-
 * charger. Until those providers are built, KBUILD_EXTRA_SYMBOLS is empty and
 * the Type-C paths below compile out; the YFT GPIO/UVC/tiny2c paths -- the
 * actual uSmart thermal-camera ownership chain -- do not depend on them.
 * ---------------------------------------------------------------------- */
/* Provider ABI, established from stock's 63 undefined symbols: exactly four of
 * them are not exported by the GKI 6.1.115 vmlinux, and they map one-to-one onto
 * the two modules named in stock's .modinfo depends=tcpc_class,mt6375-charger:
 *   register_tcp_dev_notifier, tcpc_dev_get_by_name, tcpm_inquire_remote_cc
 *     -> //lieppos/tcpc-class-recon:tcpc_class_gki, whose inc/tcpm.h declares
 *        all three (plus the TCP_NOTIFY_ / TYPEC_CC_ / PD_ROLE_ enums used
 *        below). There is no tcpc_class.h in this workspace; tcpc_class.ko is
 *        the module name, not a header.
 *   yft_usb_flag
 *     -> //lieppos/mt6375-charger-provider-abi (build-only witness, never load;
 *        runtime provider is the stock mt6375-charger.ko)
 * Notably absent from stock: get_charger_by_name and
 * charger_dev_enable_vac_otgovp. The donor's dvchg1 charger_device path was
 * therefore dropped by Usmart -- the OTG/VBUS current path here runs through
 * the regulator framework only (devm_regulator_get_exclusive,
 * regulator_set_voltage/_set_current_limit/_enable/_disable,
 * regulator_is_enabled), which is also why vbus-limit-current is a sysfs
 * attribute on this device rather than a charger-framework delegation.
 */
#if IS_ENABLED(CONFIG_TCPC_CLASS)
#include "tcpm.h"
#endif

extern int yft_usb_flag;	/* provider: mt6375-charger.ko (witness: int) */

/* Stock .symtab has this GLOBAL but the module exports nothing, so the vendor
 * tree declared it in extcon-mtk-usb.h without EXPORT_SYMBOL. That header is
 * not present in this workspace, so declare it here instead. */
int get_otg_select_gpio(void);

static const unsigned int usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

struct mtk_extcon_info {
	struct device *dev;
	struct extcon_dev *edev;
	struct usb_role_switch *role_sw;
	unsigned int c_role; /* current data role */
	struct workqueue_struct *extcon_wq;
	struct regulator *vbus;
	unsigned int vbus_vol;
	unsigned int vbus_cur;
	bool vbus_on;
	struct power_supply *usb_psy;
	struct notifier_block psy_nb;
	struct delayed_work wq_psy;
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	struct tcpc_device *tcpc_dev;
	struct notifier_block tcpc_nb;
#endif
	bool bypss_typec_sink;
	/* id gpio */
	struct gpio_desc *id_gpiod;
	int id_irq;
	struct delayed_work wq_detcable;
	unsigned int vbus_limit_cur;
	bool vbus_cur_inlimit;

	/* ---- Ulefone/Usmart YFT additions ------------------------------
	 * Stock .bss objects (readelf -sW, sizes exact):
	 *   yft_otg_select_gpio_num 4B  yft_uvc_switch_gpio_num 4B
	 *   yft_uvc_vbus_gpio_num   4B  uvc_insert_status       1B
	 *   yft_extcon              8B  (module-global info pointer)
	 * DT contract, all of them present in the merged stock DTB:
	 *   extcon-usb { id-gpios, otg_gpio_select, uvc_switch_gpio,
	 *                uvc_vbus_gpio, vbus-supply, vbus-voltage,
	 *                vbus-current, vbus-limit-current, charger, tcpc,
	 *                mediatek,bypss-typec-sink, mediatek,u2,
	 *                port/endpoint -> mtu3 usb0 }
	 * GPIO specs are <0x89 pin 0x00> i.e. &eint_pingroup pin GPIOD_IN:
	 *   id-gpios = 0x1c(28)  otg_gpio_select = 0x25(37)
	 *   uvc_switch_gpio = 0x37(55)  uvc_vbus_gpio = 0x98(152)
	 * which reproduces the audited "GPIO 28 IRQ / GPIOs 55+152 / GPIO 37
	 * plus OTG regulator" ownership table exactly.
	 * ------------------------------------------------------------- */
	struct gpio_desc *yft_otg_desc;		/* otg_gpio_select, 37 */
	int yft_otg_gpio_num;
	struct gpio_desc *uvc_switch_desc;	/* uvc_switch_gpio, 55 */
	struct gpio_desc *uvc_vbus_desc;	/* uvc_vbus_gpio, 152 */
	int yft_tiny2c_mode;
};

struct usb_role_info {
	struct mtk_extcon_info *extcon;
	struct delayed_work dwork;
	unsigned int d_role; /* desire data role */
};

/* [PROVEN_EXACT] donor v17 shape; stock update_role is 284B with exactly the
 * donor's 6 extcon_set_state_sync() branches + usb_role_switch_set_role + kfree
 * (llvm-mc: 6 x extcon_set_state_sync, 1 x usb_role_switch_set_role,
 * 1 x kfree, format "cur_dr(%d) new_dr(%d)\n").
 */
static void mtk_usb_extcon_update_role(struct work_struct *work)
{
	struct usb_role_info *role = container_of(to_delayed_work(work),
					struct usb_role_info, dwork);
	struct mtk_extcon_info *extcon = role->extcon;
	unsigned int cur_dr, new_dr;

	cur_dr = extcon->c_role;
	new_dr = role->d_role;

	dev_info(extcon->dev, "cur_dr(%d) new_dr(%d)\n", cur_dr, new_dr);

	/* none -> device */
	if (cur_dr == USB_ROLE_NONE && new_dr == USB_ROLE_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, true);
	/* none -> host */
	} else if (cur_dr == USB_ROLE_NONE && new_dr == USB_ROLE_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, true);
	/* device -> none */
	} else if (cur_dr == USB_ROLE_DEVICE && new_dr == USB_ROLE_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
	/* host -> none */
	} else if (cur_dr == USB_ROLE_HOST && new_dr == USB_ROLE_NONE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
	/* device -> host */
	} else if (cur_dr == USB_ROLE_DEVICE && new_dr == USB_ROLE_HOST) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB, false);
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, true);
	/* host -> device */
	} else if (cur_dr == USB_ROLE_HOST && new_dr == USB_ROLE_DEVICE) {
		extcon_set_state_sync(extcon->edev, EXTCON_USB_HOST, false);
		extcon_set_state_sync(extcon->edev, EXTCON_USB, true);
	}

	/* usb role switch */
	if (extcon->role_sw)
		usb_role_switch_set_role(extcon->role_sw, new_dr);

	extcon->c_role = new_dr;
	kfree(role);
}

/* [PROVEN_EXACT] 168B; kzalloc(sizeof(*role_info)) via kmalloc_trace,
 * INIT_DELAYED_WORK (init_timer_key), queue_delayed_work_on. */
static int mtk_usb_extcon_set_role(struct mtk_extcon_info *extcon,
				   unsigned int role)
{
	struct usb_role_info *role_info;

	/* create and prepare worker */
	role_info = kzalloc(sizeof(*role_info), GFP_ATOMIC);
	if (!role_info)
		return -ENOMEM;

	INIT_DELAYED_WORK(&role_info->dwork, mtk_usb_extcon_update_role);

	role_info->extcon = extcon;
	role_info->d_role = role;
	/* issue connection work */
	queue_delayed_work(extcon->extcon_wq, &role_info->dwork, 0);

	return 0;
}

/* ---- Ulefone/Usmart YFT GPIO plumbing [PROVEN_STRUCTURE] -----------------
 * Both globals are 4-byte .bss ints; every log line below is a byte-exact
 * string from stock .rodata.str1.1, including the vendor's own typos
 * ("bypss", "cannnot", "ser chg_online = 1") and the stale 'mediatek,usb3'
 * mention in get_otg_select_gpio()'s failure path, which is direct evidence
 * this code was forked from a usb3-extcon driver.
 * ------------------------------------------------------------------------ */
static int yft_otg_select_gpio_num;
static int yft_uvc_switch_gpio_num;
static int yft_uvc_vbus_gpio_num;
static bool uvc_insert_status;

static int yft_parse_dts(struct device_node *np, const char *name, int *gpio_num)
{
	struct gpio_desc *desc;
	enum of_gpio_flags flags;
	int gpio;

	pr_info("%s: YFT_OTG_yft_parse_dts \n", __func__);

	gpio = of_get_named_gpio_flags(np, name, 0, &flags);
	/* Two distinct failure literals exist in stock .rodata -- "of_get_named_gpio
	 * fail." and "get gpio num fail." -- so the helper distinguishes the DT
	 * lookup from the validity check (assignment INFERRED, both literals
	 * PROVEN).
	 */
	if (gpio < 0) {
		pr_info("%s: YFT_OTG_of_get_named_gpio fail. \n", __func__);
		return gpio;
	}
	if (!gpio_is_valid(gpio)) {
		pr_info("%s: YFT_OTG_get gpio num fail. \n", __func__);
		return -ENODEV;
	}
	pr_info("%s: YFT_OTG_of_get_named_gpio GPIO is %d.\n", __func__, gpio);

	desc = gpio_to_desc(gpio);
	if (!desc) {
		pr_info("%s: YFT_OTG_gpio_desc is null.\n", __func__);
		return -ENODEV;
	}
	pr_info("%s: YFT_OTG_gpio_desc is not null.\n", __func__);
	pr_info("%s: YFT_OTG_gpio number %d is valid. \n", __func__, gpio);

	if (gpio_request(gpio, name)) {
		pr_info("%s: YFT_OTG_gpio_request fail. \n", __func__);
		return -EBUSY;
	}
	*gpio_num = gpio;

	/* No gpio_free() on the failure path: stock's import set has gpio_request
	 * but no gpio_free, so the vendor helper leaks the pin on error. */
	if (gpiod_direction_output_raw(desc, gpiod_get_raw_value(desc))) {
		pr_info("%s: YFT_OTG_gpio_direction_output failed. \n",
			__func__);
		return -EIO;
	}
	gpiod_set_raw_value(desc, 0);
	pr_info("%s: YFT_OTG_gpio_get_value =%d. \n", __func__,
		gpiod_get_raw_value(desc));

	return 0;
}

/* [PROVEN_STRUCTURE] 160B. Resolves the extcon-usb node itself, then pulls
 * `otg_gpio_select` (GPIO 37) out of it. The node lookup is by compatible, not
 * by the platform device, because this runs for the mt6375-charger OTG path
 * too -- see the 'mediatek,usb3' log mismatch below (forked from usb3 extcon). */
int get_otg_select_gpio(void)
{
	struct device_node *np;

	pr_info("%s: YFT_OTG_enter. \n", __func__);

	np = of_find_compatible_node(NULL, NULL, "mediatek,extcon-usb");
	if (!np) {
		pr_info("%s: YFT_OTG_cannot get the node: 'mediatek,usb3'.\n",
			__func__);
		return 0;
	}

	if (yft_parse_dts(np, "otg_gpio_select", &yft_otg_select_gpio_num))
		pr_info("%s: YFT_OTG_yft_parse_dts 1 fail. \n", __func__);

	/* "YFT_OTG_get_otg_select_gpio failed." and its '=%d' twin are stock-only
	 * until proven otherwise: the numbered form belongs to this helper's own
	 * validity check, the bare form to the caller in probe (INFERRED).
	 */
	if (!gpio_is_valid(yft_otg_select_gpio_num)) {
		pr_info("YFT_OTG_get_otg_select_gpio failed. yft_otg_select_gpio_num=%d\n",
			yft_otg_select_gpio_num);
		pr_info("%s: YFT_OTG_end. \n", __func__);
		of_node_put(np);
		return -ENODEV;
	}

	pr_info("%s: YFT_OTG_end. \n", __func__);

	of_node_put(np);

	return yft_otg_select_gpio_num;
}
/* TODO(export): deliberately NOT exported. Stock .symtab marks this GLOBAL and
 * defined (0x4, 160B) -- so the vendor source declares it non-static, most
 * likely in extcon-mtk-usb.h for the charger TU -- yet the module has zero
 * __ksymtab entries (inventory export_count = 0), so no EXPORT_SYMBOL exists.
 * If a sibling LieppOS module needs it at runtime, that provider must export a
 * real symbol instead of relying on this one.
 */

/* [PROVEN_EXACT] donor v17, 216B: two power_supply_get_property() calls
 * (ONLINE then USB_TYPE) and "online=%d, type=%d\n". */
static bool usb_is_online(struct mtk_extcon_info *extcon)
{
	union power_supply_propval pval;
	union power_supply_propval tval;
	int ret;

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_ONLINE, &pval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get online prop\n");
		return false;
	}

	ret = power_supply_get_property(extcon->usb_psy,
				POWER_SUPPLY_PROP_USB_TYPE, &tval);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to get usb type\n");
		return false;
	}

	dev_info(extcon->dev, "online=%d, type=%d\n", pval.intval, tval.intval);
	if (pval.intval && (tval.intval == POWER_SUPPLY_USB_TYPE_SDP ||
			tval.intval == POWER_SUPPLY_USB_TYPE_CDP))
		return true;
	else
		return false;
}

/* [PROVEN_EXACT] donor v17, 364B. */
static void mtk_usb_extcon_psy_detector(struct work_struct *work)
{
	struct mtk_extcon_info *extcon = container_of(to_delayed_work(work),
		struct mtk_extcon_info, wq_psy);

	/* Workaround for PR_SWAP, IF tcpc_dev, then do not switch role. */
	/* Since we will set USB to none when type-c plug out */
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	if (extcon->tcpc_dev) {
		if (usb_is_online(extcon) && extcon->c_role == USB_ROLE_NONE)
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
	} else
#endif
	{
		if (usb_is_online(extcon))
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		else
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
	}
}

/* [PROVEN_EXACT] donor v17, 68B. */
static int mtk_usb_extcon_psy_notifier(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct mtk_extcon_info *extcon = container_of(nb,
					struct mtk_extcon_info, psy_nb);

	if (event != PSY_EVENT_PROP_CHANGED || psy != extcon->usb_psy)
		return NOTIFY_DONE;

	queue_delayed_work(system_power_efficient_wq, &extcon->wq_psy, 0);

	return NOTIFY_DONE;
}

static int mtk_usb_extcon_psy_init(struct mtk_extcon_info *extcon)
{
	int ret = 0;
	struct device *dev = extcon->dev;

	if (!of_property_read_bool(dev->of_node, "charger")) {
		ret = -EINVAL;
		goto fail;
	}

	extcon->usb_psy = devm_power_supply_get_by_phandle(dev, "charger");
	if (IS_ERR_OR_NULL(extcon->usb_psy)) {
		/* try to get by name */
		extcon->usb_psy = power_supply_get_by_name("primary_chg");
		if (IS_ERR_OR_NULL(extcon->usb_psy)) {
			dev_err(dev, "fail to get usb_psy\n");
			ret = -EINVAL;
			goto fail;
		}
	}

	INIT_DELAYED_WORK(&extcon->wq_psy, mtk_usb_extcon_psy_detector);

	extcon->psy_nb.notifier_call = mtk_usb_extcon_psy_notifier;
	ret = power_supply_reg_notifier(&extcon->psy_nb);
	if (ret)
		dev_err(dev, "fail to register notifer\n");
fail:
	return ret;
}

/* [PROVEN_EXACT] donor v17 set_vbus body + Ulefone delta: the stock build also
 * drives the OTG-select GPIO low on the way out, logging
 * "[%s] YFT_OTG_yft_otg_gpio set_low" with __func__ == "mtk_usb_extcon_set_vbus"
 * (observed in mtk_usb_extcon_shutdown's inline expansion). */
static int mtk_usb_extcon_set_vbus(struct mtk_extcon_info *extcon, bool is_on)
{
	struct regulator *vbus = extcon->vbus;
	struct device *dev = extcon->dev;
	int ret;

	/* vbus is optional */
	if (!vbus || extcon->vbus_on == is_on)
		return 0;

	dev_info(dev, "vbus turn %s\n", is_on ? "on" : "off");

	if (is_on) {
		if (extcon->vbus_vol) {
			ret = regulator_set_voltage(vbus, extcon->vbus_vol,
						   extcon->vbus_vol);
			if (ret) {
				dev_err(dev, "vbus regulator set voltage failed\n");
				return ret;
			}
		}

		if (extcon->vbus_cur) {
			ret = regulator_set_current_limit(vbus, extcon->vbus_cur,
							  extcon->vbus_cur);
			if (ret) {
				dev_err(dev, "vbus regulator set current failed\n");
				return ret;
			}
		}

		ret = regulator_enable(vbus);
		if (ret) {
			dev_info(dev, "vbus regulator enable failed\n");
			return ret;
		}

		/* Ulefone: OTG select line high alongside the boost. The stock
		 * image carries exactly one "[%s] YFT_OTG_yft_otg_gpio set_high"
		 * literal next to a gpiod_set_raw_value(), against the two
		 * set_low sites in the same module; this is the only enable path
		 * in the driver, so the log belongs here (placement INFERRED,
		 * existence PROVEN by oracle/strings.txt).
		 */
		if (extcon->yft_otg_desc) {
			gpiod_set_raw_value(extcon->yft_otg_desc, 1);
			pr_info("[%s] YFT_OTG_yft_otg_gpio set_high\n", __func__);
		}
	} else {
		regulator_disable(vbus);
		if (extcon->yft_otg_desc) {
			gpiod_set_raw_value(extcon->yft_otg_desc, 0);
			pr_info("[%s] YFT_OTG_yft_otg_gpio set_low\n", __func__);
		}
		/* Restore to default state */
		extcon->vbus_cur_inlimit = 0;
	}

	extcon->vbus_on = is_on;

	return 0;
}

/* [PROVEN_EXACT] dev_attr_vbus_limit_cur (.data, 32B), 80B body. */
static ssize_t vbus_limit_cur_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mtk_extcon_info *extcon = dev_get_drvdata(dev);
	struct regulator *vbus = extcon->vbus;

	if (!vbus)
		return sprintf(buf, "0");
	else
		return sprintf(buf, "%d\n", extcon->vbus_cur_inlimit);
}

/* [PROVEN_EXACT] 264B: kstrtoint 0/1 gate, then regulator_set_current_limit
 * between vbus_limit_cur and vbus_cur. */
static ssize_t vbus_limit_cur_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mtk_extcon_info *extcon = dev_get_drvdata(dev);
	struct regulator *vbus = extcon->vbus;
	int ret, in_limit = 0;
	unsigned int vbus_cur = 0;

	/* Check whether we have vbus instance */
	if (!vbus) {
		dev_info(dev, "No vbus instance\n");
		return -EOPNOTSUPP;
	}
	if ((kstrtoint(buf, 10, &in_limit) != 0) ||
	    (in_limit != 1 && in_limit != 0)) {
		/* stock carries a single shared "Invalid input" literal, used by
		 * both store paths, so no __func__ prefix here.
		 */
		dev_info(dev, "Invalid input\n");
		return -EINVAL;
	}

	extcon->vbus_cur_inlimit = (in_limit == 1);
	/* Only operate while vbus is on */
	if (extcon->vbus_on) {
		if (extcon->vbus_cur_inlimit && extcon->vbus_limit_cur)
			vbus_cur = extcon->vbus_limit_cur;
		else if (!extcon->vbus_cur_inlimit && extcon->vbus_cur)
			vbus_cur = extcon->vbus_cur;
	}
	if (vbus_cur) {
		ret = regulator_set_current_limit(vbus, vbus_cur, vbus_cur);
		if (ret) {
			dev_info(dev, "vbus regulator set current failed\n");
			return -EIO;
		}
	} else
		dev_info(dev, "Do not change current\n");

	return count;
}

static DEVICE_ATTR_RW(vbus_limit_cur);

static int mtk_usb_extcon_vbus_init(struct mtk_extcon_info *extcon)
{
	int ret = 0;
	struct device *dev = extcon->dev;

	if (!of_property_read_bool(dev->of_node, "vbus-supply")) {
		ret = -EINVAL;
		goto fail;
	}

	extcon->vbus = devm_regulator_get_exclusive(dev, "vbus");
	if (IS_ERR(extcon->vbus)) {
		/* try to get by name */
		extcon->vbus = devm_regulator_get_exclusive(dev, "usb-otg-vbus");
		if (IS_ERR(extcon->vbus)) {
			dev_err(dev, "failed to get vbus\n");
			ret = PTR_ERR(extcon->vbus);
			extcon->vbus = NULL;
			goto fail;
		}
	}

	/* sync vbus state */
	extcon->vbus_on = regulator_is_enabled(extcon->vbus);
	dev_info(dev, "vbus is %s\n", extcon->vbus_on ? "on" : "off");

	if (!of_property_read_u32(dev->of_node, "vbus-voltage", &extcon->vbus_vol))
		dev_info(dev, "vbus-voltage=%d", extcon->vbus_vol);

	if (!of_property_read_u32(dev->of_node, "vbus-current", &extcon->vbus_cur))
		dev_info(dev, "vbus-current=%d", extcon->vbus_cur);

	if (!of_property_read_u32(dev->of_node, "vbus-limit-current",
				  &extcon->vbus_limit_cur)) {
		dev_info(dev, "vbus limited current=%d", extcon->vbus_limit_cur);
		extcon->vbus_cur_inlimit = 0;
		ret = device_create_file(dev, &dev_attr_vbus_limit_cur);
		if (ret)
			dev_info(dev, "failed to create vbus currnet limit control node\n");
	}

fail:
	return ret;
}

#if IS_ENABLED(CONFIG_TCPC_CLASS)
/* [PROVEN_EXACT] donor v17, 832B; all three event classes present verbatim. */
static int mtk_extcon_tcpc_notifier(struct notifier_block *nb,
				    unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct mtk_extcon_info *extcon =
			container_of(nb, struct mtk_extcon_info, tcpc_nb);
	struct device *dev = extcon->dev;
	bool vbus_on;

	switch (event) {
	case TCP_NOTIFY_SOURCE_VBUS:
		dev_info(dev, "source vbus = %dmv\n", noti->vbus_state.mv);
		vbus_on = (noti->vbus_state.mv) ? true : false;
		mtk_usb_extcon_set_vbus(extcon, vbus_on);
		break;
	case TCP_NOTIFY_TYPEC_STATE:
		dev_info(dev, "old_state=%d, new_state=%d\n",
			 noti->typec_state.old_state,
			 noti->typec_state.new_state);
		if (noti->typec_state.old_state == TYPEC_UNATTACHED &&
		    noti->typec_state.new_state == TYPEC_ATTACHED_SRC) {
			dev_info(dev, "Type-C SRC plug in\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
		} else if (!(extcon->bypss_typec_sink) &&
			   noti->typec_state.old_state == TYPEC_UNATTACHED &&
			   (noti->typec_state.new_state == TYPEC_ATTACHED_SNK ||
			    noti->typec_state.new_state == TYPEC_ATTACHED_NORP_SRC ||
			    noti->typec_state.new_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			    noti->typec_state.new_state == TYPEC_ATTACHED_DBGACC_SNK)) {
			dev_info(dev, "Type-C SINK plug in\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		} else if ((noti->typec_state.old_state == TYPEC_ATTACHED_SRC ||
			    noti->typec_state.old_state == TYPEC_ATTACHED_SNK ||
			    noti->typec_state.old_state == TYPEC_ATTACHED_NORP_SRC ||
			    noti->typec_state.old_state == TYPEC_ATTACHED_CUSTOM_SRC ||
			    noti->typec_state.old_state == TYPEC_ATTACHED_DBGACC_SNK) &&
			   noti->typec_state.new_state == TYPEC_UNATTACHED) {
			dev_info(dev, "Type-C plug out\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
		}
		break;
	case TCP_NOTIFY_DR_SWAP:
		dev_info(dev, "%s dr_swap, new role=%d\n",
			 __func__, noti->swap_state.new_role);
		if (noti->swap_state.new_role == PD_ROLE_UFP &&
		    extcon->c_role != USB_ROLE_DEVICE) {
			dev_info(dev, "switch role to device\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
		} else if (noti->swap_state.new_role == PD_ROLE_DFP &&
			   extcon->c_role != USB_ROLE_HOST) {
			dev_info(dev, "switch role to host\n");
			mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
			mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
		}
		break;
	}

	return NOTIFY_OK;
}

static int mtk_usb_extcon_tcpc_init(struct mtk_extcon_info *extcon)
{
	struct tcpc_device *tcpc_dev;
	struct device_node *np = extcon->dev->of_node;
	const char *tcpc_name;
	int ret;

	ret = of_property_read_string(np, "tcpc", &tcpc_name);
	if (ret < 0)
		return -ENODEV;

	tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (!tcpc_dev) {
		dev_err(extcon->dev, "get tcpc device fail\n");
		return -ENODEV;
	}

	extcon->tcpc_nb.notifier_call = mtk_extcon_tcpc_notifier;
	ret = register_tcp_dev_notifier(tcpc_dev, &extcon->tcpc_nb,
					TCP_NOTIFY_TYPE_USB | TCP_NOTIFY_TYPE_VBUS |
					TCP_NOTIFY_TYPE_MISC);
	if (ret < 0) {
		dev_err(extcon->dev, "register notifer fail\n");
		return -EINVAL;
	}

	extcon->tcpc_dev = tcpc_dev;

	return 0;
}
#endif

/* ---- Ulefone/Usmart UVC (thermal camera) status attribute ----------------
 * dev_attr_uvc_insert_status (.data, 32B) + 84B show. Reads the module-global
 * uvc_insert_status (.bss, 1B) that yft_tiny2c_usb.ko keeps current.
 * [PROVEN_STRUCTURE]
 * ----------------------------------------------------------------------- */
static ssize_t show_uvc_insert_status(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	pr_info("show uvc insert status is %d \n", uvc_insert_status);

	return scnprintf(buf, PAGE_SIZE, "%d\n", uvc_insert_status);
}

/* Hand-rolled instead of DEVICE_ATTR_RO() so the emitted symbol stays
 * dev_attr_uvc_insert_status (stock .data, 32B = attribute 16B + show + store)
 * while the method keeps the stock name show_uvc_insert_status. */
static struct device_attribute dev_attr_uvc_insert_status = {
	.attr = { .name = "uvc_insert_status", .mode = 0444 },
	.show = show_uvc_insert_status,
};

/* ---- ID pin / cable detect --------------------------------------------
 * Donor body + Ulefone delta of four gpiod_set_raw_value() calls (UVC switch +
 * UVC vbus, twice) and the yft_usb_flag gate. Stock is 468B vs donor ~330B;
 * the extra block is exactly
 *   if (yft_usb_flag) { pr_info("[%s] yft_drv ignore usb func for yft_usb_flag\n",
 *                              __func__); return; }
 * placed before the role update -- this is the audited handoff where the
 * thermal camera, not the charger stack, owns the port. [PROVEN_STRUCTURE]
 * ----------------------------------------------------------------------- */
static void mtk_usb_extcon_detect_cable(struct work_struct *work)
{
	struct mtk_extcon_info *extcon = container_of(to_delayed_work(work),
		struct mtk_extcon_info, wq_detcable);
	int id;

	/* check ID and update cable state */
	id = extcon->id_gpiod ?
		gpiod_get_value_cansleep(extcon->id_gpiod) : 1;

	/* at first we clean states which are no longer active */
	if (id) {
		/* Ulefone: camera is out of the saddle -> UVC path off */
		if (extcon->uvc_switch_desc)
			gpiod_set_raw_value(extcon->uvc_switch_desc, 0);
		if (extcon->uvc_vbus_desc)
			gpiod_set_raw_value(extcon->uvc_vbus_desc, 0);
		uvc_insert_status = 0;

		mtk_usb_extcon_set_vbus(extcon, false);
		mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
	} else {
		/* Ulefone: if the thermal camera owns the port, the charger
		 * stack must not take VBUS/role back. */
		if (yft_usb_flag) {
			pr_info("[%s] yft_drv ignore usb func for yft_usb_flag\n",
				__func__);
			return;
		}

		if (extcon->uvc_switch_desc)
			gpiod_set_raw_value(extcon->uvc_switch_desc, 1);
		if (extcon->uvc_vbus_desc)
			gpiod_set_raw_value(extcon->uvc_vbus_desc, 1);
		uvc_insert_status = 1;

		mtk_usb_extcon_set_vbus(extcon, true);
		mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
	}
}

/* [PROVEN_EXACT] donor v17, 52B. */
static irqreturn_t mtk_usb_idpin_handle(int irq, void *dev_id)
{
	struct mtk_extcon_info *extcon = dev_id;

	/* issue detection work */
	queue_delayed_work(system_power_efficient_wq, &extcon->wq_detcable, 0);

	return IRQ_HANDLED;
}

static int mtk_usb_extcon_id_pin_init(struct mtk_extcon_info *extcon)
{
	int ret = 0;
	int id;

	extcon->id_gpiod = devm_gpiod_get(extcon->dev, "id", GPIOD_IN);
	if (!extcon->id_gpiod || IS_ERR(extcon->id_gpiod)) {
		dev_info(extcon->dev, "failed to get id gpio\n");
		return -ENODEV;
	}

	extcon->id_irq = gpiod_to_irq(extcon->id_gpiod);
	if (extcon->id_irq < 0) {
		dev_info(extcon->dev, "failed to get ID IRQ\n");
		return extcon->id_irq;
	}

	INIT_DELAYED_WORK(&extcon->wq_detcable, mtk_usb_extcon_detect_cable);

	ret = devm_request_threaded_irq(extcon->dev, extcon->id_irq, NULL,
					mtk_usb_idpin_handle,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT, dev_name(extcon->dev), extcon);
	if (ret < 0) {
		dev_info(extcon->dev, "failed to request handler for ID IRQ\n");
		return ret;
	}

	/* get id pin value when boot on */
	id = extcon->id_gpiod ? gpiod_get_value_cansleep(extcon->id_gpiod) : 1;
	dev_info(extcon->dev, "id value : %d\n", id);
	if (!id) {
		mtk_usb_extcon_set_vbus(extcon, true);
		mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
	}

	return 0;
}

#if IS_ENABLED(CONFIG_TCPC_CLASS)
/* [PROVEN_EXACT] donor v17 procfs pair (248B / 48B): /proc/mtk_typec/
 * smt_u2_cc_mode -> tcpm_inquire_remote_cc() -> "0" if either CC is open or
 * DRP-toggling else "1". */
#define PROC_FILE_SMT "mtk_typec"
#define FILE_SMT_U2_CC_MODE "smt_u2_cc_mode"

static int usb_cc_smt_procfs_show(struct seq_file *s, void *unused)
{
	struct mtk_extcon_info *extcon = s->private;
	struct device_node *np = extcon->dev->of_node;
	const char *tcpc_name;
	uint8_t cc1, cc2;
	int ret;

	ret = of_property_read_string(np, "tcpc", &tcpc_name);
	if (ret < 0)
		return -ENODEV;

	extcon->tcpc_dev = tcpc_dev_get_by_name(tcpc_name);
	if (!extcon->tcpc_dev)
		return -ENODEV;

	tcpm_inquire_remote_cc(extcon->tcpc_dev, &cc1, &cc2, false);
	dev_info(extcon->dev, "cc1=%d, cc2=%d\n", cc1, cc2);

	if (cc1 == TYPEC_CC_VOLT_OPEN || cc1 == TYPEC_CC_DRP_TOGGLING)
		seq_puts(s, "0\n");
	else if (cc2 == TYPEC_CC_VOLT_OPEN || cc2 == TYPEC_CC_DRP_TOGGLING)
		seq_puts(s, "0\n");
	else
		seq_puts(s, "1\n");

	return 0;
}

static int usb_cc_smt_procfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, usb_cc_smt_procfs_show, pde_data(inode));
}

static const struct proc_ops usb_cc_smt_procfs_fops = {
	.proc_open = usb_cc_smt_procfs_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int mtk_usb_extcon_procfs_init(struct mtk_extcon_info *extcon)
{
	struct proc_dir_entry *file, *root;
	int ret = 0;

	root = proc_mkdir(PROC_FILE_SMT, NULL);
	if (!root) {
		dev_info(extcon->dev, "fail creating proc dir: %s\n",
			 PROC_FILE_SMT);
		ret = -ENOMEM;
		goto error;
	}

	file = proc_create_data(FILE_SMT_U2_CC_MODE, 0400, root,
				&usb_cc_smt_procfs_fops, extcon);
	if (!file) {
		dev_info(extcon->dev, "fail creating proc file: %s\n",
			 FILE_SMT_U2_CC_MODE);
		ret = -ENOMEM;
		goto error;
	}

	dev_info(extcon->dev, "success creating proc file: %s\n",
		 FILE_SMT_U2_CC_MODE);

error:
	dev_info(extcon->dev, "%s ret:%d\n", __func__, ret);
	return ret;
}
#endif

/* ---- Ulefone/Usmart tiny2c mode class attribute ------------------------
 * class_attr_tiny2c_mode (.data, 64B = struct class_attribute) installed with
 * __class_create("yft_extcon") + class_create_file_ns() -- all three imports
 * are present in stock and none of them survives in the donor, which instead
 * exports extcon_usb_mode_switch() under CONFIG_NT_USB_TS. Stock symtab has no
 * such export, so Ulefone replaced the in-kernel mode-switch API with this
 * sysfs surface. store() is 312B: sscanf(buf, "%d"), log, then
 * kmalloc_trace + init_timer_key + queue_delayed_work_on, i.e. it queues a
 * delayed work rather than switching synchronously. [RECONSTRUCTED]
 * ----------------------------------------------------------------------- */
static struct mtk_extcon_info *yft_extcon;

struct yft_tiny2c_mode_work {
	struct delayed_work dwork;
	int mode;
};

static void yft_tiny2c_mode_work(struct work_struct *work)
{
	struct yft_tiny2c_mode_work *w = container_of(to_delayed_work(work),
					struct yft_tiny2c_mode_work, dwork);
	struct mtk_extcon_info *extcon = yft_extcon;

	if (!extcon)
		return;

	extcon->yft_tiny2c_mode = w->mode;

	/*
	 * Stock (oracle disassembly of store_tiny2c_mode): mode 1 queues an
	 * update_role request with d_role = USB_ROLE_HOST, anything else with
	 * USB_ROLE_NONE. Nothing else: the uvc switch/vbus GPIOs belong to the
	 * ID-pin (uSmart accessory) path in mtk_usb_extcon_detect_cable and are
	 * not touched here. The internal Tiny2-C thermal camera is a UVC device,
	 * so the controller must be the USB host; requesting DEVICE (as this
	 * reconstruction first did) leaves it as a gadget and the camera never
	 * enumerates.
	 */
	if (w->mode == 1) {
		mtk_usb_extcon_set_role(extcon, USB_ROLE_HOST);
	} else {
		mtk_usb_extcon_set_role(extcon, USB_ROLE_NONE);
		/*
		 * LieppOS: stock stops here, so a PC cable that stayed plugged in
		 * while the camera held the port got no data role back (no adb/MTP
		 * until a replug). Give the port back to the PC the same way
		 * mtk_usb_extcon_psy_detector decides on device mode.
		 */
		if (!IS_ERR_OR_NULL(extcon->usb_psy) && usb_is_online(extcon))
			mtk_usb_extcon_set_role(extcon, USB_ROLE_DEVICE);
	}

	kfree(w);
}

static ssize_t show_tiny2c_mode(struct class *class, struct class_attribute *attr,
				char *buf)
{
	pr_info("[%s] is yft_tiny2c_status=%d \n", __func__,
		yft_extcon ? yft_extcon->yft_tiny2c_mode : 0);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 yft_extcon ? yft_extcon->yft_tiny2c_mode : 0);
}

static ssize_t store_tiny2c_mode(struct class *class, struct class_attribute *attr,
				 const char *buf, size_t count)
{
	struct yft_tiny2c_mode_work *w;
	int mode = 0;

	/* One shared "Invalid input" literal covers both store paths in the stock
	 * image, so no __func__ prefix here.
	 */
	if (sscanf(buf, "%d", &mode) != 1) {
		pr_info("Invalid input\n");
		return -EINVAL;
	}
	pr_info("[%s] yft_tiny2c_status=%d\n", __func__, mode);

	w = kmalloc(sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;

	/* Stock call census for store_tiny2c_mode is exactly
	 * sscanf -> _printk -> kmalloc_trace -> init_timer_key ->
	 * queue_delayed_work_on -> (return), with no kfree: the allocation is
	 * owned by the work item and freed by the handler. init_timer_key is
	 * INIT_DELAYED_WORK's timer initialisation, which rules out plain
	 * INIT_WORK. */
	INIT_DELAYED_WORK(&w->dwork, yft_tiny2c_mode_work);
	w->mode = mode;
	queue_delayed_work(system_power_efficient_wq, &w->dwork, 0);

	return count;
}

/* Hand-rolled rather than CLASS_ATTR_RW(tiny2c_mode) so the emitted symbol is
 * byte-identical to stock .data `class_attr_tiny2c_mode` (64B) while the method
 * names stay the stock show_tiny2c_mode / store_tiny2c_mode. */
static struct class_attribute class_attr_tiny2c_mode = {
	.attr = { .name = "tiny2c_mode", .mode = 0644 },
	.show = show_tiny2c_mode,
	.store = store_tiny2c_mode,
};

/* ---- probe / remove / shutdown ----------------------------------------
 * probe is 2252B against the donor's ~1.6KB: the Ulefone build inlines
 * vbus_init/psy_init/id_pin_init/tcpc_init (clang -O2 + thinlto, single caller
 * each -- consistent with the single __key/.rodata cluster inside the probe
 * body) and adds the YFT GPIO setup plus the yft_extcon class creation.
 * remove is 8B == `return 0`, exactly as in the donor. shutdown is 152B ==
 * donor body (set_vbus(false)) plus the OTG-select GPIO low pulse.
 * [PROVEN_STRUCTURE]
 * ----------------------------------------------------------------------- */
static int mtk_usb_extcon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_extcon_info *extcon;
#if IS_ENABLED(CONFIG_TCPC_CLASS)
	const char *tcpc_name;
#endif
	/* Named exactly like the stock symbol, which lives in .bss as
	 * mtk_usb_extcon_probe.yft_extcon_class (8B) -- i.e. a function-static,
	 * not a module global, so the class pointer is only reachable from
	 * probe. Keep it that way: moving it to file scope would change the
	 * oracle's object census. */
	static struct class *yft_extcon_class;
	int ret;

	extcon = devm_kzalloc(&pdev->dev, sizeof(*extcon), GFP_KERNEL);
	if (!extcon)
		return -ENOMEM;

	extcon->dev = dev;

	/* extcon */
	extcon->edev = devm_extcon_dev_allocate(dev, usb_extcon_cable);
	if (IS_ERR(extcon->edev)) {
		dev_err(dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(dev, extcon->edev);
	if (ret < 0) {
		dev_info(dev, "failed to register extcon device\n");
		return ret;
	}

	/* usb role switch */
	extcon->role_sw = usb_role_switch_get(extcon->dev);
	if (IS_ERR(extcon->role_sw)) {
		dev_err(dev, "failed to get usb role\n");
		return PTR_ERR(extcon->role_sw);
	}

	/* initial usb role */
	if (extcon->role_sw)
		extcon->c_role = USB_ROLE_NONE;

	/* vbus */
	ret = mtk_usb_extcon_vbus_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init vbus\n");

	extcon->bypss_typec_sink =
		of_property_read_bool(dev->of_node, "mediatek,bypss-typec-sink");

#if IS_ENABLED(CONFIG_TCPC_CLASS)
	ret = of_property_read_string(dev->of_node, "tcpc", &tcpc_name);
	if (of_property_read_bool(dev->of_node, "mediatek,u2") && ret == 0 &&
	    strcmp(tcpc_name, "type_c_port0") == 0)
		mtk_usb_extcon_procfs_init(extcon);
#endif

	extcon->extcon_wq = create_singlethread_workqueue("extcon_usb");
	if (!extcon->extcon_wq)
		return -ENOMEM;

	/* get id resources */
	ret = mtk_usb_extcon_id_pin_init(extcon);
	if (ret < 0)
		dev_info(dev, "failed to init id pin\n");

	/* power psy */
	ret = mtk_usb_extcon_psy_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init psy\n");

#if IS_ENABLED(CONFIG_TCPC_CLASS)
	/* tcpc */
	ret = mtk_usb_extcon_tcpc_init(extcon);
	if (ret < 0)
		dev_err(dev, "failed to init tcpc\n");
#endif

	/* ---- Ulefone/Usmart YFT block ----------------------------------
	 * NOTE on the GPIO lookup path: the DT properties are spelled with
	 * underscores (`uvc_switch_gpio`, `uvc_vbus_gpio`, `otg_gpio_select`),
	 * which the gpiod consumer API cannot match -- it only probes
	 * "<con-id>-gpios" / "<con-id>-gpio". The vendor therefore reads these
	 * raw with of_get_named_gpio_flags() (that is exactly what
	 * yft_parse_dts() wraps) and keeps the three resulting numbers in the
	 * three 4-byte .bss globals, then goes through gpio_to_desc() when it
	 * needs a descriptor. Consistent with the import census: devm_gpiod_get
	 * appears once (the "id" pin) while gpio_to_desc recurses through
	 * detect_cable/shutdown.
	 * ---------------------------------------------------------------- */

	/* UVC path switch + power for the FM78100 thermal camera */
	/* Log text below is quoted verbatim from the stock .rodata (grade
	 * PROVEN for the strings, INFERRED for which branch of the two GPIO
	 * blocks emits which). Note the vendor copy-paste: the uvc_vbus path
	 * reports yft_uvc_switch_gpio_num.
	 */
	if (yft_parse_dts(dev->of_node, "uvc_switch_gpio",
			  &yft_uvc_switch_gpio_num))
		pr_info("%s: yft_parse_dts fail. \n", __func__);
	if (gpio_is_valid(yft_uvc_switch_gpio_num))
		extcon->uvc_switch_desc = gpio_to_desc(yft_uvc_switch_gpio_num);
	else
		pr_info("yft_uvc_switch_gpio_num failed. yft_uvc_switch_gpio_num=%d\n",
			yft_uvc_switch_gpio_num);

	if (yft_parse_dts(dev->of_node, "uvc_vbus_gpio",
			  &yft_uvc_vbus_gpio_num))
		pr_info("%s: yft_parse_dts fail. yft_uvc_switch_gpio_num=%d\n",
			__func__, yft_uvc_switch_gpio_num);
	if (gpio_is_valid(yft_uvc_vbus_gpio_num))
		extcon->uvc_vbus_desc = gpio_to_desc(yft_uvc_vbus_gpio_num);
	else
		pr_info("yft_uvc_vbus_gpio_num failed. yft_uvc_vbus_gpio_num=%d\n",
			yft_uvc_vbus_gpio_num);

	ret = device_create_file(dev, &dev_attr_uvc_insert_status);
	if (ret)
		dev_info(dev, "Failed to create uvc sysfs file %d\n", ret);

	/* OTG select line (GPIO 37), shared with the charger OTG path */
	if (get_otg_select_gpio() < 0)
		pr_info("YFT_OTG_get_otg_select_gpio failed.\n");
	extcon->yft_otg_gpio_num = yft_otg_select_gpio_num;
	if (gpio_is_valid(yft_otg_select_gpio_num))
		extcon->yft_otg_desc = gpio_to_desc(yft_otg_select_gpio_num);

	yft_extcon = extcon;

	/* TODO(class): the class name "yft_extcon" comes from stock .rodata, but
	 * the attribute mode (0644) and the fact that the attribute is added at
	 * probe rather than lazily are inferred, not observed.
	 * class_create() expands to __class_create(THIS_MODULE, name, &key),
	 * matching the stock undefined symbol __class_create; the explicit NULL
	 * ns is the 6.1 class_create_file_ns() signature (stock imports it).
	 */
	/* The stock image contains no failure string for either the class_create
	 * or the class_create_file_ns error path, so both only take the NULL /
	 * continue action here; the "yft_tiny2c_mode=%d" literal below is the
	 * remaining unattributed stock log in probe and the tiny2c bootstrap is
	 * the only candidate site (INFERRED).
	 */
	yft_extcon_class = class_create(THIS_MODULE, "yft_extcon");
	if (IS_ERR(yft_extcon_class)) {
		yft_extcon_class = NULL;
	} else {
		ret = class_create_file_ns(yft_extcon_class,
					   &class_attr_tiny2c_mode, NULL);
	}
	pr_info("yft_tiny2c_mode=%d\n", extcon->yft_tiny2c_mode);
	/* ---------------------------------------------------------------- */

	platform_set_drvdata(pdev, extcon);

	return 0;
}

/* [PROVEN_EXACT] 8B == `return 0`. */
static int mtk_usb_extcon_remove(struct platform_device *pdev)
{
	return 0;
}

/* [PROVEN_STRUCTURE] 152B: donor body + YFT OTG GPIO low. */
static void mtk_usb_extcon_shutdown(struct platform_device *pdev)
{
	struct mtk_extcon_info *extcon = platform_get_drvdata(pdev);

	dev_info(extcon->dev, "shutdown\n");

	mtk_usb_extcon_set_vbus(extcon, false);
}

static const struct of_device_id mtk_usb_extcon_of_match[] = {
	{ .compatible = "mediatek,extcon-usb", },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_usb_extcon_of_match);

static struct platform_driver mtk_usb_extcon_driver = {
	.probe		= mtk_usb_extcon_probe,
	.remove		= mtk_usb_extcon_remove,
	.shutdown	= mtk_usb_extcon_shutdown,
	.driver		= {
		.name	= "mtk-extcon-usb",
		.of_match_table = mtk_usb_extcon_of_match,
	},
};

static int __init mtk_usb_extcon_init(void)
{
	return platform_driver_register(&mtk_usb_extcon_driver);
}
late_initcall(mtk_usb_extcon_init);

static void __exit mtk_usb_extcon_exit(void)
{
	platform_driver_unregister(&mtk_usb_extcon_driver);
}
module_exit(mtk_usb_extcon_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek Extcon USB Driver");
