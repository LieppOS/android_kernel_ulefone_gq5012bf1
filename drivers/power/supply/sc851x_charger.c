// SPDX-License-Identifier: GPL-2.0
/*
 * SouthChip SC851X (SC8510 / SC8517) bidirectional switched-capacitor
 * converter -- "Dual Cell High Efficiency 10-A Forward 2:1 Converter and
 * Reverse 1:2 Charger with Load-Switch Function".
 *
 * Reconstructed from the Ulefone Armor 29 Pro Thermal (GQ5012BF1) stock
 * vendor_boot module sc851x_charger.ko
 *   SHA256 b5e5f08bcfe37ac8c81525b0d4fe514902b552cc45b7f99fbeda8b80f6c2aa0c
 *   build-id d7daa55c413b8e593c2062b79b6bbaa9d19a68cc
 *   vermagic 6.1.115-android14-11-g945dff7bc1bf
 *
 * The stock binary is the behavioural oracle. Field names are taken from the
 * device-tree property names that the stock driver itself parses; bitfields
 * that the stock driver allocates but never reads or writes are left
 * deliberately unnamed (SC851X_F_RSVD_*) because their silicon meaning is not
 * proven by the oracle.
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
#include <linux/regmap.h>

#define SC851X_DRV_VERSION	"1.0.0_G"

#define SC851X_REG_MAX		0x15
#define SC851X_REG_FLAG1	0x0C
#define SC851X_REG_FLAG2	0x0D
#define SC851X_REG_FLAG3	0x0E

/*
 * Register-field table. Index order is ABI: sc851x_init_device() and
 * sc851x_charger_shutdown() address fields by numeric index, and the stock
 * .rodata table has exactly this layout.
 */
enum sc851x_fields {
	SC851X_F_V2X_OVP_DIS = 0,	/* 0x00[7]   */
	SC851X_F_V2X_OVP,		/* 0x00[6:5] */
	SC851X_F_V2X_UVLO,		/* 0x00[4]   */
	SC851X_F_V2X_S2F,		/* 0x00[3:2] */
	SC851X_F_UVLO_DEG,		/* 0x00[1:0] */
	SC851X_F_V1X_OVP_DIS,		/* 0x01[7]   */
	SC851X_F_V1X_OVP,		/* 0x01[6:2] */
	SC851X_F_V1X_SCP_DIS,		/* 0x01[1]   */
	SC851X_F_V1X_SCP_F,		/* 0x01[0]   */
	SC851X_F_VAC_OVP_DIS,		/* 0x02[7]   */
	SC851X_F_VAC_OVP,		/* 0x02[6:3] */
	SC851X_F_RSVD_02_1,		/* 0x02[1]   unused by stock */
	SC851X_F_RSVD_02_0,		/* 0x02[0]   unused by stock */
	SC851X_F_RVS_OCP,		/* 0x03[7:4] */
	SC851X_F_FWD_OCP,		/* 0x03[3:0] */
	SC851X_F_WDT,			/* 0x04[7:4] */
	SC851X_F_LNC_SS_TIMEOUT_DIS,	/* 0x04[3]   */
	SC851X_F_LNC_SS_TIMEOUT,	/* 0x04[2:1] */
	SC851X_F_SS_TIMEOUT_DIS,	/* 0x04[0]   */
	SC851X_F_RSVD_05_7_5,		/* 0x05[7:5] unused by stock */
	SC851X_F_RSVD_05_1,		/* 0x05[1]   unused by stock */
	SC851X_F_RSVD_05_0,		/* 0x05[0]   unused by stock */
	SC851X_F_RSVD_06_7,		/* 0x06[7]   unused by stock */
	SC851X_F_RSVD_06_6,		/* 0x06[6]   unused by stock */
	SC851X_F_RSVD_06_5,		/* 0x06[5]   unused by stock */
	SC851X_F_RSVD_06_4,		/* 0x06[4]   unused by stock */
	SC851X_F_REG_RST,		/* 0x06[3]   */
	SC851X_F_AUDIO_EN,		/* 0x06[2]   */
	SC851X_F_AUDIO_FREQ,		/* 0x06[1]   */
	SC851X_F_AUDIO_INTO_DG,		/* 0x06[0]   */
	SC851X_F_RSVD_07_6,		/* 0x07[6]   unused by stock */
	SC851X_F_RSVD_07_5_4,		/* 0x07[5:4] unused by stock */
	SC851X_F_FREQ,			/* 0x07[3:0] */
	SC851X_F_REF_SKIP_R,		/* 0x08[7:5] */
	SC851X_F_SKIP_HYST,		/* 0x08[3:1] */
	SC851X_F_T_VAC_OVP_DG,		/* 0x12[7]   */
	SC851X_F_T_V2X_OVP_DG,		/* 0x12[6]   */
	SC851X_F_T_V1X_OVP_DG,		/* 0x12[5]   */
	SC851X_F_T_FWD_OCP_DG,		/* 0x12[4]   */
	SC851X_F_T_RVS_OCP_DG,		/* 0x12[3]   */
	SC851X_F_T_V2X2VAD_LNC_MAX_DG,	/* 0x12[2]   */
	SC851X_F_T_T3_SET,		/* 0x12[0]   */
	SC851X_F_FWD_OCP_DIS,		/* 0x13[7]   */
	SC851X_F_RVS_OCP_DIS,		/* 0x13[6]   */
	SC851X_F_TSHUT_DIS,		/* 0x13[5]   */
	SC851X_F_RSVD_13_4,		/* 0x13[4]   unused by stock */
	SC851X_F_RSVD_13_3,		/* 0x13[3]   unused by stock */
	SC851X_F_V1X_OSS_OPP_DIS,	/* 0x13[1]   */
	SC851X_F_RSVD_14_4_0,		/* 0x14[4:0] unused by stock */
	SC851X_F_FAM_EN,		/* 0x15[1:0] */
	SC851X_F_MAX
};

