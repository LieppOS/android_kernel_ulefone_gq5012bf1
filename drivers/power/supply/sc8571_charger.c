// SPDX-License-Identifier: GPL-2.0
/*
 * SouthChip SC8571 switched-capacitor direct charger.
 * Reconstructed from the GQ5012BF1 stock module by static analysis only.
 * Stock oracle SHA256: 790065853b875213ee7906bb4c41a5f272106b24a537034a886f12f0153d1d0b
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "charger_class.h"

#define SC8571_DRV_VERSION "1.0.1_G"
#define SC8571_DEVICE_ID 0x41
#define SC8571_MAX_REGISTER 0x42
#define SC8571_ADC_BASE 0x25

enum sc8571_mode {
	SC8571_STANDALONE = 0,
	SC8571_MASTER = 1,
	SC8571_SLAVE = 2,
};

enum sc8571_fields {
	F_VBAT_OVP_DIS,
	F_VBAT_OVP,
	F_VBAT_OVP_ALM_DIS,
	F_VBAT_OVP_ALM,
	F_IBAT_OCP_DIS,
	F_IBAT_OCP,
	F_IBAT_OCP_ALM_DIS,
	F_IBAT_OCP_ALM,
	F_IBUS_UCP_DIS,
	F_VBUS_IN_RANGE_DIS,
	F_VBUS_PD_EN,
	F_VBUS_OVP,
	F_VBUS_OVP_ALM_DIS,
	F_VBUS_OVP_ALM,
	F_IBUS_OCP_DIS,
	F_IBUS_OCP,
	F_TSHUT_DIS,
	F_TDIE_ALM_DIS,
	F_TSBUS_FLT_DIS,
	F_TSBAT_FLT_DIS,
	F_TDIE_ALM,
	F_TSBUS_FLT,
	F_TSBAT_FLT,
	F_VAC1_OVP,
	F_VAC2_OVP,
	F_VAC1_PD_EN,
	F_VAC2_PD_EN,
	F_REG_RST,
	F_OTG_EN,
	F_CHG_EN,
	F_CHARGE_MODE,
	F_ACDRV1_STAT,
	F_ACDRV2_STAT,
	F_FSW_SET,
	F_WD_TIMEOUT,
	F_WD_TIMEOUT_DIS,
	F_IBAT_SNS_R,
	F_SS_TIMEOUT,
	F_IBUS_UCP_FALL_DG,
	F_VOUT_OVP_DIS,
	F_VOUT_OVP,
	F_MS,
	F_CP_SWITCHING_STAT,
	F_VBUS_ERR_HI_STAT,
	F_VBUS_ERR_LO_STAT,
	F_DEVICE_ID,
	F_ADC_EN,
	F_ACDRV_MANUAL_EN,
	F_ACDRV1_EN,
	F_ACDRV2_EN,
	F_SS_TIMEOUT_DIS,
	F_VBUS_OVP_DIS,
	F_PMID2OUT_OVP_DIS,
	F_PMID2OUT_UVP_DIS,
	F_PMID2OUT_UVP,
	F_PMID2OUT_OVP,
	F_PMID2OUT_UVP_FLAG,
	F_PMID2OUT_OVP_FLAG,
	F_MAX_FIELDS,
};

static const struct reg_field sc8571_reg_fields[] = {
	REG_FIELD(0x00, 7, 7), REG_FIELD(0x00, 0, 6),
	REG_FIELD(0x01, 7, 7), REG_FIELD(0x01, 0, 6),
	REG_FIELD(0x02, 7, 7), REG_FIELD(0x02, 0, 6),
	REG_FIELD(0x03, 7, 7), REG_FIELD(0x03, 0, 6),
	REG_FIELD(0x05, 7, 7), REG_FIELD(0x05, 2, 2),
	REG_FIELD(0x06, 7, 7), REG_FIELD(0x06, 0, 6),
	REG_FIELD(0x07, 7, 7), REG_FIELD(0x07, 0, 6),
	REG_FIELD(0x08, 7, 7), REG_FIELD(0x08, 0, 4),
	REG_FIELD(0x0a, 7, 7), REG_FIELD(0x0a, 4, 4),
	REG_FIELD(0x0a, 3, 3), REG_FIELD(0x0a, 2, 2),
	REG_FIELD(0x0b, 0, 7), REG_FIELD(0x0c, 0, 7),
	REG_FIELD(0x0d, 0, 7), REG_FIELD(0x0e, 5, 7),
	REG_FIELD(0x0e, 2, 4), REG_FIELD(0x0e, 1, 1),
	REG_FIELD(0x0e, 0, 0), REG_FIELD(0x0f, 7, 7),
	REG_FIELD(0x0f, 5, 5), REG_FIELD(0x0f, 4, 4),
	REG_FIELD(0x0f, 3, 3), REG_FIELD(0x0f, 1, 1),
	REG_FIELD(0x0f, 0, 0), REG_FIELD(0x10, 5, 7),
	REG_FIELD(0x10, 3, 4), REG_FIELD(0x10, 2, 2),
	REG_FIELD(0x11, 7, 7), REG_FIELD(0x11, 4, 6),
	REG_FIELD(0x11, 2, 3), REG_FIELD(0x12, 7, 7),
	REG_FIELD(0x12, 5, 6), REG_FIELD(0x12, 0, 1),
	REG_FIELD(0x17, 6, 6), REG_FIELD(0x17, 4, 4),
	REG_FIELD(0x17, 3, 3), REG_FIELD(0x22, 0, 7),
	REG_FIELD(0x23, 7, 7), REG_FIELD(0x40, 6, 6),
	REG_FIELD(0x40, 5, 5), REG_FIELD(0x40, 4, 4),
	REG_FIELD(0x41, 7, 7), REG_FIELD(0x41, 6, 6),
	REG_FIELD(0x41, 5, 5), REG_FIELD(0x41, 4, 4),
	REG_FIELD(0x42, 5, 7), REG_FIELD(0x42, 2, 4),
	REG_FIELD(0x42, 1, 1), REG_FIELD(0x42, 0, 0),
};

struct sc8571_cfg {
	u32 vbat_ovp_dis, vbat_ovp, vbat_ovp_alm_dis, vbat_ovp_alm;
	u32 ibat_ocp_dis, ibat_ocp, ibat_ocp_alm_dis, ibat_ocp_alm;
	u32 ibus_ucp_dis, vbus_in_range_dis, vbus_pd_en, vbus_ovp;
	u32 vbus_ovp_alm_dis, vbus_ovp_alm, ibus_ocp_dis, ibus_ocp;
	u32 tshut_dis, tsbus_flt_dis, tsbat_flt_dis, tdie_alm;
	u32 tsbus_flt, tsbat_flt, vac1_ovp, vac2_ovp;
	u32 vac1_pd_en, vac2_pd_en, fsw_set, wd_timeout;
	u32 wd_timeout_dis, ibat_sns_r, ss_timeout, ibus_ucp_fall_dg;
	u32 vout_ovp_dis, vout_ovp, ss_timeout_dis, vbus_ovp_dis;
	u32 pmid2out_ovp_dis, pmid2out_ovp;
	u32 pmid2out_uvp_dis, pmid2out_uvp;
};

static_assert(sizeof(struct charger_ops) == 648);

struct sc8571_chip {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct regmap_field *fields[F_MAX_FIELDS];
	struct sc8571_cfg cfg;
	int irq_gpio;
	int irq;
	u32 mode;
	bool charge_enabled;
	int reserved_adc;
	int vbus_volt;
	int ibus_curr;
	int vbat_volt;
	int ibat_curr;
	int tdie;
	struct charger_device *chg_dev;
	const char *chg_dev_name;
	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *psy;
};
static_assert(sizeof(struct sc8571_chip) == 872);

static const struct regmap_config sc8571_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = SC8571_MAX_REGISTER,
};

static char sc8571_buf[15];
static int sc8571_conunt;
static struct class *sc8571_fg_class;
static char sc8571_charge_id[30];

static __always_inline int sc8571_field_write(struct sc8571_chip *sc,
					       int field, u32 val)
{
	int ret = regmap_field_write(sc->fields[field], val);

	if (ret < 0)
		dev_err(sc->dev, "sc8571 read field %d fail: %d\n", field, ret);
	return ret;
}

static __always_inline int sc8571_field_read(struct sc8571_chip *sc,
					      int field, u32 *val)
{
	int ret = regmap_field_read(sc->fields[field], val);

	if (ret < 0)
		dev_err(sc->dev, "sc8571 read field %d fail: %d\n", field, ret);
	return ret;
}

static __always_inline int sc8571_dump_reg(struct sc8571_chip *sc)
{
	unsigned int val = 0;
	int ret = 0;
	u8 addr;

	for (addr = 0; addr <= SC8571_MAX_REGISTER; addr++) {
		ret = regmap_read(sc->regmap, addr, &val);
		dev_err(sc->dev, "%s reg[0x%02x] = 0x%02x\n",
			"sc8571_dump_reg", addr, val);
	}
	return ret;
}

struct sc8571_dt_prop { const char *name; u32 *data; };
struct sc8571_init_data { u32 field; u32 val; };

static int sc8571_parse_dt(struct sc8571_chip *sc, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct sc8571_dt_prop props[] = {
		{ "sc,sc8571,vbat-ovp-dis", &sc->cfg.vbat_ovp_dis },
		{ "sc,sc8571,vbat-ovp", &sc->cfg.vbat_ovp },
		{ "sc,sc8571,vbat-ovp-alm-dis", &sc->cfg.vbat_ovp_alm_dis },
		{ "sc,sc8571,vbat-ovp-alm", &sc->cfg.vbat_ovp_alm },
		{ "sc,sc8571,ibat-ocp-dis", &sc->cfg.ibat_ocp_dis },
		{ "sc,sc8571,ibat-ocp", &sc->cfg.ibat_ocp },
		{ "sc,sc8571,ibat-ocp-alm-dis", &sc->cfg.ibat_ocp_alm_dis },
		{ "sc,sc8571,ibat-ocp-alm", &sc->cfg.ibat_ocp_alm },
		{ "sc,sc8571,ibus-ucp-dis", &sc->cfg.ibus_ucp_dis },
		{ "sc,sc8571,vbus-in-range-dis", &sc->cfg.vbus_in_range_dis },
		{ "sc,sc8571,vbus-pd-en", &sc->cfg.vbus_pd_en },
		{ "sc,sc8571,vbus-ovp", &sc->cfg.vbus_ovp },
		{ "sc,sc8571,vbus-ovp-alm-dis", &sc->cfg.vbus_ovp_alm_dis },
		{ "sc,sc8571,vbus-ovp-alm", &sc->cfg.vbus_ovp_alm },
		{ "sc,sc8571,ibus-ocp-dis", &sc->cfg.ibus_ocp_dis },
		{ "sc,sc8571,ibus-ocp", &sc->cfg.ibus_ocp },
		{ "sc,sc8571,tshut-dis", &sc->cfg.tshut_dis },
		{ "sc,sc8571,tsbus-flt-dis", &sc->cfg.tsbus_flt_dis },
		{ "sc,sc8571,tsbat-flt-dis", &sc->cfg.tsbat_flt_dis },
		{ "sc,sc8571,tdie-alm", &sc->cfg.tdie_alm },
		{ "sc,sc8571,tsbus-flt", &sc->cfg.tsbus_flt },
		{ "sc,sc8571,tsbat-flt", &sc->cfg.tsbat_flt },
		{ "sc,sc8571,vac1-ovp", &sc->cfg.vac1_ovp },
		{ "sc,sc8571,vac2-ovp", &sc->cfg.vac2_ovp },
		{ "sc,sc8571,vac1-pd-en", &sc->cfg.vac1_pd_en },
		{ "sc,sc8571,vac2-pd-en", &sc->cfg.vac2_pd_en },
		{ "sc,sc8571,fsw-set", &sc->cfg.fsw_set },
		{ "sc,sc8571,wd-timeout", &sc->cfg.wd_timeout },
		{ "sc,sc8571,wd-timeout-dis", &sc->cfg.wd_timeout_dis },
		{ "sc,sc8571,ibat-sns-r", &sc->cfg.ibat_sns_r },
		{ "sc,sc8571,ss-timeout-dis", &sc->cfg.ss_timeout_dis },
		{ "sc,sc8571,ss-timeout", &sc->cfg.ss_timeout },
		{ "sc,sc8571,ibus-ucp-fall-dg", &sc->cfg.ibus_ucp_fall_dg },
		{ "sc,sc8571,vout-ovp-dis", &sc->cfg.vout_ovp_dis },
		{ "sc,sc8571,vout-ovp", &sc->cfg.vout_ovp },
		{ "sc,sc8571,vbus-ovp-dis", &sc->cfg.vbus_ovp_dis },
		{ "sc,sc8571,pmid2out-ovp-dis", &sc->cfg.pmid2out_ovp_dis },
		{ "sc,sc8571,pmid2out-ovp", &sc->cfg.pmid2out_ovp },
		{ "sc,sc8571,pmid2out-uvp-dis", &sc->cfg.pmid2out_uvp_dis },
		{ "sc,sc8571,pmid2out-uvp", &sc->cfg.pmid2out_uvp },
	};
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = of_property_read_u32(np, props[i].name, props[i].data);
		if (ret < 0) {
			dev_err(sc->dev, "can not read %s \n", props[i].name);
			return ret;
		}
	}
	sc->irq_gpio = of_get_named_gpio(np, "sc8571,intr_gpio", 0);
	if (!gpio_is_valid(sc->irq_gpio)) {
		dev_err(sc->dev, "fail to valid gpio : %d\n", sc->irq_gpio);
		return -EINVAL;
	}
	ret = of_property_read_string(np, "charger_name", &sc->chg_dev_name);
	if (ret < 0) {
		sc->chg_dev_name = "charger";
		dev_err(sc->dev, "no charger name\n");
	}
	dev_err(sc->dev, "gftk chg_dev_name : %s\n", sc->chg_dev_name);
	return 0;
}

static int sc8571_init_device(struct sc8571_chip *sc)
{
	struct sc8571_init_data d[] = {
		{ F_VBAT_OVP_DIS, sc->cfg.vbat_ovp_dis },
		{ F_VBAT_OVP, sc->cfg.vbat_ovp },
		{ F_VBAT_OVP_ALM_DIS, sc->cfg.vbat_ovp_alm_dis },
		{ F_VBAT_OVP_ALM, sc->cfg.vbat_ovp_alm },
		{ F_IBAT_OCP_DIS, sc->cfg.ibat_ocp_dis },
		{ F_IBAT_OCP, sc->cfg.ibat_ocp },
		{ F_IBAT_OCP_ALM_DIS, sc->cfg.ibat_ocp_alm_dis },
		{ F_IBAT_OCP_ALM, sc->cfg.ibat_ocp_alm },
		{ F_IBUS_UCP_DIS, sc->cfg.ibus_ucp_dis },
		{ F_VBUS_IN_RANGE_DIS, sc->cfg.vbus_in_range_dis },
		{ F_VBUS_PD_EN, sc->cfg.vbus_pd_en },
		{ F_VBUS_OVP, sc->cfg.vbus_ovp },
		{ F_VBUS_OVP_ALM_DIS, sc->cfg.vbus_ovp_alm_dis },
		{ F_VBUS_OVP_ALM, sc->cfg.vbus_ovp_alm },
		{ F_IBUS_OCP_DIS, sc->cfg.ibus_ocp_dis },
		{ F_IBUS_OCP, sc->cfg.ibus_ocp },
		{ F_TSHUT_DIS, sc->cfg.tshut_dis },
		{ F_TSBUS_FLT_DIS, sc->cfg.tsbus_flt_dis },
		{ F_TSBAT_FLT_DIS, sc->cfg.tsbat_flt_dis },
		{ F_TDIE_ALM, sc->cfg.tdie_alm },
		{ F_TSBUS_FLT, sc->cfg.tsbus_flt },
		{ F_TSBAT_FLT, sc->cfg.tsbat_flt },
		{ F_VAC1_OVP, sc->cfg.vac1_ovp },
		{ F_VAC2_OVP, sc->cfg.vac2_ovp },
		{ F_VAC1_PD_EN, sc->cfg.vac1_pd_en },
		{ F_VAC2_PD_EN, sc->cfg.vac2_pd_en },
		{ F_FSW_SET, sc->cfg.fsw_set },
		{ F_WD_TIMEOUT, sc->cfg.wd_timeout },
		{ F_WD_TIMEOUT_DIS, sc->cfg.wd_timeout_dis },
		{ F_IBAT_SNS_R, sc->cfg.ibat_sns_r },
		{ F_SS_TIMEOUT_DIS, sc->cfg.ss_timeout_dis },
		{ F_SS_TIMEOUT, sc->cfg.ss_timeout },
		{ F_IBUS_UCP_FALL_DG, sc->cfg.ibus_ucp_fall_dg },
		{ F_VOUT_OVP_DIS, sc->cfg.vout_ovp_dis },
		{ F_VOUT_OVP, sc->cfg.vout_ovp },
		{ F_VBUS_OVP_DIS, sc->cfg.vbus_ovp_dis },
		{ F_PMID2OUT_OVP_DIS, sc->cfg.pmid2out_ovp_dis },
		{ F_PMID2OUT_OVP, sc->cfg.pmid2out_ovp },
		{ F_PMID2OUT_UVP_DIS, sc->cfg.pmid2out_uvp_dis },
		{ F_PMID2OUT_UVP, sc->cfg.pmid2out_uvp },
	};
	int ret, i;

	ret = sc8571_field_write(sc, F_REG_RST, 1);
	if (ret < 0)
		dev_err(sc->dev, "%s Failed to reset registers(%d)\n",
			"sc8571_init_device", ret);
	msleep(10);
	for (i = 0; i < ARRAY_SIZE(d); i++)
		ret = sc8571_field_write(sc, d[i].field, d[i].val);
	if (sc->mode == SC8571_SLAVE) {
		ret = sc8571_field_write(sc, F_VBUS_IN_RANGE_DIS, 1);
		if (ret < 0)
			dev_err(sc->dev, "%s Failed to disable vbus in range(%d)\n",
				"sc8571_init_device", ret);
	}
	return ret;
}

enum {
	ADC_IBUS,
	ADC_VBUS,
	ADC_VAC1,
	ADC_VAC2,
	ADC_VOUT,
	ADC_VBAT,
	ADC_IBAT,
	ADC_TSBUS,
	ADC_TSBAT,
	ADC_TDIE,
	ADC_MAX_NUM,
} SC8571_ADC_CH;

static const int sc8571_adc_m[10] =
	{ 25, 625, 625, 625, 25, 25, 3125, 9766, 9766, 5 };
static const int sc8571_adc_l[10] =
	{ 10, 100, 100, 100, 10, 10, 1000, 100000, 100000, 10 };
static const int sc8571_adc_accuracy_tbl[10] =
	{ 150000, 35000, 35000, 35000, 20000, 20000, 200000, 0, 0, 4 };

static int sc8571_get_adc_data(struct sc8571_chip *sc, int channel, int *result)
{
	u8 val[2] = { 0 };
	int ret;
	u16 raw;

	if (channel > 9)
		return -EINVAL;
	dev_info(sc->dev, "%s:%d", "sc8571_enable_adc", 1);
	sc8571_field_write(sc, F_ADC_EN, 1);
	msleep(50);
	ret = regmap_bulk_read(sc->regmap, SC8571_ADC_BASE + channel * 2,
			       val, 2);
	if (ret < 0) {
		dev_err(sc->dev, "sc8571 read %02x block failed %d\n",
			SC8571_ADC_BASE + channel * 2, ret);
		return ret;
	}
	raw = ((u16)val[0] << 8) | val[1];
	*result = raw * sc8571_adc_m[channel] / sc8571_adc_l[channel];
	dev_info(sc->dev, "val[1]:%x, val[0]:%x", val[1], val[0]);
	dev_info(sc->dev, "%s %d %d", "sc8571_get_adc_data", channel, *result);
	dev_info(sc->dev, "%s:%d", "sc8571_enable_adc", 0);
	sc8571_field_write(sc, F_ADC_EN, 0);
	return ret;
}

static int sc8571_enable_charge(struct sc8571_chip *sc, bool enable)
{
	int ret;

	dev_info(sc->dev, "%s:%d", "sc8571_enable_charge", enable);
	ret = sc8571_field_write(sc, F_CHG_EN, enable);
	sc8571_dump_reg(sc);
	return ret;
}

static int mtk_sc8571_enable_chg(struct charger_device *chg_dev, bool en)
{
	return sc8571_enable_charge(charger_get_data(chg_dev), en);
}

static int sc8571_check_charge_enabled(struct sc8571_chip *sc, bool *enabled)
{
	int ret;
	u32 val = 0;

	ret = sc8571_field_read(sc, F_CP_SWITCHING_STAT, &val);

	*enabled = (bool)val;

	dev_info(sc->dev, "%s:%d", __func__, val);

	return ret;
}

static int mtk_sc8571_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);
	int ret;

	ret = sc8571_check_charge_enabled(sc, en);

	return ret;
}

enum sc8571_reg_range {
	SC8571_VBAT_OVP,
	SC8571_IBAT_OCP,
	SC8571_VBUS_OVP,
	SC8571_IBUS_OCP,
	SC8571_VBUS_OVP_ALM,
	SC8571_VBAT_OVP_ALM,
};

struct reg_range {
	u32 min;
	u32 max;
	u32 step;
	u32 offset;
	const u32 *table;
	u16 num_table;
	bool round_up;
};

#define SC8571_CHG_RANGE(_min, _max, _step, _offset, _ru) \
{ \
	.min = _min, \
	.max = _max, \
	.step = _step, \
	.offset = _offset, \
	.round_up = _ru, \
}

static const struct reg_range sc8571_reg_range[] = {
	[SC8571_VBAT_OVP]	= SC8571_CHG_RANGE(7000, 9540, 20, 7000, false),
	[SC8571_IBAT_OCP]	= SC8571_CHG_RANGE(0, 12700, 100, 0, false),
	[SC8571_VBUS_OVP]	= SC8571_CHG_RANGE(14000, 26700, 100, 14000, false),
	[SC8571_IBUS_OCP]	= SC8571_CHG_RANGE(1000, 8000, 250, 1000, false),
	[SC8571_VBUS_OVP_ALM]	= SC8571_CHG_RANGE(14000, 26700, 100, 14000, false),
	[SC8571_VBAT_OVP_ALM]	= SC8571_CHG_RANGE(7000, 9540, 20, 7000, false),
};

static u8 val2reg(enum sc8571_reg_range id, u32 val)
{
	int i;
	u8 reg;
	const struct reg_range *range = &sc8571_reg_range[id];

	if (!range)
		return val;

	if (range->table) {
		if (val <= range->table[0])
			return 0;
		for (i = 1; i < range->num_table - 1; i++) {
			if (val == range->table[i])
				return i;
			if (val > range->table[i] &&
			    val < range->table[i + 1])
				return range->round_up ? i + 1 : i;
		}
		return range->num_table - 1;
	}
	if (val <= range->min)
		reg = 0;
	else if (val >= range->max)
		reg = (range->max - range->offset) / range->step;
	else if (range->round_up)
		reg = (val - range->offset) / range->step + 1;
	else
		reg = (val - range->offset) / range->step;
	return reg;
}

static int sc8571_set_busovp_th(struct sc8571_chip *sc, int threshold)
{
	int reg_val = val2reg(SC8571_VBUS_OVP, threshold);

	dev_info(sc->dev, "%s:%d-%d", __func__, threshold, reg_val);

	return sc8571_field_write(sc, F_VBUS_OVP, reg_val);
}

static int sc8571_set_busocp_th(struct sc8571_chip *sc, int threshold)
{
	int reg_val = val2reg(SC8571_IBUS_OCP, threshold);

	dev_info(sc->dev, "%s:%d-%d", __func__, threshold, reg_val);

	return sc8571_field_write(sc, F_IBUS_OCP, reg_val);
}

static int sc8571_set_batovp_th(struct sc8571_chip *sc, int threshold)
{
	int reg_val = val2reg(SC8571_VBAT_OVP, threshold);

	dev_info(sc->dev, "%s:%d-%d", __func__, threshold, reg_val);

	return sc8571_field_write(sc, F_VBAT_OVP, reg_val);
}

static int sc8571_set_batocp_th(struct sc8571_chip *sc, int threshold)
{
	int reg_val = val2reg(SC8571_IBAT_OCP, threshold);

	dev_info(sc->dev, "%s:%d-%d", __func__, threshold, reg_val);

	return sc8571_field_write(sc, F_IBAT_OCP, reg_val);
}

static int sc8571_set_vbusovp_alarm(struct sc8571_chip *sc, int threshold)
{
	int reg_val = val2reg(SC8571_VBUS_OVP_ALM, threshold);

	dev_info(sc->dev, "%s:%d-%d", __func__, threshold, reg_val);

	return sc8571_field_write(sc, F_VBUS_OVP_ALM, reg_val);
}

static int sc8571_set_vbatovp_alarm(struct sc8571_chip *sc, int threshold)
{
	int reg_val = val2reg(SC8571_VBAT_OVP_ALM, threshold);

	dev_info(sc->dev, "%s:%d-%d", __func__, threshold, reg_val);

	return sc8571_field_write(sc, F_VBAT_OVP_ALM, reg_val);
}

static int mtk_sc8571_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);
	int mv;

	mv = uV / 1000;

	return sc8571_set_busovp_th(sc, mv);
}

static int mtk_sc8571_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);
	int ma;

	ma = uA / 1000;

	return sc8571_set_busocp_th(sc, ma);
}

static int mtk_sc8571_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);
	int ret;

	ret = sc8571_set_batovp_th(sc, uV/1000);
	if (ret < 0)
		return ret;

	return ret;
}

static int mtk_sc8571_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);
	int ret;

	ret = sc8571_set_batocp_th(sc, uA/1000);
	if (ret < 0)
		return ret;

	return ret;
}

static int mtk_sc8571_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);
	int ret;

	ret = sc8571_set_vbatovp_alarm(sc, uV/1000);
	if (ret < 0)
		return ret;

	return ret;
}

static int mtk_sc8571_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);
	int ret;

	ret = sc8571_set_vbusovp_alarm(sc, uV/1000);
	if (ret < 0)
		return ret;

	return ret;
}

static int mtk_sc8571_reset_vbatovp_alarm(struct charger_device *chg_dev)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);

	dev_err(sc->dev, "%s", "mtk_sc8571_reset_vbatovp_alarm");
	return 0;
}

static int mtk_sc8571_reset_vbusovp_alarm(struct charger_device *chg_dev)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);

	dev_err(sc->dev, "%s", "mtk_sc8571_reset_vbusovp_alarm");
	return 0;
}

static int mtk_sc8571_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);
	u32 val = 0;
	int ret;

	ret = sc8571_field_read(sc, F_VBUS_ERR_LO_STAT, &val);
	if (ret < 0)
		return ret;
	dev_info(sc->dev, "%s:%d", "sc8571_is_vbuslowerr", val);
	*err = val != 0;
	return ret;
}

static int mtk_sc8571_init_chip(struct charger_device *chg_dev)
{
	return sc8571_init_device(charger_get_data(chg_dev));
}

static int mtk_sc8571_get_adc(struct charger_device *chg_dev,
			      enum adc_channel chan, int *min, int *max)
{
	struct sc8571_chip *sc = charger_get_data(chg_dev);
	int internal;

	switch (chan) {
	case ADC_CHANNEL_VBUS: internal = 1; break;
	case ADC_CHANNEL_VBAT: internal = 5; break;
	case ADC_CHANNEL_IBUS: internal = 0; break;
	case ADC_CHANNEL_IBAT: internal = 6; break;
	case ADC_CHANNEL_TEMP_JC: internal = 9; break;
	case ADC_CHANNEL_VOUT: internal = 4; break;
	default: internal = 10; break;
	}
	sc8571_get_adc_data(sc, internal, max);
	if (chan != ADC_CHANNEL_TEMP_JC)
		*max *= 1000;
	if (min != max)
		*min = *max;
	return 0;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch"
#pragma clang diagnostic ignored "-Wsometimes-uninitialized"
static int mtk_sc8571_get_adc_accuracy(struct charger_device *chg_dev,
				       enum adc_channel chan,
				       int *min, int *max)
{
	int channel;

	switch (chan) {
	case ADC_CHANNEL_VBUS: channel = 1; break;
	case ADC_CHANNEL_VBAT: channel = 5; break;
	case ADC_CHANNEL_IBUS: channel = 0; break;
	case ADC_CHANNEL_IBAT: channel = 6; break;
	case ADC_CHANNEL_TEMP_JC: channel = 9; break;
	case ADC_CHANNEL_VOUT: channel = 4; break;
	default:
		asm volatile("brk #0x5512");
		__builtin_unreachable();
	}
	*min = *max = sc8571_adc_accuracy_tbl[channel];
	return 0;
}
#pragma clang diagnostic pop

static const struct charger_ops sc8571_chg_ops = {
	.enable = mtk_sc8571_enable_chg,
	.is_enabled = mtk_sc8571_is_chg_enabled,
	.set_ibusocp = mtk_sc8571_set_ibusocp,
	.set_vbusovp = mtk_sc8571_set_vbusovp,
	.set_ibatocp = mtk_sc8571_set_ibatocp,
	.set_vbatovp = mtk_sc8571_set_vbatovp,
	.set_vbatovp_alarm = mtk_sc8571_set_vbatovp_alarm,
	.reset_vbatovp_alarm = mtk_sc8571_reset_vbatovp_alarm,
	.set_vbusovp_alarm = mtk_sc8571_set_vbusovp_alarm,
	.reset_vbusovp_alarm = mtk_sc8571_reset_vbusovp_alarm,
	.is_vbuslowerr = mtk_sc8571_is_vbuslowerr,
	.init_chip = mtk_sc8571_init_chip,
	.get_adc = mtk_sc8571_get_adc,
	.get_adc_accuracy = mtk_sc8571_get_adc_accuracy,
};

static const struct charger_properties sc8571_chg_props = {
	.alias_name = "sc8571_chg",
};

static enum power_supply_property sc8571_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_TEMP,
};

static int sc8571_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct sc8571_chip *sc = power_supply_get_drvdata(psy);
	int result = 0;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		sc8571_check_charge_enabled(sc, &sc->charge_enabled);
		val->intval = sc->charge_enabled;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sc8571_get_adc_data(sc, ADC_VBUS, &result);
		if (!ret)
			sc->vbus_volt = result;
		val->intval = sc->vbus_volt;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = sc8571_get_adc_data(sc, ADC_IBUS, &result);
		if (!ret)
			sc->ibus_curr = result;
		val->intval = sc->ibus_curr;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sc8571_get_adc_data(sc, ADC_VBAT, &result);
		if (!ret)
			sc->vbat_volt = result;
		val->intval = sc->vbat_volt;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sc8571_get_adc_data(sc, ADC_IBAT, &result);
		if (!ret)
			sc->ibat_curr = result;
		val->intval = sc->ibat_curr;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = sc8571_get_adc_data(sc, ADC_TDIE, &result);
		if (!ret)
			sc->tdie = result;
		val->intval = sc->tdie;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sc8571_charger_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct sc8571_chip *sc = power_supply_get_drvdata(psy);

	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;
	sc8571_enable_charge(sc, val->intval != 0);
	dev_info(sc->dev, "POWER_SUPPLY_PROP_ONLINE: %s\n",
		 val->intval ? "enable" : "disable");
	return 0;
}

static int sc8571_charger_is_writeable(struct power_supply *psy,
				       enum power_supply_property psp)
{
	return 0;
}

static const char * const sc8571_psy_name[] =
	{ "sc-cp-standalone", "sc-cp-master", "sc-cp-slave" };
static const char * const sc8571_irq_name[] =
	{ "sc8571-standalone-irq", "sc8571-master-irq", "sc8571-slave-irq" };

static int sc8571_psy_register(struct sc8571_chip *sc)
{
	sc->psy_cfg.drv_data = sc;
	sc->psy_cfg.of_node = sc->dev->of_node;

	sc->psy_desc.name = sc8571_psy_name[sc->mode];

	sc->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	sc->psy_desc.properties = sc8571_charger_props;
	sc->psy_desc.num_properties = ARRAY_SIZE(sc8571_charger_props);
	sc->psy_desc.get_property = sc8571_charger_get_property;
	sc->psy_desc.set_property = sc8571_charger_set_property;
	sc->psy_desc.property_is_writeable = sc8571_charger_is_writeable;
	sc->psy = power_supply_register(sc->dev, &sc->psy_desc, &sc->psy_cfg);
	if (IS_ERR(sc->psy)) {
		dev_err(sc->dev, "%s failed to register psy\n", "sc8571_psy_register");
		return PTR_ERR(sc->psy);
	}
	dev_info(sc->dev, "%s power supply register successfully\n",
		 sc->psy_desc.name);
	return 0;
}

static irqreturn_t sc8571_irq_handler(int irq, void *data)
{
	struct sc8571_chip *sc = data;

	dev_err(sc->dev, "INT OCCURED\n");
	sc8571_dump_reg(sc);
	power_supply_changed(sc->psy);
	return IRQ_HANDLED;
}

static int sc8571_register_interrupt(struct sc8571_chip *sc)
{
	int ret;

	if (gpio_is_valid(sc->irq_gpio)) {
		ret = gpio_request_one(sc->irq_gpio, GPIOF_DIR_IN, "sc8571_irq");
		if (ret) {
			dev_err(sc->dev, "failed to request sc8571_irq\n");
			return -EINVAL;
		}
		sc->irq = gpio_to_irq(sc->irq_gpio);
		if (sc->irq < 0) {
			dev_err(sc->dev, "failed to gpio_to_irq\n");
			return -EINVAL;
		}
	} else {
		dev_err(sc->dev, "irq gpio not provided\n");
		return -EINVAL;
	}

	if (sc->irq) {
		ret = devm_request_threaded_irq(&sc->client->dev, sc->irq,
				NULL, sc8571_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				sc8571_irq_name[sc->mode], sc);

		if (ret < 0) {
			dev_err(sc->dev, "request irq for irq=%d failed, ret =%d\n",
				sc->irq, ret);
			return ret;
		}
		enable_irq_wake(sc->irq);
	}

	return ret;
}

static ssize_t sc8571_show_registers(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sc8571_chip *sc = dev_get_drvdata(dev);
	u8 tmpbuf[300], addr;
	unsigned int val = 0;
	int len, idx = 0, ret;

	memset(tmpbuf, 0, sizeof(tmpbuf));
	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8571");
	for (addr = 0; addr <= SC8571_MAX_REGISTER; addr++) {
		ret = regmap_read(sc->regmap, addr, &val);
		if (!ret) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}
	return idx;
}

static ssize_t sc8571_store_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sc8571_chip *sc = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= SC8571_MAX_REGISTER)
		regmap_write(sc->regmap, (unsigned char)reg, (unsigned char)val);
	return count;
}
static DEVICE_ATTR(registers, 0660, sc8571_show_registers,
		   sc8571_store_register);

static ssize_t chipid_show(struct class *class, struct class_attribute *attr,
			   char *buf)
{
	return sprintf(buf, "%s\n", sc8571_charge_id);
}

static struct class_attribute class_attr_sc8571[] = {
	__ATTR(chipid, 0444, chipid_show, NULL),
	__ATTR_NULL,
};

static int sc8571_mode_data[] =
	{ SC8571_STANDALONE, SC8571_MASTER, SC8571_SLAVE };

static struct of_device_id sc8571_charger_match_table[] = {
	{ .compatible = "sc,sc8571-standalone", .data = &sc8571_mode_data[0] },
	{ .compatible = "sc,sc8571-master", .data = &sc8571_mode_data[1] },
	{ .compatible = "sc,sc8571-slave", .data = &sc8571_mode_data[2] },
	{},
};

static int sc8571_detect_device(struct sc8571_chip *sc)
{
	int ret;
	u32 val = 0;

	ret = sc8571_field_read(sc, F_DEVICE_ID, &val);
	if (ret < 0) {
		dev_err(sc->dev, "%s fail(%d)\n", __func__, ret);
		return ret;
	}

	sprintf(sc8571_charge_id, "%x", val);

	if (val != SC8571_DEVICE_ID) {
		dev_err(sc->dev, "%s not find SC8571, ID = 0x%02x\n", __func__, ret);
		return -EINVAL;
	}

	return ret;
}

static void sc8571_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static void sc8571_remove_device_node(struct sc8571_chip *sc)
{
	device_remove_file(sc->dev, &dev_attr_registers);
}

static int sc8571_set_work_mode(struct sc8571_chip *sc, int mode)
{
	sc->mode = mode;

	dev_err(sc->dev, "work mode is %s\n", sc->mode == SC8571_STANDALONE
		? "standalone" : (sc->mode == SC8571_MASTER ? "master" : "slave"));

	return 0;
}

static int sc8571_charger_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct sc8571_chip *sc;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret, i;

	dev_err(&client->dev, "%s (%s)\n", __func__, SC8571_DRV_VERSION);

	sc = devm_kzalloc(&client->dev, sizeof(struct sc8571_chip), GFP_KERNEL);
	if (!sc) {
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	sc->dev = &client->dev;
	sc->client = client;

	sc->regmap = devm_regmap_init_i2c(client,
					  &sc8571_regmap_config);
	if (IS_ERR(sc->regmap)) {
		dev_err(sc->dev, "Failed to initialize regmap\n");
		ret = PTR_ERR(sc->regmap);
		goto err_regmap_init;
	}

	for (i = 0; i < ARRAY_SIZE(sc8571_reg_fields); i++) {
		const struct reg_field *reg_fields = sc8571_reg_fields;

		sc->fields[i] =
			devm_regmap_field_alloc(sc->dev,
						sc->regmap,
						reg_fields[i]);
		if (IS_ERR(sc->fields[i])) {
			dev_err(sc->dev, "cannot allocate regmap field\n");
			ret = PTR_ERR(sc->fields[i]);
			goto err_regmap_field;
		}
	}

	ret = sc8571_detect_device(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s detect device fail\n", __func__);
		goto err_detect_dev;
	}

	i2c_set_clientdata(client, sc);
	sc8571_create_device_node(&(client->dev));

	match = of_match_node(sc8571_charger_match_table, node);
	if (match == NULL) {
		dev_err(sc->dev, "device tree match not found!\n");
		goto err_match_node;
	}

	ret = sc8571_set_work_mode(sc, *(int *)match->data);
	if (ret) {
		dev_err(sc->dev, "Fail to set work mode!\n");
		goto err_set_mode;
	}

	ret = sc8571_parse_dt(sc, &client->dev);
	if (ret < 0) {
		dev_err(sc->dev, "%s parse dt failed(%d)\n", __func__, ret);
		goto err_parse_dt;
	}

	ret = sc8571_init_device(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s init device failed(%d)\n", __func__, ret);
		goto err_init_device;
	}

	ret = sc8571_psy_register(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s psy register failed(%d)\n", __func__, ret);
		goto err_register_psy;
	}

	ret = sc8571_register_interrupt(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s register irq fail(%d)\n",
			__func__, ret);
		goto err_register_irq;
	}

	sc->chg_dev = charger_device_register(sc->chg_dev_name,
					      &client->dev, sc,
					      &sc8571_chg_ops,
					      &sc8571_chg_props);
	if (IS_ERR_OR_NULL(sc->chg_dev)) {
		ret = sc->chg_dev ? PTR_ERR(sc->chg_dev) : -ENODEV;
		sc->chg_dev = NULL;
		dev_err(sc->dev, "Fail to register charger!\n");
		goto err_register_mtk_charger;
	}

	/* YFT: /sys/class/fastcharge_<n>/chipid */
	sprintf(sc8571_buf, "fastcharge_%d", sc8571_conunt++);
	printk("%s: sc8571_buf=%s\n", __func__, sc8571_buf);
	sc8571_fg_class = class_create(THIS_MODULE, sc8571_buf);
	if (IS_ERR(sc8571_fg_class)) {
		ret = PTR_ERR(sc8571_fg_class);
		sc8571_fg_class = NULL;
		printk(KERN_ERR "create class, err = %d\n", ret);
		goto err_register_mtk_charger;
	}
	for (i = 0; class_attr_sc8571[i].attr.name; i++) {
		ret = class_create_file(sc8571_fg_class, &class_attr_sc8571[i]);
		if (ret)
			printk(KERN_ERR "Fail class_create_file\n");
	}

	dev_err(sc->dev, "sc8571[%s] probe successfully!\n",
		sc->mode == SC8571_MASTER ? "master" : "slave");
	return 0;

