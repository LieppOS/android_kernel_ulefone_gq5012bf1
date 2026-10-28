/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk_panel_ext_stock_abi.h
 *
 * Stock-memory-layout-exact model of the MediaTek panel-extension API used by
 * Ulefone Armor 29 Pro Thermal (GQ5012BF1, MT6878) stock kernel module
 *     panel-ky-vtdr6115-dphy-cmd.ko
 *     vermagic 6.1.115-android14-11-g945dff7bc1bf
 *     BuildID  c86ee66361fe30b7b27e0c1a11edb1b81eafd2b0
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The exact MediaTek vendor header revision used to build the stock module is
 * not present in any locally available source tree.  The nearest available
 * revision (NothingOSS MT6878 device_modules
 * drivers/gpu/drm/mediatek/mediatek_v2/mtk_panel_ext.h) has:
 *
 *     sizeof(struct mtk_panel_params) = 69232   (stock: 66328)
 *     sizeof(struct mtk_panel_funcs)  =   336   (stock:   320)
 *     offsetof(round_corner_en)       =  5632   (stock:  0x0AAC = 2732)
 *     offsetof(lcm_degree)            =  6220   (stock:  0x0CFC = 3324)
 *     offsetof(reset)                 =    56   (stock:  0x0030 = slot 6)
 *
 * Building against the Nothing header would place every populated field and
 * every function pointer at the wrong offset, which the *stock*
 * mtk_panel_ext.ko provider would then misread.  That is a hardware-affecting
 * defect, not a cosmetic one.
 *
 * This header therefore models the stock layout directly.  Every offset below
 * is *proven* from the stock binary (relocation targets, instruction
 * displacements, and the byte content of the three 66328-byte ext_params
 * objects) and is enforced by static_assert at the bottom of the file.
 *
 * Fields whose stock name could not be proven are declared as explicit
 * reserved padding.  They are all-zero in the stock objects, so the byte image
 * is identical regardless of their true names.
 *
 * IMPORTANT: this is not the unavailable original type graph. Linux genksyms
 * hashes reachable type definitions, so this layout model does not reproduce
 * the seven stock vendor-provider CRCs and must not be used to claim a
 * deployable stock-provider ABI match.
 *
 * Names that ARE used below were confirmed by value/semantic correlation:
 *   +0x0000 pll_clk               = 435   (= data_rate / 2)
 *   +0x0004 data_rate             = 870   Mbps/lane, 4 lanes
 *   +0x000C/+0x0010               = 1, 1  (video-mode per-frame LP /
 *                                          change-fps-by-vfp-and-send-cmd)
 *   +0x0098 dfps cmd_num          = 2
 *   +0x009C dfps para_list        = { 0x6C, <00|01|02> }
 *   +0x0A48 cust_esd_check        = 1
 *   +0x0A4C esd_check_enable      = 1
 *   +0x0A50 lcm_esd_check_table[0]= { .cmd = 0x0A, .count = 1, .para = 0x9C }
 *   +0x0AAC round_corner_en       (written from DT "rc-enable" by lcm_probe)
 *   +0x0B30 dsc_params            (34 u32 + ext_pps_cfg, layout identical to
 *                                  the Nothing revision, size 208)
 *   +0x0CD0 output_mode           = 1  (MTK_PANEL_DSC_SINGLE_PORT)
 *   +0x0CFC lcm_degree            (written from DT "lcm-degree" by lcm_probe)
 */

#ifndef __MTK_PANEL_EXT_STOCK_ABI_H__
#define __MTK_PANEL_EXT_STOCK_ABI_H__

#include <linux/types.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <drm/drm_connector.h>

struct mtk_dsi;
struct cmdq_pkt;
struct mtk_panel_ctx;
struct mtk_panel_params;
struct mtk_ddic_dsi_cmd;
struct mtk_panel_para_table;
struct mtk_bl_ext_config;
struct DISP_PANEL_BASE_VOLTAGE;
struct mtk_oddmr_panelid;
struct mtk_lcm_dsi_cmd_packet;

