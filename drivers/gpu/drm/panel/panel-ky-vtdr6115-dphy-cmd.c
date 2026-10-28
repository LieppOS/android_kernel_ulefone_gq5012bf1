// SPDX-License-Identifier: GPL-2.0
/*
 * Reconstruction of the Ulefone Armor 29 Pro Thermal (GQ5012BF1 / MT6878)
 * stock display-panel module:
 *
 *     panel-ky-vtdr6115-dphy-cmd.ko
 *     name        panel_ky_vtdr6115_dphy_cmd
 *     sha256      1e5778f3612378dd6fc3ba96ca461d8ba0661fc6617290a7c64245ede3f3af07
 *     BuildID     c86ee66361fe30b7b27e0c1a11edb1b81eafd2b0
 *     vermagic    6.1.115-android14-11-g945dff7bc1bf
 *
 * Every command byte, every delay, every GPIO transition and every panel-ext
 * parameter below was recovered from that binary.  Nothing here was taken from
 * the Motorola CSOT 144 Hz VDO donor except where the donor independently
 * matched the stock bytes (only the 8 bpp DSC PPS payload does).
 *
 * The reconstructed hardware values are used with the source-built MediaTek
 * V2 provider below, not the binary-only stock provider.  Share its canonical
 * callback/parameter declarations: the stock-layout model shifts callbacks
 * (including ext_param_get) and is not compatible with that provider.
 *
 * NOTE ON THE PANEL MODE: despite "cmd" in the module name and in the DT
 * compatible string, dsi->mode_flags in the stock binary is 0x0E05 =
 * MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
 * MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_CLOCK_NON_CONTINUOUS |
 * MIPI_DSI_MODE_LPM.  The panel runs in DSI VIDEO (sync-pulse) mode.  The
 * MODULE_DESCRIPTION ("vtdr6115 VDO 120HZ AMOLED Panel Driver") is correct;
 * the "cmd" token in the filename/compatible is a vendor misnomer.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
/* mtk_debug.h declares a DRM-plane helper; only its opaque type is needed. */
struct mtk_plane_state;
#include "../mediatek/mediatek_v2/mtk_debug.h"

/* ------------------------------------------------------------------------ */
/* Panel private context                                                     */
/*                                                                           */
/* sizeof == 112 (0x70) - proven from                                        */
/*     devm_kmalloc(&dsi->dev, 0x70, GFP_KERNEL | __GFP_ZERO)                */
/* in lcm_probe.  Member offsets proven from instruction displacements:      */
/*     0x00 dev, 0x08 panel, 0x38 backlight, 0x40 reset_gpio,                */
/*     0x50 dvdd_gpio, 0x58 vci_gpio, 0x60 prepared, 0x61 enabled,           */
/*     0x68 hbm_flag, 0x6C error.                                            */
/* 0x48 and 0x64 are never referenced by any stock instruction; they are     */
/* modelled here with the two plausible vendor members (the DT does carry an */
/* unconsumed powerdm-gpios and a gate-ic property).                         */
/* ------------------------------------------------------------------------ */
struct lcm {
	struct device *dev;			/* 0x00 */
	struct drm_panel panel;			/* 0x08 */
	struct backlight_device *backlight;	/* 0x38 */
	struct gpio_desc *reset_gpio;		/* 0x40 */
	struct gpio_desc *powerdm_gpio;		/* 0x48 - never referenced */
	struct gpio_desc *dvdd_gpio;		/* 0x50 */
	struct gpio_desc *vci_gpio;		/* 0x58 */
	bool prepared;				/* 0x60 */
	bool enabled;				/* 0x61 */
	unsigned int gate_ic;			/* 0x64 - never referenced */
	int hbm_flag;				/* 0x68 */
	int error;				/* 0x6C */
};

static_assert(sizeof(struct lcm) == 112, "struct lcm size");
static_assert(offsetof(struct lcm, panel) == 0x08, "lcm.panel");
static_assert(offsetof(struct lcm, backlight) == 0x38, "lcm.backlight");
static_assert(offsetof(struct lcm, reset_gpio) == 0x40, "lcm.reset_gpio");
static_assert(offsetof(struct lcm, dvdd_gpio) == 0x50, "lcm.dvdd_gpio");
static_assert(offsetof(struct lcm, vci_gpio) == 0x58, "lcm.vci_gpio");
static_assert(offsetof(struct lcm, prepared) == 0x60, "lcm.prepared");
static_assert(offsetof(struct lcm, enabled) == 0x61, "lcm.enabled");
static_assert(offsetof(struct lcm, hbm_flag) == 0x68, "lcm.hbm_flag");
static_assert(offsetof(struct lcm, error) == 0x6C, "lcm.error");