err_register_mtk_charger:
	if (sc->chg_dev) {
		charger_device_unregister(sc->chg_dev);
		sc->chg_dev = NULL;
	}
err_register_irq:
	power_supply_unregister(sc->psy);
	sc->psy = NULL;
err_register_psy:
err_init_device:
	sc8571_remove_device_node(sc);
err_detect_dev:
err_match_node:
err_set_mode:
err_parse_dt:
err_regmap_init:
err_regmap_field:
	devm_kfree(&client->dev, sc);
err_kzalloc:
	dev_err(&client->dev, "sc8571 probe fail\n");
	return ret;
}

static void sc8571_charger_remove(struct i2c_client *client)
{
	struct sc8571_chip *sc = i2c_get_clientdata(client);

	sc8571_remove_device_node(sc);
	if (sc->chg_dev) {
		charger_device_unregister(sc->chg_dev);
		sc->chg_dev = NULL;
	}
	if (sc->psy) {
		power_supply_unregister(sc->psy);
		sc->psy = NULL;
	}
}

static void sc8571_charger_shutdown(struct i2c_client *client)
{
	struct sc8571_chip *sc = i2c_get_clientdata(client);

	dev_info(sc->dev, "%s:%d", "sc8571_enable_adc", 0);
	sc8571_field_write(sc, F_ADC_EN, 0);
	printk("sc8571_charger_shutdown: sc8571_enable_adc\n");
}