typedef void (*dcs_write_gce)(struct mtk_dsi *dsi, struct cmdq_pkt *handle,
			      const void *data, size_t len);
typedef void (*dcs_write_gce_pack)(struct mtk_dsi *dsi, struct cmdq_pkt *handle,
				   struct mtk_ddic_dsi_cmd *para_table);
typedef void (*dcs_grp_write_gce)(struct mtk_dsi *dsi, struct cmdq_pkt *handle,
				  struct mtk_panel_para_table *para_table,
				  unsigned int para_size);

enum RES_SWITCH_TYPE {
	RES_SWITCH_NO_USE,
	RES_SWITCH_ON_DDIC,
	RES_SWITCH_ON_AP,
};

enum MTK_PANEL_MODE_SWITCH_STAGE {
	BEFORE_DSI_POWERDOWN,
	AFTER_DSI_POWERON,
};

enum MTK_PANEL_OUTPUT_PORT_MODE {
	MTK_PANEL_SINGLE_PORT = 0x0,
	MTK_PANEL_DSC_SINGLE_PORT,
	MTK_PANEL_DUAL_PORT,
};

#define MTK_ESD_CHECK_NUM	4
#define MTK_RT_MAX_NUM		10
#define MTK_DFPS_PARA_LIST_LEN	64

/* ------------------------------------------------------------------------ */
/* sub-structures (layout identical to the MediaTek vendor revision)         */
/* ------------------------------------------------------------------------ */

/* 22 bytes */
struct esd_check_item {
	unsigned char cmd;
	unsigned char count;
	unsigned char para_list[MTK_RT_MAX_NUM];
	unsigned char mask_list[MTK_RT_MAX_NUM];
};

/* 72 bytes */
struct dfps_switch_cmd {
	unsigned int src_fps;
	unsigned int cmd_num;
	unsigned char para_list[MTK_DFPS_PARA_LIST_LEN];
};

/* 72 bytes */
struct mtk_panel_dsc_ext_pps_cfg {
	unsigned int enable;
	unsigned int *rc_buf_thresh;
	unsigned int rc_buf_thresh_count;
	unsigned int *range_min_qp;
	unsigned int range_min_qp_count;
	unsigned int *range_max_qp;
	unsigned int range_max_qp_count;
	int *range_bpg_ofs;
	unsigned int range_bpg_ofs_count;
};

/* 208 bytes */
struct mtk_panel_dsc_params {
	unsigned int enable;
	unsigned int dual_dsc_enable;
	unsigned int ver;		/* [7:4] major [3:0] minor */
	unsigned int slice_mode;
	unsigned int rgb_swap;
	unsigned int dsc_cfg;
	unsigned int rct_on;
	unsigned int bit_per_channel;
	unsigned int dsc_line_buf_depth;
	unsigned int bp_enable;
	unsigned int bit_per_pixel;
	unsigned int pic_height;
	unsigned int pic_width;
	unsigned int slice_height;
	unsigned int slice_width;
	unsigned int chunk_size;
	unsigned int xmit_delay;
	unsigned int dec_delay;
	unsigned int scale_value;
	unsigned int increment_interval;
	unsigned int decrement_interval;
	unsigned int line_bpg_offset;
	unsigned int nfl_bpg_offset;
	unsigned int slice_bpg_offset;
	unsigned int initial_offset;
	unsigned int final_offset;
	unsigned int flatness_minqp;
	unsigned int flatness_maxqp;
	unsigned int rc_model_size;
	unsigned int rc_edge_factor;
	unsigned int rc_quant_incr_limit0;
	unsigned int rc_quant_incr_limit1;
	unsigned int rc_tgt_offset_hi;
	unsigned int rc_tgt_offset_lo;
	struct mtk_panel_dsc_ext_pps_cfg ext_pps_cfg;
};

/* ------------------------------------------------------------------------ */
/* struct mtk_panel_params - stock layout, total 66328 bytes                 */
/* ------------------------------------------------------------------------ */