/* module-scope state (stock .data / .bss) */
static struct lcm *g_ctx;			/* .bss+0x00 */
static unsigned int current_backlight;		/* .bss+0x08 */
static struct class *primary_lcm_class;		/* .bss+0x10 */
static int last_hbm_backlight_value;		/* .bss+0x18 */
static unsigned int current_fps = 120;		/* .data+0xC8, init 0x78 */

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

/* ------------------------------------------------------------------------ */
/* DSI transport                                                             */
/*                                                                           */
/* Stock lcm_dcs_write: opcode <= 0xAF -> mipi_dsi_dcs_write_buffer,         */
/* opcode >= 0xB0 -> mipi_dsi_generic_write.  Both paths retry the write     */
/* exactly once after logging, and only latch ctx->error if the retry also   */
/* fails.  The retry ("seq_2") is a Ulefone/YFT deviation from the MediaTek  */
/* template and is reproduced verbatim.                                      */
/* ------------------------------------------------------------------------ */
static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0) {
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
		if (ret < 0) {
			dev_info(ctx->dev, "error %zd writing seq: %ph\n",
				 ret, data);
			ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
			if (ret < 0) {
				dev_info(ctx->dev,
					 "error %zd writing seq_2: %ph\n",
					 ret, data);
				ctx->error = ret;
			}
		}
	} else {
		ret = mipi_dsi_generic_write(dsi, data, len);
		if (ret < 0) {
			dev_info(ctx->dev,
				 "generic_write error %zd writing seq: %ph\n",
				 ret, data);
			ret = mipi_dsi_generic_write(dsi, data, len);
			if (ret < 0) {
				dev_info(ctx->dev,
					 "generic_write error %zd writing seq_2: %ph\n",
					 ret, data);
				ctx->error = ret;
			}
		}
	}
}