static int sc8571_suspend(struct device *dev)
{
	struct sc8571_chip *sc = dev_get_drvdata(dev);

	dev_info(sc->dev, "Suspend successfully!");
	if (device_may_wakeup(dev))
		irq_set_irq_wake(sc->irq, 1);
	disable_irq(sc->irq);
	return 0;
}

static int sc8571_resume(struct device *dev)
{
	struct sc8571_chip *sc = dev_get_drvdata(dev);

	dev_info(sc->dev, "Resume successfully!");
	if (device_may_wakeup(dev))
		irq_set_irq_wake(sc->irq, 0);
	enable_irq(sc->irq);
	return 0;
}
static SIMPLE_DEV_PM_OPS(sc8571_pm, sc8571_suspend, sc8571_resume);

static struct i2c_driver sc8571_charger_driver = {
	.driver = {
		.name = "sc8571",
		.owner = THIS_MODULE,
		.of_match_table = sc8571_charger_match_table,
		.pm = &sc8571_pm,
	},
	.probe = sc8571_charger_probe,
	.remove = sc8571_charger_remove,
	.shutdown = sc8571_charger_shutdown,
};
module_i2c_driver(sc8571_charger_driver);

MODULE_DESCRIPTION("SC SC8571 Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("South Chip <Aiden-yu@southchip.com>");