static const struct reg_field sc851x_reg_fields[] = {
	[SC851X_F_V2X_OVP_DIS]		= REG_FIELD(0x00, 7, 7),
	[SC851X_F_V2X_OVP]		= REG_FIELD(0x00, 5, 6),
	[SC851X_F_V2X_UVLO]		= REG_FIELD(0x00, 4, 4),
	[SC851X_F_V2X_S2F]		= REG_FIELD(0x00, 2, 3),
	[SC851X_F_UVLO_DEG]		= REG_FIELD(0x00, 0, 1),
	[SC851X_F_V1X_OVP_DIS]		= REG_FIELD(0x01, 7, 7),
	[SC851X_F_V1X_OVP]		= REG_FIELD(0x01, 2, 6),
	[SC851X_F_V1X_SCP_DIS]		= REG_FIELD(0x01, 1, 1),
	[SC851X_F_V1X_SCP_F]		= REG_FIELD(0x01, 0, 0),
	[SC851X_F_VAC_OVP_DIS]		= REG_FIELD(0x02, 7, 7),
	[SC851X_F_VAC_OVP]		= REG_FIELD(0x02, 3, 6),
	[SC851X_F_RSVD_02_1]		= REG_FIELD(0x02, 1, 1),
	[SC851X_F_RSVD_02_0]		= REG_FIELD(0x02, 0, 0),
	[SC851X_F_RVS_OCP]		= REG_FIELD(0x03, 4, 7),
	[SC851X_F_FWD_OCP]		= REG_FIELD(0x03, 0, 3),
	[SC851X_F_WDT]			= REG_FIELD(0x04, 4, 7),
	[SC851X_F_LNC_SS_TIMEOUT_DIS]	= REG_FIELD(0x04, 3, 3),
	[SC851X_F_LNC_SS_TIMEOUT]	= REG_FIELD(0x04, 1, 2),
	[SC851X_F_SS_TIMEOUT_DIS]	= REG_FIELD(0x04, 0, 0),
	[SC851X_F_RSVD_05_7_5]		= REG_FIELD(0x05, 5, 7),
	[SC851X_F_RSVD_05_1]		= REG_FIELD(0x05, 1, 1),
	[SC851X_F_RSVD_05_0]		= REG_FIELD(0x05, 0, 0),
	[SC851X_F_RSVD_06_7]		= REG_FIELD(0x06, 7, 7),
	[SC851X_F_RSVD_06_6]		= REG_FIELD(0x06, 6, 6),
	[SC851X_F_RSVD_06_5]		= REG_FIELD(0x06, 5, 5),
	[SC851X_F_RSVD_06_4]		= REG_FIELD(0x06, 4, 4),
	[SC851X_F_REG_RST]		= REG_FIELD(0x06, 3, 3),
	[SC851X_F_AUDIO_EN]		= REG_FIELD(0x06, 2, 2),
	[SC851X_F_AUDIO_FREQ]		= REG_FIELD(0x06, 1, 1),
	[SC851X_F_AUDIO_INTO_DG]	= REG_FIELD(0x06, 0, 0),
	[SC851X_F_RSVD_07_6]		= REG_FIELD(0x07, 6, 6),
	[SC851X_F_RSVD_07_5_4]		= REG_FIELD(0x07, 4, 5),
	[SC851X_F_FREQ]			= REG_FIELD(0x07, 0, 3),
	[SC851X_F_REF_SKIP_R]		= REG_FIELD(0x08, 5, 7),
	[SC851X_F_SKIP_HYST]		= REG_FIELD(0x08, 1, 3),
	[SC851X_F_T_VAC_OVP_DG]		= REG_FIELD(0x12, 7, 7),
	[SC851X_F_T_V2X_OVP_DG]		= REG_FIELD(0x12, 6, 6),
	[SC851X_F_T_V1X_OVP_DG]		= REG_FIELD(0x12, 5, 5),
	[SC851X_F_T_FWD_OCP_DG]		= REG_FIELD(0x12, 4, 4),
	[SC851X_F_T_RVS_OCP_DG]		= REG_FIELD(0x12, 3, 3),
	[SC851X_F_T_V2X2VAD_LNC_MAX_DG]	= REG_FIELD(0x12, 2, 2),
	[SC851X_F_T_T3_SET]		= REG_FIELD(0x12, 0, 0),
	[SC851X_F_FWD_OCP_DIS]		= REG_FIELD(0x13, 7, 7),
	[SC851X_F_RVS_OCP_DIS]		= REG_FIELD(0x13, 6, 6),
	[SC851X_F_TSHUT_DIS]		= REG_FIELD(0x13, 5, 5),
	[SC851X_F_RSVD_13_4]		= REG_FIELD(0x13, 4, 4),
	[SC851X_F_RSVD_13_3]		= REG_FIELD(0x13, 3, 3),
	[SC851X_F_V1X_OSS_OPP_DIS]	= REG_FIELD(0x13, 1, 1),
	[SC851X_F_RSVD_14_4_0]		= REG_FIELD(0x14, 0, 4),
	[SC851X_F_FAM_EN]		= REG_FIELD(0x15, 0, 1),
};