#define lcm_dcs_write_seq_static(ctx, seq...)                                  \
	({                                                                     \
		static const u8 d[] = { seq };                                 \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

/* ------------------------------------------------------------------------ */
/* Panel initialisation - .rodata command tables, call order proven from the */
/* relocation stream of lcm_panel_init                                       */
/* ------------------------------------------------------------------------ */
static void lcm_panel_init(struct lcm *ctx)
{
	u8 bl_tb0[] = { 0x51, 0x07, 0xFF };

	/* reset L(15ms) - H(15ms) - L(15ms) - H(20ms) */
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(15 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(15 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(15 * 1000, 15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(20 * 1000, 20 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	printk("%s+ yft-lcm-vtdr6115-drv- start reset_gpio H-L-H,%d\n",
	       __func__, current_fps);

	lcm_dcs_write_seq_static(ctx, 0x03, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x20);
	lcm_dcs_write_seq_static(ctx, 0x51, 0x00, 0xFF);
	lcm_dcs_write_seq_static(ctx, 0x6F, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x59, 0x00);

	if (current_fps == 60)
		lcm_dcs_write_seq_static(ctx, 0x6C, 0x00);
	else if (current_fps == 120)
		lcm_dcs_write_seq_static(ctx, 0x6C, 0x02);
	else if (current_fps == 90)
		lcm_dcs_write_seq_static(ctx, 0x6C, 0x01);

	lcm_dcs_write_seq_static(ctx, 0x6D, 0x00);

	/* DSC 1.1 PPS, 8 bpp, 1080x2400, slice 540x12 */
	lcm_dcs_write_seq_static(ctx, 0x70,
		0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x09, 0x60,
		0x04, 0x38, 0x00, 0x0C, 0x02, 0x1C, 0x02, 0x1C,
		0x02, 0x00, 0x02, 0x0E, 0x00, 0x20, 0x01, 0x1F,
		0x00, 0x07, 0x00, 0x0C, 0x08, 0xBB, 0x08, 0x7A,
		0x18, 0x00, 0x10, 0xF0, 0x03, 0x0C, 0x20, 0x00,
		0x06, 0x0B, 0x0B, 0x33, 0x0E, 0x1C, 0x2A, 0x38,
		0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7B,
		0x7D, 0x7E, 0x01, 0x02, 0x01, 0x00, 0x09, 0x40,
		0x09, 0xBE, 0x19, 0xFC, 0x19, 0xFA, 0x19, 0xF8,
		0x1A, 0x38, 0x1A, 0x78, 0x1A, 0xB6, 0x2A, 0xB6,
		0x2A, 0xF4, 0x2A, 0xF4, 0x4B, 0x34, 0x63, 0x74,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

	lcm_dcs_write_seq_static(ctx, 0xF0, 0xAA, 0x19);
	lcm_dcs_write_seq_static(ctx, 0xD0, 0x01, 0x1F, 0x1E, 0x1F, 0x1E, 0x1F, 0x1E);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0xAA, 0x18);
	lcm_dcs_write_seq_static(ctx, 0xB0, 0x13);
	lcm_dcs_write_seq_static(ctx, 0xB2, 0x13);
	lcm_dcs_write_seq_static(ctx, 0xFF, 0x5A, 0x81);
	lcm_dcs_write_seq_static(ctx, 0x65, 0x03);
	lcm_dcs_write_seq_static(ctx, 0xF4, 0x03);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0xAA, 0x12);
	lcm_dcs_write_seq_static(ctx, 0x65, 0x09);
	lcm_dcs_write_seq_static(ctx, 0xC5, 0x07);
	lcm_dcs_write_seq_static(ctx, 0xFF, 0x5A, 0x80);
	lcm_dcs_write_seq_static(ctx, 0x65, 0x0E);
	lcm_dcs_write_seq_static(ctx, 0xF9, 0xB9);
	lcm_dcs_write_seq_static(ctx, 0xFF, 0x5A, 0x81);
	lcm_dcs_write_seq_static(ctx, 0x65, 0x08);
	lcm_dcs_write_seq_static(ctx, 0xF6, 0x51, 0x44);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0xAA, 0x12);
	lcm_dcs_write_seq_static(ctx, 0x65, 0x02);
	lcm_dcs_write_seq_static(ctx, 0xBB, 0x10);
	lcm_dcs_write_seq_static(ctx, 0x55, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0xAA, 0x11);
	lcm_dcs_write_seq_static(ctx, 0xBA, 0x82, 0x10);
	lcm_dcs_write_seq_static(ctx, 0x65, 0x01);
	lcm_dcs_write_seq_static(ctx, 0xB0, 0x81);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0xAA, 0x10);
	lcm_dcs_write_seq_static(ctx, 0xD0,
		0x84, 0x16, 0x50, 0x14, 0x14, 0x00, 0x29, 0x00,
		0x13, 0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x00,
		0x00, 0x00, 0x06, 0x06, 0x13, 0x13);
	lcm_dcs_write_seq_static(ctx, 0xD1,
		0x84, 0x15, 0x50, 0x14, 0x14, 0x00, 0x39, 0x2C,
		0x0D, 0x32, 0x00, 0x00, 0x2C, 0x23, 0x32, 0x00,
		0x00, 0x00, 0x05, 0x05, 0x0D, 0x0D);

	bl_tb0[1] = (current_backlight >> 8) & 0xF;
	bl_tb0[2] = current_backlight & 0xFF;
	lcm_dcs_write(ctx, bl_tb0, ARRAY_SIZE(bl_tb0));

	lcm_dcs_write_seq_static(ctx, 0xF0, 0xAA, 0x10);
	lcm_dcs_write_seq_static(ctx, 0xB1,
		0x01, 0x6D, 0x00, 0x18, 0x09, 0xE0, 0x00, 0x01,
		0x6D, 0x00, 0x18, 0x03, 0x76, 0x00, 0x01, 0x6D,
		0x00, 0x18, 0x00, 0x3C, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xB2,
		0x01, 0x6D, 0x00, 0x18, 0x00, 0x3C, 0x03, 0x01,
		0x6D, 0x00, 0x18, 0x00, 0x3C, 0x03, 0x01, 0x6D,
		0x00, 0x18, 0x00, 0x3C, 0x03);
	lcm_dcs_write_seq_static(ctx, 0xFF, 0x5A, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xF0, 0xAA, 0x00);

	/* Sleep Out */
	lcm_dcs_write_seq_static(ctx, 0x11, 0x00);
	msleep(120);

	/* Display On */
	lcm_dcs_write_seq_static(ctx, 0x29, 0x00);
	msleep(50);

	pr_info("%s-\n", __func__);
}

/* ------------------------------------------------------------------------ */
/* drm_panel operations                                                      */
/* ------------------------------------------------------------------------ */
static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	printk("%s+ yft-lcm-vtdr6115-drv- start \n", __func__);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;

	printk("%s+ yft-lcm-vtdr6115-drv- start \n", __func__);

	lcm_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(20);
	lcm_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(200);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	pr_info("yft-drv- %s reset_gpio  is off !\n", __func__);
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	msleep(10);

	ctx->vci_gpio = devm_gpiod_get(ctx->dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_err(ctx->dev, "%s: cannot get vci_gpio %ld\n",
			__func__, PTR_ERR(ctx->vci_gpio));
		return PTR_ERR(ctx->vci_gpio);
	}
	pr_info("yft-drv- %s vci_gpio is off !\n", __func__);
	gpiod_set_value(ctx->vci_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->vci_gpio);
	msleep(10);

	ctx->dvdd_gpio = devm_gpiod_get(ctx->dev, "dvdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->dvdd_gpio)) {
		dev_err(ctx->dev, "%s: cannot get dvdd_gpio %ld\n",
			__func__, PTR_ERR(ctx->dvdd_gpio));
		return PTR_ERR(ctx->dvdd_gpio);
	}
	pr_info("yft-drv- %s dvdd_pin is off !\n", __func__);
	gpiod_set_value(ctx->dvdd_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->dvdd_gpio);

	ctx->error = 0;
	ctx->prepared = false;
	msleep(10);

	printk("%s+ yft-lcm-vtdr6115-drv- end \n", __func__);

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s+ yft-lcm-vtdr6115-drv- start\n", __func__);

	if (ctx->prepared)
		return 0;

	ctx->dvdd_gpio = devm_gpiod_get(ctx->dev, "dvdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->dvdd_gpio)) {
		dev_err(ctx->dev, "%s: cannot get dvdd_gpio %ld\n",
			__func__, PTR_ERR(ctx->dvdd_gpio));
		return PTR_ERR(ctx->dvdd_gpio);
	}
	pr_info("yft-drv- %s dvdd_pin is on !\n", __func__);
	gpiod_set_value(ctx->dvdd_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->dvdd_gpio);

	msleep(10);

	ctx->vci_gpio = devm_gpiod_get(ctx->dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_err(ctx->dev, "%s: cannot get vci_gpio %ld\n",
			__func__, PTR_ERR(ctx->vci_gpio));
		return PTR_ERR(ctx->vci_gpio);
	}
	pr_info("yft-drv- %s vci_gpio is on !\n", __func__);
	gpiod_set_value(ctx->vci_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->vci_gpio);

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

	pr_info("%s- yft-lcm-vtdr6115-drv- end\n", __func__);

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	printk("%s+ yft-lcm-vtdr6115-drv- start \n", __func__);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

/* ------------------------------------------------------------------------ */
/* Display modes                                                             */
/*                                                                           */
/* Exact stock .rodata values.  1080x2400, htotal 1204 (hfp 100 / hsa 8 /    */
/* hbp 16); the three refresh rates are produced purely by vfp:              */
/*     60 Hz  vfp 2528, vtotal 4952                                          */
/*     90 Hz  vfp  886, vtotal 3310                                          */
/*    120 Hz  vfp   60, vtotal 2484                                          */
/* All three .clock values equal htotal*vtotal*fps/1000 exactly.             */
/* ------------------------------------------------------------------------ */
static const struct drm_display_mode default_mode = {
	.clock		= 357732,
	.hdisplay	= 1080,
	.hsync_start	= 1080 + 100,
	.hsync_end	= 1080 + 100 + 8,
	.htotal		= 1080 + 100 + 8 + 16,
	.vdisplay	= 2400,
	.vsync_start	= 2400 + 2528,
	.vsync_end	= 2400 + 2528 + 4,
	.vtotal		= 2400 + 2528 + 4 + 20,
};

static const struct drm_display_mode performance_mode_90hz = {
	.clock		= 358671,
	.hdisplay	= 1080,
	.hsync_start	= 1080 + 100,
	.hsync_end	= 1080 + 100 + 8,
	.htotal		= 1080 + 100 + 8 + 16,
	.vdisplay	= 2400,
	.vsync_start	= 2400 + 886,
	.vsync_end	= 2400 + 886 + 4,
	.vtotal		= 2400 + 886 + 4 + 20,
};

static const struct drm_display_mode performance_mode_120hz = {
	.clock		= 358888,
	.hdisplay	= 1080,
	.hsync_start	= 1080 + 100,
	.hsync_end	= 1080 + 100 + 8,
	.htotal		= 1080 + 100 + 8 + 16,
	.vdisplay	= 2400,
	.vsync_start	= 2400 + 60,
	.vsync_end	= 2400 + 60 + 4,
	.vtotal		= 2400 + 60 + 4 + 20,
};

/* ------------------------------------------------------------------------ */
/* MediaTek panel extension parameters                                       */
/* ------------------------------------------------------------------------ */
static unsigned int vtdr6115_vdo_120hz_dphy_buf_thresh[14] = {
	896, 1792, 2688, 3584, 4480, 5376, 6272, 6720,
	7168, 7616, 7744, 7872, 8000, 8064
};

static unsigned int vtdr6115_vdo_120hz_dphy_range_min_qp[15] = {
	0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 9, 12
};

static unsigned int vtdr6115_vdo_120hz_dphy_range_max_qp[15] = {
	4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 10, 11, 11, 12, 13
};

static int vtdr6115_vdo_120hz_dphy_range_bpg_ofs[15] = {
	2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12
};

#define VTDR6115_EXT_PARAMS_COMMON					\
	.pll_clk = 435,							\
	.data_rate = 870,						\
	.vdo_per_frame_lp_enable = 1,					\
	.change_fps_by_vfp_send_cmd = 1,				\
	.cust_esd_check = 1,						\
	.esd_check_enable = 1,						\
	.lcm_esd_check_table[0] = {					\
		.cmd = 0x0A,						\
		.count = 1,						\
		.para_list[0] = 0x9C,					\
	},								\
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,			\
	.lp_perline_en = 1,						\
	.dsc_params = {							\
		.enable = 1,						\
		.ver = 17,						\
		.slice_mode = 1,					\
		.rgb_swap = 0,						\
		.dsc_cfg = 34,						\
		.rct_on = 1,						\
		.bit_per_channel = 8,					\
		.dsc_line_buf_depth = 9,				\
		.bp_enable = 1,						\
		.bit_per_pixel = 128,					\
		.pic_height = 2400,					\
		.pic_width = 1080,					\
		.slice_height = 12,					\
		.slice_width = 540,					\
		.chunk_size = 540,					\
		.xmit_delay = 512,					\
		.dec_delay = 526,					\
		.scale_value = 32,					\
		.increment_interval = 287,				\
		.decrement_interval = 7,				\
		.line_bpg_offset = 12,					\
		.nfl_bpg_offset = 2235,					\
		.slice_bpg_offset = 2170,				\
		.initial_offset = 6144,					\
		.final_offset = 4336,					\
		.flatness_minqp = 3,					\
		.flatness_maxqp = 12,					\
		.rc_model_size = 8192,					\
		.rc_edge_factor = 6,					\
		.rc_quant_incr_limit0 = 11,				\
		.rc_quant_incr_limit1 = 11,				\
		.rc_tgt_offset_hi = 3,					\
		.rc_tgt_offset_lo = 3,					\
		.ext_pps_cfg = {					\
			.enable = 1,					\
			.rc_buf_thresh = vtdr6115_vdo_120hz_dphy_buf_thresh,   \
			.range_min_qp = vtdr6115_vdo_120hz_dphy_range_min_qp,  \
			.range_max_qp = vtdr6115_vdo_120hz_dphy_range_max_qp,  \
			.range_bpg_ofs = vtdr6115_vdo_120hz_dphy_range_bpg_ofs,\
		},							\
	}

static struct mtk_panel_params ext_params = {
	VTDR6115_EXT_PARAMS_COMMON,
	.dyn_fps.dfps_cmd_table[0] = {
		.cmd_num = 2,
		.para_list = { 0x6C, 0x00 },
	},
};

static struct mtk_panel_params ext_params_90hz = {
	VTDR6115_EXT_PARAMS_COMMON,
	.dyn_fps.dfps_cmd_table[0] = {
		.cmd_num = 2,
		.para_list = { 0x6C, 0x01 },
	},
};

static struct mtk_panel_params ext_params_120hz = {
	VTDR6115_EXT_PARAMS_COMMON,
	.dyn_fps.dfps_cmd_table[0] = {
		.cmd_num = 2,
		.para_list = { 0x6C, 0x02 },
	},
};

/* ------------------------------------------------------------------------ */
/* panel-ext callbacks                                                       */
/* ------------------------------------------------------------------------ */

/* NOTE: stock always returns 1 - there is no runtime ATA/ID readback. */
static int panel_ata_check(struct drm_panel *panel)
{
	return 1;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	u8 bl_tb0[] = { 0x51, 0x07, 0xFF };

	if (level <= 21) {
		pr_info("%s level=%d, limit=%d\n", __func__, level, 22);
		level = 22;
	}

	bl_tb0[1] = (level >> 8) & 0xF;
	bl_tb0[2] = level & 0xFF;

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	current_backlight = level;

	pr_info("%s %d %d %d\n", __func__, level, bl_tb0[1], bl_tb0[2]);

	return 0;
}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s:yft-lcm-vtdr6115-drv \n", __func__);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

struct drm_display_mode *get_mode_by_id(struct drm_connector *connector,
					unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}

enum RES_SWITCH_TYPE mtk_get_res_switch_type(void)
{
	pr_info("%s:yft-lcm-vtdr6115-drv\n", __func__);
	return RES_SWITCH_NO_USE;
}

int mtk_scaling_mode_mapping(int mode_idx)
{
	pr_info("%s:yft-lcm-vtdr6115-drv\n", __func__);
	return mode_idx % 3;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel,
				   struct drm_connector *connector,
				   unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);
	int dst_fps = m ? drm_mode_vrefresh(m) : -EINVAL;

	printk("%s+ yft-lcm-vtdr6115-drv- start dst_fps=%d\n",
	       __func__, dst_fps);

	if (dst_fps == 60)
		ext->params = &ext_params;
	else if (dst_fps == 90)
		ext->params = &ext_params_90hz;
	else if (dst_fps == 120 || dst_fps == 30)
		ext->params = &ext_params_120hz;
	else {
		pr_err("%s, dst_fps %d\n", __func__, dst_fps);
		return -EINVAL;
	}

	current_fps = drm_mode_vrefresh(m);

	return ret;
}

static int mtk_panel_ext_param_get(struct drm_panel *panel,
				   struct drm_connector *connector,
				   struct mtk_panel_params **ext_para,
				   unsigned int mode)
{
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	if (drm_mode_vrefresh(m) == 120)
		*ext_para = &ext_params_120hz;
	else if (drm_mode_vrefresh(m) == 90)
		*ext_para = &ext_params_90hz;
	else if (drm_mode_vrefresh(m) == 60)
		*ext_para = &ext_params;
	else
		return 1;

	current_fps = drm_mode_vrefresh(m);

	return 0;
}

/*
 * Stock mode_switch sends NO DSI command.  It validates the destination mode
 * and logs; the actual rate change is realised by the vfp of the selected
 * drm_display_mode plus the ext_params selected by mtk_panel_ext_param_set.
 */
static int mode_switch(struct drm_panel *panel,
		       struct drm_connector *connector, unsigned int cur_mode,
		       unsigned int dst_mode,
		       enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, dst_mode);
	int dst_fps;

	pr_info("%s:yft-lcm-vtdr6115-drv cur_mode = %d dst_mode %d\n",
		__func__, cur_mode, dst_mode);

	if (!m) {
		ret = -EINVAL;
		pr_err("%s, dst_fps %d\n", __func__, ret);
		return ret;
	}

	dst_fps = drm_mode_vrefresh(m);

	if (dst_fps == 60 || dst_fps == 90 || dst_fps == 120)
		return 0;

	pr_err("%s, dst_fps %d\n", __func__, dst_fps);

	return -EINVAL;
}

/*
 * HBM: 0x51 with the 12-bit maximum (0x0FFF) when enabled, 0x03FF when
 * disabled.  There is no dedicated vendor HBM register on this panel.
 */
static int sethbm_cmdq(struct drm_panel *panel, void *dsi, dcs_write_gce cb,
		       void *handle, bool en)
{
	u8 bl_tb0[] = { 0x51, 0x07, 0xFF };

	pr_info("%s, yft-lcm-vtdr6115-drv en=%d\n", __func__, en);

	if (!cb)
		return -1;

	bl_tb0[1] = en ? 0x0F : 0x03;
	bl_tb0[2] = 0xFF;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

static int mtk_set_value(int value)
{
	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.reset = panel_ext_reset,
	.ata_check = panel_ata_check,
	.ext_param_set = mtk_panel_ext_param_set,
	.ext_param_get = mtk_panel_ext_param_get,
	.get_res_switch_type = mtk_get_res_switch_type,
	.scaling_mode_mapping = mtk_scaling_mode_mapping,
	.mode_switch = mode_switch,
	.hbm_set_cmdq = sethbm_cmdq,
	.set_value = mtk_set_value,
};

/* ------------------------------------------------------------------------ */
/* sysfs: /sys/class/primary_lcm/set_hbm_backlight                           */
/* Fingerprint local-HBM path via mediatek-drm's ddic_dsi_send_cmd_for_fp.   */
/* ------------------------------------------------------------------------ */
static ssize_t set_hbm_backlight_show(struct class *class,
				      struct class_attribute *attr, char *buf)
{
	struct lcm *ctx = g_ctx;

	pr_info("[%s] yft-lcm-vtdr6115-drv- yft_hbm_flag = %d\n",
		__func__, ctx->hbm_flag);
	return sprintf(buf, "%d\n", ctx->hbm_flag);
}

static ssize_t set_hbm_backlight_store(struct class *class,
				       struct class_attribute *attr,
				       const char *buf, size_t count)
{
	struct lcm *ctx = g_ctx;
	int databuf[16] = { 0 };
	u8 bl_tb0[3];
	unsigned int level = 0;

	sscanf(buf, "%d", &databuf[0]);

	if (last_hbm_backlight_value == databuf[0]) {
		pr_info("%s yft-lcm-vtdr6115-drv-! ignore hbm_backlight\n",
			__func__);
		return count;
	}

	if (databuf[0] == 1) {
		bl_tb0[1] = 0x0F;
		bl_tb0[2] = 0xFF;
		level = 0;
	} else if (databuf[0] == 0) {
		level = current_backlight;
		bl_tb0[1] = (current_backlight >> 8) & 0xF;
		bl_tb0[2] = current_backlight & 0xFF;
	} else {
		bl_tb0[1] = 0x00;
		bl_tb0[2] = 0xFF;
		level = 0;
	}

	pr_info("%s yft-lcm-vtdr6115-drv-! databuf[0]=%d,[0x%x,0x%x], level=%d\n",
		__func__, databuf[0], bl_tb0[1], bl_tb0[2], level);

	bl_tb0[0] = 0x51;
	ddic_dsi_send_cmd_for_fp(bl_tb0, 3);

	ctx->hbm_flag = databuf[0];
	last_hbm_backlight_value = databuf[0];

	return count;
}

static struct class_attribute class_attr_primary_lcm[] = {
	__ATTR(set_hbm_backlight, 0664,
	       set_hbm_backlight_show, set_hbm_backlight_store),
	__ATTR_NULL,
};

/* ------------------------------------------------------------------------ */
/* get_modes / probe / remove                                                */
/* ------------------------------------------------------------------------ */
static int lcm_get_modes(struct drm_panel *panel,
			 struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode_1;
	struct drm_display_mode *mode_2;

	pr_info("%s+ yft-lcm-vtdr6115-drv- add  11111\n", __func__);

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 default_mode.hdisplay, default_mode.vdisplay,
			 drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode);

	mode_1 = drm_mode_duplicate(connector->dev, &performance_mode_90hz);
	if (!mode_1) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_90hz.hdisplay,
			 performance_mode_90hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_90hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_1);
	mode_1->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_1);

	mode_2 = drm_mode_duplicate(connector->dev, &performance_mode_120hz);
	if (!mode_2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_120hz.hdisplay,
			 performance_mode_120hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_120hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode_2);
	mode_2->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode_2);

	connector->display_info.width_mm = 69;
	connector->display_info.height_mm = 155;

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node = NULL, *remote_node = NULL, *endpoint = NULL;
	struct device_node *backlight = NULL;
	struct lcm *ctx;
	unsigned int lcm_degree = 0;
	unsigned int rc_enable = 0;
	struct mtk_panel_params *ext_param_sel;
	struct mtk_panel_ctx *ext_ctx;
	bool attached = false;
	bool panel_added = false;
	int ret = 0;
	int i;

	pr_info("%s+ lcm,vtdr6115,vdo,120hz\n", __func__);

	dsi_node = of_get_parent(dev->of_node);
	if (!dsi_node)
		return -ENODEV;

	endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
	if (!endpoint) {
		ret = -ENODEV;
		goto out_nodes;
	}

	remote_node = of_graph_get_remote_port_parent(endpoint);
	if (!remote_node) {
		pr_info("No panel connected,skip probe lcm\n");
		ret = -ENODEV;
		goto out_nodes;
	}

	pr_info("device node name:%s\n", remote_node->name);
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		ret = -ENODEV;
		goto out_nodes;
	}

	of_node_put(remote_node);
	remote_node = NULL;
	of_node_put(endpoint);
	endpoint = NULL;

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto out_nodes;
	}

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;
	g_ctx = ctx;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
			 | MIPI_DSI_MODE_NO_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS
			 | MIPI_DSI_MODE_LPM;

	ret = of_property_read_u32(dev->of_node, "rc-enable", &rc_enable);
	if (ret < 0) {
		rc_enable = 0;
	} else {
		ext_params.round_corner_en = rc_enable;
		ext_params_90hz.round_corner_en = rc_enable;
		ext_params_120hz.round_corner_en = rc_enable;

	}
	pr_info("%s+ round_corner_en %d\n", __func__,
		ext_params.round_corner_en);

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		backlight = NULL;
		if (!ctx->backlight) {
			ret = -EPROBE_DEFER;
			goto out_nodes;
		}
	}

	pr_info("%s+ yft-lcm-vtdr6115-drv- add  11111\n", __func__);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		ret = PTR_ERR(ctx->reset_gpio);
		goto out_backlight;
	}
	current_backlight = 2047;
	devm_gpiod_put(dev, ctx->reset_gpio);

	ctx->dvdd_gpio = devm_gpiod_get(dev, "dvdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->dvdd_gpio)) {
		dev_info(dev, "cannot get dvdd-gpios %ld\n",
			 PTR_ERR(ctx->dvdd_gpio));
		ret = PTR_ERR(ctx->dvdd_gpio);
		goto out_backlight;
	}
	devm_gpiod_put(dev, ctx->dvdd_gpio);

	ctx->vci_gpio = devm_gpiod_get(dev, "vci", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vci_gpio)) {
		dev_info(dev, "cannot get vci_gpio %ld\n",
			 PTR_ERR(ctx->vci_gpio));
		ret = PTR_ERR(ctx->vci_gpio);
		goto out_backlight;
	}
	devm_gpiod_put(dev, ctx->vci_gpio);

	pr_info("%s+ yft-lcm-vtdr6115-drv- add  2222\n", __func__);

	ctx->prepared = true;
	ctx->enabled = true;

	if (of_find_property(dsi_node, "init-panel-off", NULL)) {
		ctx->prepared = false;
		ctx->enabled = false;
		pr_info("vtdr6115,120hz dsi_node:%s set prepared = enabled = false\n",
			dsi_node->full_name);
	}
	of_node_put(dsi_node);
	dsi_node = NULL;

	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	drm_panel_add(&ctx->panel);
	panel_added = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		goto err_panel;
	attached = true;

	mtk_panel_tch_handle_reg(&ctx->panel);

	if (current_fps == 90)
		ext_param_sel = &ext_params_90hz;
	else if (current_fps == 120)
		ext_param_sel = &ext_params_120hz;
	else
		ext_param_sel = &ext_params;

	ret = mtk_panel_ext_create(dev, ext_param_sel, &ext_funcs, &ctx->panel);
	if (ret < 0)
		goto err_attached;

	ret = of_property_read_u32(dev->of_node, "lcm-degree", &lcm_degree);
	if (ret < 0)
		lcm_degree = 0;
	else
		ext_param_sel->lcm_degree = lcm_degree;
	pr_info("lcm_degree: %d\n", ext_param_sel->lcm_degree);

	primary_lcm_class = class_create(THIS_MODULE, "primary_lcm");
	if (IS_ERR(primary_lcm_class)) {
		ret = PTR_ERR(primary_lcm_class);
		primary_lcm_class = NULL;
		pr_err("create class, err = %d\n", ret);
		goto err_ext;
	}

	for (i = 0; class_attr_primary_lcm[i].attr.name; i++) {
		ret = class_create_file(primary_lcm_class,
					&class_attr_primary_lcm[i]);
		if (ret) {
			pr_err("Fail class_create_file\n");
			while (--i >= 0)
				class_remove_file(primary_lcm_class,
						  &class_attr_primary_lcm[i]);
			class_destroy(primary_lcm_class);
			primary_lcm_class = NULL;
			goto err_ext;
		}
	}

	pr_info("%s- ,vtdr6115,vdo,120hz ret=%d\n", __func__, ret);
	return 0;