struct mtk_panel_params {
	/* 0x0000 */ unsigned int pll_clk;
	/* 0x0004 */ unsigned int data_rate;
	/* 0x0008 */ unsigned int data_rate_khz;
	/* 0x000C */ unsigned int vdo_per_frame_lp_enable;
	/* 0x0010 */ unsigned int change_fps_by_vfp_send_cmd;
	/* 0x0014 */ unsigned char __resv_0x0014[0x0094 - 0x0014];
	/*
	 * 0x0094: dyn_fps.dfps_cmd_table[0].  The dfps table base was proven
	 * from the only field that differs between the three stock ext_params
	 * objects: cmd_num == 2 at +0x98 and para_list == {0x6C, code} at
	 * +0x9C, where code is 0x00 (60 Hz) / 0x01 (90 Hz) / 0x02 (120 Hz).
	 */
	/* 0x0094 */ struct dfps_switch_cmd dfps_cmd_table0;
	/* 0x00DC */ unsigned char __resv_0x00dc[0x0A48 - 0x00DC];
	/* 0x0A48 */ unsigned int cust_esd_check;
	/* 0x0A4C */ unsigned int esd_check_enable;
	/* 0x0A50 */ struct esd_check_item lcm_esd_check_table[MTK_ESD_CHECK_NUM];
	/* 0x0AA8 */ unsigned int ssc_enable;
	/* 0x0AAC */ unsigned int round_corner_en;
	/* 0x0AB0 */ unsigned char __resv_0x0ab0[0x0B30 - 0x0AB0];
	/* 0x0B30 */ struct mtk_panel_dsc_params dsc_params;
	/* 0x0C00 */ unsigned char __resv_0x0c00[0x0CD0 - 0x0C00];
	/* 0x0CD0 */ unsigned int output_mode;
	/* 0x0CD4 */ unsigned char __resv_0x0cd4[0x0CF0 - 0x0CD4];
	/* 0x0CF0 */ unsigned int lp_perline_en;
	/* 0x0CF4 */ unsigned char __resv_0x0cf4[0x0CFC - 0x0CF4];
	/* 0x0CFC */ unsigned int lcm_degree;
	/* 0x0D00 */ unsigned char __resv_0x0d00[66328 - 0x0D00];
} __aligned(8);

/* ------------------------------------------------------------------------ */
/* struct mtk_panel_funcs - stock layout, 40 pointers / 320 bytes            */
/*                                                                           */
/* Slots populated by the stock module (proven from .rela.data against       */
/* ext_funcs at .data+0xD0):                                                 */
/*   slot  2 (+0x010) set_backlight_cmdq   -> lcm_setbacklight_cmdq          */
/*   slot  6 (+0x030) reset                -> panel_ext_reset                */
/*   slot  7 (+0x038) ata_check            -> panel_ata_check                */
/*   slot  8 (+0x040) ext_param_set        -> mtk_panel_ext_param_set        */
/*   slot  9 (+0x048) ext_param_get        -> mtk_panel_ext_param_get        */
/*   slot 10 (+0x050) get_res_switch_type  -> mtk_get_res_switch_type        */
/*   slot 11 (+0x058) scaling_mode_mapping -> mtk_scaling_mode_mapping       */
/*   slot 12 (+0x060) mode_switch          -> mode_switch                    */
/*   slot 26 (+0x0D0) hbm_set_cmdq         -> sethbm_cmdq                    */
/*   slot 34 (+0x110) set_value            -> mtk_set_value                  */
/*                                                                           */
/* Relative to the Nothing revision the stock table is 2 pointers shorter:   */
/* one entry is absent somewhere in Nothing slots 0..6 and one in Nothing    */
/* slots 28..35.  Those entries are unpopulated by this panel, so the        */
/* unnamed reserved slots below are byte-exact NULLs either way.             */
/* ------------------------------------------------------------------------ */