/* Device-tree supplied configuration, in stock parse order. */
struct sc851x_cfg {
	u32 v2x_ovp_dis;
	u32 v2x_ovp;
	u32 v2x_uvlo;
	u32 v2x_s2f;
	u32 uvlo_deg;
	u32 v1x_ovp_dis;
	u32 v1x_ovp;
	u32 v1x_scp_dis;
	u32 v1x_scp_f;
	u32 vac_ovp_dis;
	u32 vac_ovp;
	u32 rvs_ocp;
	u32 fwd_ocp;
	u32 wdt;
	u32 lnc_ss_timeout_dis;
	u32 lnc_ss_timeout;
	u32 ss_timeout_dis;
	u32 audio_en;
	u32 audio_freq;
	u32 audio_into_dg;
	u32 freq;
	u32 ref_skip_r;
	u32 skip_hyst;
	u32 t_vac_ovp_dg;
	u32 t_v2x_ovp_dg;
	u32 t_v1x_ovp_dg;
	u32 t_fwd_ocp_dg;
	u32 t_rvs_ocp_dg;
	u32 t_v2x2vad_lnc_max_dg;
	u32 t_t3_set;
	u32 fwd_ocp_dis;
	u32 rvs_ocp_dis;
	u32 tshut_dis;
	u32 v1x_oss_opp_dis;
	u32 fam_en;
};