err_ext:
	ext_ctx = find_panel_ctx(&ctx->panel);
	if (ext_ctx) {
		mtk_panel_detach(ext_ctx);
		mtk_panel_remove(ext_ctx);
	}
err_attached:
	if (attached)
		mipi_dsi_detach(dsi);
err_panel:
	if (panel_added)
		drm_panel_remove(&ctx->panel);
out_backlight:
	if (ctx && ctx->backlight) {
		put_device(&ctx->backlight->dev);
		ctx->backlight = NULL;
	}
out_nodes:
	of_node_put(dsi_node);
	of_node_put(remote_node);
	of_node_put(endpoint);
	of_node_put(backlight);
	return ret;
}

static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
	struct mtk_panel_ctx *ext_ctx;

	pr_info("%s+ yft-lcm-vtdr6115-drv- add  11\n", __func__);

	if (!ctx)
		return;

	ext_ctx = find_panel_ctx(&ctx->panel);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
	if (ext_ctx) {
		mtk_panel_detach(ext_ctx);
		mtk_panel_remove(ext_ctx);
	}
	if (ctx->backlight) {
		put_device(&ctx->backlight->dev);
		ctx->backlight = NULL;
	}
	if (primary_lcm_class) {
		class_destroy(primary_lcm_class);
		primary_lcm_class = NULL;
	}
	g_ctx = NULL;
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "hx,vtdr6115,cmd,120hz,ky", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-ky-vtdr6115-dphy-cmd",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("shaohua deng <shaohua.deng@mediatek.com>");
MODULE_DESCRIPTION("vtdr6115 VDO 120HZ AMOLED Panel Driver");
MODULE_LICENSE("GPL v2");