struct mtk_panel_funcs {
	/*  0 */ int (*set_bl_elvss_cmdq)(void *dsi_drv, dcs_grp_write_gce cb,
					  void *handle,
					  struct mtk_bl_ext_config *bl_ext_config);
	/*  1 */ void *__resv_slot1;
	/*  2 */ int (*set_backlight_cmdq)(void *dsi_drv, dcs_write_gce cb,
					   void *handle, unsigned int level);
	/*  3 */ void *__resv_slot3;
	/*  4 */ void *__resv_slot4;
	/*  5 */ int (*set_backlight_grp_cmdq)(void *dsi_drv,
					       dcs_grp_write_gce cb,
					       void *handle, unsigned int level);
	/*  6 */ int (*reset)(struct drm_panel *panel, int on);
	/*  7 */ int (*ata_check)(struct drm_panel *panel);
	/*  8 */ int (*ext_param_set)(struct drm_panel *panel,
				      struct drm_connector *connector,
				      unsigned int mode);
	/*  9 */ int (*ext_param_get)(struct drm_panel *panel,
				      struct drm_connector *connector,
				      struct mtk_panel_params **ext_para,
				      unsigned int mode);
	/* 10 */ enum RES_SWITCH_TYPE (*get_res_switch_type)(void);
	/* 11 */ int (*scaling_mode_mapping)(int mode_idx);
	/* 12 */ int (*mode_switch)(struct drm_panel *panel,
				    struct drm_connector *connector,
				    unsigned int cur_mode, unsigned int dst_mode,
				    enum MTK_PANEL_MODE_SWITCH_STAGE stage);
	/* 13 */ void *__resv_slot13;
	/* 14 */ void *__resv_slot14;
	/* 15 */ void *__resv_slot15;
	/* 16 */ void *__resv_slot16;
	/* 17 */ void *__resv_slot17;
	/* 18 */ void *__resv_slot18;
	/* 19 */ void *__resv_slot19;
	/* 20 */ void *__resv_slot20;
	/* 21 */ void *__resv_slot21;
	/* 22 */ void *__resv_slot22;
	/* 23 */ void *__resv_slot23;
	/* 24 */ void *__resv_slot24;
	/* 25 */ void *__resv_slot25;
	/* 26 */ int (*hbm_set_cmdq)(struct drm_panel *panel, void *dsi_drv,
				     dcs_write_gce cb, void *handle, bool en);
	/* 27 */ void *__resv_slot27;
	/* 28 */ void *__resv_slot28;
	/* 29 */ void *__resv_slot29;
	/* 30 */ void *__resv_slot30;
	/* 31 */ void *__resv_slot31;
	/* 32 */ void *__resv_slot32;
	/* 33 */ void *__resv_slot33;
	/* 34 */ int (*set_value)(int value);
	/* 35 */ void *__resv_slot35;
	/* 36 */ void *__resv_slot36;
	/* 37 */ void *__resv_slot37;
	/* 38 */ void *__resv_slot38;
	/* 39 */ void *__resv_slot39;
};

struct mtk_panel_ext {
	struct mtk_panel_funcs *funcs;
	struct mtk_panel_params *params;
	int is_connected;
};

void mtk_panel_remove(struct mtk_panel_ctx *ctx);
int mtk_panel_detach(struct mtk_panel_ctx *ctx);
struct mtk_panel_ext *find_panel_ext(struct drm_panel *panel);
struct mtk_panel_ctx *find_panel_ctx(struct drm_panel *panel);
int mtk_panel_ext_create(struct device *dev,
			 struct mtk_panel_params *ext_params,
			 struct mtk_panel_funcs *ext_funcs,
			 struct drm_panel *panel);
int mtk_panel_tch_handle_reg(struct drm_panel *panel);

/* Ulefone/YFT addition exported by mediatek-drm.ko (not present in the
 * public MediaTek tree).  Used by the fingerprint local-HBM sysfs path.
 */
int ddic_dsi_send_cmd_for_fp(u8 *tx_buf, int size);

/* ------------------------------------------------------------------------ */
/* Stock-ABI assertions - every value below is recovered from the stock .ko  */
/* ------------------------------------------------------------------------ */