struct sc851x_chip {
	struct device *dev;				/* +0x000 */
	struct i2c_client *client;			/* +0x008 */
	struct regmap *regmap;				/* +0x010 */
	struct regmap_field *rmap_fields[SC851X_F_MAX];	/* +0x018 */
	struct sc851x_cfg cfg;				/* +0x1a8 */
	int irq_gpio;					/* +0x234 */
	int irq;					/* +0x238 */
};

static const struct regmap_config sc851x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = SC851X_REG_MAX,
};

struct sc851x_dt_prop {
	const char *name;
	u32 *conv_data;
};

struct sc851x_init_data {
	u32 field;
	u32 val;
};

static int sc851x_field_write(struct sc851x_chip *sc,
			      enum sc851x_fields field_id, u32 val)
{
	int ret;

	ret = regmap_field_write(sc->rmap_fields[field_id], val);
	if (ret < 0)
		dev_err(sc->dev, "sc851x read field %d fail: %d\n",
			field_id, ret);

	return ret;
}

static int sc851x_dump_reg(struct sc851x_chip *sc)
{
	int ret = 0;
	unsigned int val = 0;
	u8 addr;

	for (addr = 0x00; addr <= SC851X_REG_MAX; addr++) {
		ret = regmap_read(sc->regmap, addr, &val);
		dev_err(sc->dev, "%s reg[0x%02x] = 0x%02x\n",
			__func__, addr, val);
	}

	return ret;
}

static __always_inline int sc851x_init_device(struct sc851x_chip *sc)
{
	int ret;
	int i;
	struct sc851x_init_data init_data[] = {
		{ SC851X_F_V2X_OVP_DIS,		sc->cfg.v2x_ovp_dis },
		{ SC851X_F_V2X_OVP,		sc->cfg.v2x_ovp },
		{ SC851X_F_V2X_UVLO,		sc->cfg.v2x_uvlo },
		{ SC851X_F_V2X_S2F,		sc->cfg.v2x_s2f },
		{ SC851X_F_UVLO_DEG,		sc->cfg.uvlo_deg },
		{ SC851X_F_V1X_OVP_DIS,		sc->cfg.v1x_ovp_dis },
		{ SC851X_F_V1X_OVP,		sc->cfg.v1x_ovp },
		{ SC851X_F_V1X_SCP_DIS,		sc->cfg.v1x_scp_dis },
		{ SC851X_F_V1X_SCP_F,		sc->cfg.v1x_scp_f },
		{ SC851X_F_VAC_OVP_DIS,		sc->cfg.vac_ovp_dis },
		{ SC851X_F_VAC_OVP,		sc->cfg.vac_ovp },
		{ SC851X_F_RVS_OCP,		sc->cfg.rvs_ocp },
		{ SC851X_F_FWD_OCP,		sc->cfg.fwd_ocp },
		{ SC851X_F_WDT,			sc->cfg.wdt },
		{ SC851X_F_LNC_SS_TIMEOUT_DIS,	sc->cfg.lnc_ss_timeout_dis },
		{ SC851X_F_LNC_SS_TIMEOUT,	sc->cfg.lnc_ss_timeout },
		{ SC851X_F_SS_TIMEOUT_DIS,	sc->cfg.ss_timeout_dis },
		{ SC851X_F_AUDIO_EN,		sc->cfg.audio_en },
		{ SC851X_F_AUDIO_FREQ,		sc->cfg.audio_freq },
		{ SC851X_F_AUDIO_INTO_DG,	sc->cfg.audio_into_dg },
		{ SC851X_F_FREQ,		sc->cfg.freq },
		{ SC851X_F_REF_SKIP_R,		sc->cfg.ref_skip_r },
		{ SC851X_F_SKIP_HYST,		sc->cfg.skip_hyst },
		{ SC851X_F_T_VAC_OVP_DG,	sc->cfg.t_vac_ovp_dg },
		{ SC851X_F_T_V2X_OVP_DG,	sc->cfg.t_v2x_ovp_dg },
		{ SC851X_F_T_V1X_OVP_DG,	sc->cfg.t_v1x_ovp_dg },
		{ SC851X_F_T_FWD_OCP_DG,	sc->cfg.t_fwd_ocp_dg },
		{ SC851X_F_T_RVS_OCP_DG,	sc->cfg.t_rvs_ocp_dg },
		{ SC851X_F_T_V2X2VAD_LNC_MAX_DG, sc->cfg.t_v2x2vad_lnc_max_dg },
		{ SC851X_F_T_T3_SET,		sc->cfg.t_t3_set },
		{ SC851X_F_FWD_OCP_DIS,		sc->cfg.fwd_ocp_dis },
		{ SC851X_F_RVS_OCP_DIS,		sc->cfg.rvs_ocp_dis },
		{ SC851X_F_TSHUT_DIS,		sc->cfg.tshut_dis },
		{ SC851X_F_V1X_OSS_OPP_DIS,	sc->cfg.v1x_oss_opp_dis },
		{ SC851X_F_FAM_EN,		sc->cfg.fam_en },
	};

	ret = sc851x_field_write(sc, SC851X_F_REG_RST, 1);
	if (ret < 0)
		dev_err(sc->dev, "%s Failed to reset registers(%d)\n",
			__func__, ret);

	msleep(10);

	for (i = 0; i < ARRAY_SIZE(init_data); i++)
		sc851x_field_write(sc, init_data[i].field, init_data[i].val);

	ret = sc851x_dump_reg(sc);

	return ret;
}

static irqreturn_t sc851x_irq_handler(int irq, void *data)
{
	struct sc851x_chip *sc = data;
	unsigned int val;
	int ret;

	dev_err(sc->dev, "INT OCCURED\n");

	val = 0;
	ret = regmap_read(sc->regmap, SC851X_REG_FLAG1, &val);
	if (!ret)
		dev_err(sc->dev, "FLAG1 reg[0c] = 0x%02X\n", val);

	ret = regmap_read(sc->regmap, SC851X_REG_FLAG2, &val);
	if (!ret)
		dev_err(sc->dev, "FLAG2 reg[0d] = 0x%02X\n", val);

	ret = regmap_read(sc->regmap, SC851X_REG_FLAG3, &val);
	if (!ret)
		dev_err(sc->dev, "FLAG3 reg[0e] = 0x%02X\n", val);

	return IRQ_HANDLED;
}

static int sc851x_register_interrupt(struct sc851x_chip *sc)
{
	int ret;

	if (gpio_is_valid(sc->irq_gpio)) {
		ret = gpio_request_one(sc->irq_gpio, GPIOF_DIR_IN, "sc851x_irq");
		if (ret) {
			dev_err(sc->dev, "failed to request sc851x_irq\n");
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
				NULL, sc851x_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"sc851x-irq", sc);

		if (ret < 0) {
			dev_err(sc->dev, "request irq for irq=%d failed, ret =%d\n",
				sc->irq, ret);
			return ret;
		}
		enable_irq_wake(sc->irq);
	}

	return ret;
}