static_assert(sizeof(struct esd_check_item) == 22, "esd_check_item");
static_assert(sizeof(struct dfps_switch_cmd) == 72, "dfps_switch_cmd");
static_assert(sizeof(struct mtk_panel_dsc_params) == 208, "dsc_params size");
static_assert(offsetof(struct mtk_panel_dsc_params, ext_pps_cfg) == 136,
	      "dsc ext_pps_cfg");

static_assert(sizeof(struct mtk_panel_params) == 66328, "params size");
static_assert(offsetof(struct mtk_panel_params, pll_clk) == 0x0000, "off");
static_assert(offsetof(struct mtk_panel_params, data_rate) == 0x0004, "off");
static_assert(offsetof(struct mtk_panel_params, vdo_per_frame_lp_enable) == 0x000C, "off");
static_assert(offsetof(struct mtk_panel_params, change_fps_by_vfp_send_cmd) == 0x0010, "off");
static_assert(offsetof(struct mtk_panel_params, dfps_cmd_table0) == 0x0094, "off");
static_assert(offsetof(struct mtk_panel_params, dfps_cmd_table0.cmd_num) == 0x0098, "off");
static_assert(offsetof(struct mtk_panel_params, dfps_cmd_table0.para_list) == 0x009C, "off");
static_assert(offsetof(struct mtk_panel_params, cust_esd_check) == 0x0A48, "off");
static_assert(offsetof(struct mtk_panel_params, esd_check_enable) == 0x0A4C, "off");
static_assert(offsetof(struct mtk_panel_params, lcm_esd_check_table) == 0x0A50, "off");
static_assert(offsetof(struct mtk_panel_params, round_corner_en) == 0x0AAC, "off");
static_assert(offsetof(struct mtk_panel_params, dsc_params) == 0x0B30, "off");
static_assert(offsetof(struct mtk_panel_params, dsc_params.ext_pps_cfg) == 0x0BB8, "off");
static_assert(offsetof(struct mtk_panel_params, dsc_params.ext_pps_cfg.rc_buf_thresh) == 0x0BC0, "off");
static_assert(offsetof(struct mtk_panel_params, dsc_params.ext_pps_cfg.range_min_qp) == 0x0BD0, "off");
static_assert(offsetof(struct mtk_panel_params, dsc_params.ext_pps_cfg.range_max_qp) == 0x0BE0, "off");
static_assert(offsetof(struct mtk_panel_params, dsc_params.ext_pps_cfg.range_bpg_ofs) == 0x0BF0, "off");
static_assert(offsetof(struct mtk_panel_params, output_mode) == 0x0CD0, "off");
static_assert(offsetof(struct mtk_panel_params, lp_perline_en) == 0x0CF0, "off");
static_assert(offsetof(struct mtk_panel_params, lcm_degree) == 0x0CFC, "off");

static_assert(sizeof(struct mtk_panel_funcs) == 320, "funcs size");
static_assert(offsetof(struct mtk_panel_funcs, set_backlight_cmdq) == 0x010, "slot 2");
static_assert(offsetof(struct mtk_panel_funcs, reset) == 0x030, "slot 6");
static_assert(offsetof(struct mtk_panel_funcs, ata_check) == 0x038, "slot 7");
static_assert(offsetof(struct mtk_panel_funcs, ext_param_set) == 0x040, "slot 8");
static_assert(offsetof(struct mtk_panel_funcs, ext_param_get) == 0x048, "slot 9");
static_assert(offsetof(struct mtk_panel_funcs, get_res_switch_type) == 0x050, "slot 10");
static_assert(offsetof(struct mtk_panel_funcs, scaling_mode_mapping) == 0x058, "slot 11");
static_assert(offsetof(struct mtk_panel_funcs, mode_switch) == 0x060, "slot 12");
static_assert(offsetof(struct mtk_panel_funcs, hbm_set_cmdq) == 0x0D0, "slot 26");
static_assert(offsetof(struct mtk_panel_funcs, set_value) == 0x110, "slot 34");

static_assert(offsetof(struct mtk_panel_ext, params) == 8, "ext.params");

#endif /* __MTK_PANEL_EXT_STOCK_ABI_H__ */