static ssize_t sc851x_show_registers(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct sc851x_chip *sc = dev_get_drvdata(dev);
	u8 addr;
	unsigned int val = 0;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	memset(tmpbuf, 0, sizeof(tmpbuf));

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc851x");
	for (addr = 0x0; addr <= SC851X_REG_MAX; addr++) {
		ret = regmap_read(sc->regmap, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t sc851x_store_register(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct sc851x_chip *sc = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val = 0;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= SC851X_REG_MAX)
		regmap_write(sc->regmap, reg, val);

	return count;
}

static DEVICE_ATTR(registers, 0660, sc851x_show_registers,
		   sc851x_store_register);

static __always_inline int sc851x_parse_dt(struct sc851x_chip *sc,
					   struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret;
	int i;
	struct sc851x_dt_prop props[] = {
		{ "sc,sc851x,v2x-ovp-dis",	&sc->cfg.v2x_ovp_dis },
		{ "sc,sc851x,v2x-ovp",		&sc->cfg.v2x_ovp },
		{ "sc,sc851x,v2x-uvlo",		&sc->cfg.v2x_uvlo },
		{ "sc,sc851x,v2x-s2f",		&sc->cfg.v2x_s2f },
		{ "sc,sc851x,uvlo-deg",		&sc->cfg.uvlo_deg },
		{ "sc,sc851x,v1x-ovp-dis",	&sc->cfg.v1x_ovp_dis },
		{ "sc,sc851x,v1x-ovp",		&sc->cfg.v1x_ovp },
		{ "sc,sc851x,v1x-scp-dis",	&sc->cfg.v1x_scp_dis },
		{ "sc,sc851x,v1x-scp-f",	&sc->cfg.v1x_scp_f },
		{ "sc,sc851x,vac-ovp-dis",	&sc->cfg.vac_ovp_dis },
		{ "sc,sc851x,vac-ovp",		&sc->cfg.vac_ovp },
		{ "sc,sc851x,rvs-ocp",		&sc->cfg.rvs_ocp },
		{ "sc,sc851x,fwd-ocp",		&sc->cfg.fwd_ocp },
		{ "sc,sc851x,wdt",		&sc->cfg.wdt },
		{ "sc,sc851x,lnc-ss-timeout-dis", &sc->cfg.lnc_ss_timeout_dis },
		{ "sc,sc851x,lnc-ss-timeout",	&sc->cfg.lnc_ss_timeout },
		{ "sc,sc851x,ss-timeout-dis",	&sc->cfg.ss_timeout_dis },
		{ "sc,sc851x,audio-en",		&sc->cfg.audio_en },
		{ "sc,sc851x,audio-freq",	&sc->cfg.audio_freq },
		{ "sc,sc851x,audio-into-dg",	&sc->cfg.audio_into_dg },
		{ "sc,sc851x,freq",		&sc->cfg.freq },
		{ "sc,sc851x,ref-skip-r",	&sc->cfg.ref_skip_r },
		{ "sc,sc851x,skip-hyst",	&sc->cfg.skip_hyst },
		{ "sc,sc851x,t-vac-ovp-dg",	&sc->cfg.t_vac_ovp_dg },
		{ "sc,sc851x,t-v2x-ovp-dg",	&sc->cfg.t_v2x_ovp_dg },
		{ "sc,sc851x,t-v1x-ovp-dg",	&sc->cfg.t_v1x_ovp_dg },
		{ "sc,sc851x,t-fwd-ocp-dg",	&sc->cfg.t_fwd_ocp_dg },
		{ "sc,sc851x,t-rvs-ocp-dg",	&sc->cfg.t_rvs_ocp_dg },
		{ "sc,sc851x,t-v2x2vad-lnc-max-dg",
					&sc->cfg.t_v2x2vad_lnc_max_dg },
		{ "sc,sc851x,t-t3-set",		&sc->cfg.t_t3_set },
		{ "sc,sc851x,fwd-ocp-dis",	&sc->cfg.fwd_ocp_dis },
		{ "sc,sc851x,rvs-ocp-dis",	&sc->cfg.rvs_ocp_dis },
		{ "sc,sc851x,tshut-dis",	&sc->cfg.tshut_dis },
		{ "sc,sc851x,v1x-oss-opp-dis",	&sc->cfg.v1x_oss_opp_dis },
		{ "sc,sc851x,fam-en",		&sc->cfg.fam_en },
	};

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = of_property_read_u32(np, props[i].name,
					   props[i].conv_data);
		if (ret < 0) {
			dev_err(sc->dev, "can not read %s \n", props[i].name);
			return ret;
		}
	}

	sc->irq_gpio = of_get_named_gpio(np, "sc,sc851x,irq-gpio", 0);
	if (!gpio_is_valid(sc->irq_gpio)) {
		dev_err(sc->dev, "fail to valid gpio : %d\n", sc->irq_gpio);
		return -EINVAL;
	}

	return 0;
}

static int sc851x_charger_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct sc851x_chip *sc;
	int ret;
	int i;

	dev_err(dev, "%s (%s)\n", __func__, SC851X_DRV_VERSION);

	sc = devm_kzalloc(dev, sizeof(*sc), GFP_KERNEL);
	if (!sc) {
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	sc->dev = dev;
	sc->client = client;

	sc->regmap = devm_regmap_init_i2c(client, &sc851x_regmap_config);
	if (IS_ERR(sc->regmap)) {
		dev_err(sc->dev, "Failed to initialize regmap\n");
		ret = PTR_ERR(sc->regmap);
		goto err_free;
	}

	for (i = 0; i < ARRAY_SIZE(sc851x_reg_fields); i++) {
		const struct reg_field *reg_fields = sc851x_reg_fields;

		sc->rmap_fields[i] = devm_regmap_field_alloc(sc->dev,
							     sc->regmap,
							     reg_fields[i]);
		if (IS_ERR(sc->rmap_fields[i])) {
			dev_err(sc->dev, "cannot allocate regmap field\n");
			ret = PTR_ERR(sc->rmap_fields[i]);
			goto err_free;
		}
	}

	i2c_set_clientdata(client, sc);
	device_create_file(&(client->dev), &dev_attr_registers);

	ret = sc851x_parse_dt(sc, dev);
	if (ret < 0) {
		dev_err(sc->dev, "%s parse dt failed(%d)\n", __func__, ret);
		goto err_free;
	}

	ret = sc851x_init_device(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s init device failed(%d)\n", __func__, ret);
		goto err_free;
	}

	ret = sc851x_register_interrupt(sc);
	if (ret < 0) {
		dev_err(sc->dev, "%s register irq fail(%d)\n", __func__, ret);
		goto err_free;
	}

	dev_err(sc->dev, "sc851x probe successfully!\n");

	return 0;

err_free:
err_kzalloc:
	dev_err(dev, "sc851x probe fail\n");

	return ret;
}

static void sc851x_charger_remove(struct i2c_client *client)
{
	device_remove_file(&client->dev, &dev_attr_registers);
}

static void sc851x_charger_shutdown(struct i2c_client *client)
{
	struct sc851x_chip *sc = i2c_get_clientdata(client);

	sc851x_field_write(sc, SC851X_F_AUDIO_EN, 0);
	printk("sc851x_charger_shutdown: close AUDIO_EN\n");
}

static int sc851x_suspend(struct device *dev)
{
	struct sc851x_chip *sc = dev_get_drvdata(dev);

	dev_info(sc->dev, "Suspend successfully!");

	if (device_may_wakeup(dev))
		irq_set_irq_wake(sc->irq, 1);
	disable_irq(sc->irq);

	return 0;
}

static int sc851x_resume(struct device *dev)
{
	struct sc851x_chip *sc = dev_get_drvdata(dev);

	dev_info(sc->dev, "Resume successfully!");

	if (device_may_wakeup(dev))
		irq_set_irq_wake(sc->irq, 0);
	enable_irq(sc->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(sc851x_pm, sc851x_suspend, sc851x_resume);

static struct of_device_id sc851x_charger_match_table[] = {
	{ .compatible = "sc,sc851x", },
	{ .compatible = "sc,sc8510", },
	{ .compatible = "sc,sc8517", },
	{},
};

static struct i2c_driver sc851x_charger_driver = {
	.driver = {
		.name = "sc851x",
		.owner = THIS_MODULE,
		.of_match_table = sc851x_charger_match_table,
		.pm = &sc851x_pm,
	},
	.probe = sc851x_charger_probe,
	.remove = sc851x_charger_remove,
	.shutdown = sc851x_charger_shutdown,
};

module_i2c_driver(sc851x_charger_driver);

MODULE_DESCRIPTION("SC SC851X Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("South Chip <Aiden-yu@southchip.com>");
