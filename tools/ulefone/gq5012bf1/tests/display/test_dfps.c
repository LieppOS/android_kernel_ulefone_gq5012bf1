// SPDX-License-Identifier: GPL-2.0-only
/* Recording host shims around extracted functions. NOT hardware/GCE execution. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <mipi_display.h>

#define BIT(n) (1U << (n))
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
#define DDPINFO(...) do { } while (0)
#define pr_info(...) do { } while (0)
typedef uint8_t u8;
typedef uint32_t u32;
#include "dsi-constants.inc"

/* Minimal recording models, not ABI-compatible replacements for Linux types. */
struct cmdq_pkt { int unused; };
struct mipi_dsi_msg { const void *tx_buf; size_t tx_len; u8 type; };
struct dfps_switch_cmd { unsigned int src_fps, cmd_num; unsigned char para_list[64]; };
struct mtk_panel_params {
	bool vdo_mix_mode_en;
	int lcm_cmd_if;
	struct { struct dfps_switch_cmd dfps_cmd_table[MAX_DYN_CMD_NUM]; } dyn_fps;
};
struct mtk_panel_ext { struct mtk_panel_params *params; };
struct drm_display_mode { unsigned int refresh; };
struct mtk_drm_crtc {
	struct drm_display_mode avail_modes[3];
	struct mtk_panel_ext *panel_ext;
	struct { unsigned int event[1]; } gce_obj;
};
enum { EVENT_DSI_CMD_DONE, MTK_DISP_WDMA = 9 };
struct mtk_ddp_comp { int id; void *cmdq_base; uintptr_t regs_pa; struct mtk_drm_crtc *mtk_crtc; };
struct mtk_dsi_driver_data {
	unsigned int reg_cmdq0_ofs, reg_vm_cmd_con_ofs;
	unsigned int reg_vm_cmd_data0_ofs, reg_vm_cmd_data10_ofs;
	unsigned int reg_vm_cmd_data20_ofs, reg_vm_cmd_data30_ofs;
	bool vm_rgb_time_interval, require_phy_reset;
};
struct mtk_dsi {
	struct mtk_ddp_comp ddp_comp;
	struct mtk_panel_ext *ext;
	struct mtk_dsi_driver_data *driver_data;
	struct mtk_dsi *slave_dsi;
	void *regs;
	unsigned long mode_flags;
};
struct mtk_ddic_cmd { unsigned int cmd_num; unsigned char *para_list; };
struct mtk_ddic_dsi_cmd {
	unsigned int is_hs, is_package, cmd_count;
	struct mtk_ddic_cmd mtk_ddic_cmd_table[MAX_TX_CMD_NUM_PACK];
};
struct operation { uintptr_t address; u32 value, mask; };
static struct operation operations[512];
static unsigned int nops, idle_polls, vm_polls, normal_payloads, power_depth;
static int mtk_ddp_comp_get_type(int id) { return id; }
static unsigned int drm_mode_vrefresh(struct drm_display_mode *m) { return m->refresh; }
static void record(uintptr_t address, u32 value, u32 mask)
{
	assert(nops < 512);
	operations[nops++] = (struct operation){ address, value, mask };
}
static void cmdq_pkt_write(struct cmdq_pkt *p, void *base, uintptr_t address, u32 value, u32 mask)
{
	record(address, value, mask);
}
static void mtk_ddp_write_mask(struct mtk_ddp_comp *c, u32 val, unsigned int offset, u32 mask, struct cmdq_pkt *p)
{
	record(c->regs_pa + offset, val, mask);
}
static void mtk_ddp_write_relaxed(struct mtk_ddp_comp *c, u32 val, unsigned int offset, struct cmdq_pkt *p)
{
	record(c->regs_pa + offset, val, ~0U);
}
static void writel(u32 value, void *address) { assert(!"Unexpected CPU MMIO path"); }
static void mtk_dsi_power_keep_gce(struct mtk_dsi *dsi, struct cmdq_pkt *p, bool keep)
{
	if (keep) power_depth++;
	else { assert(power_depth); power_depth--; }
}
static void mtk_dsi_poll_for_idle(struct mtk_dsi *dsi, struct cmdq_pkt *p) { idle_polls++; }
static void mtk_dsi_runtime_phy_reset_gce(struct mtk_dsi *dsi, struct cmdq_pkt *p) { }
static void mtk_dsi_cmdq_gce(struct mtk_dsi *dsi, struct cmdq_pkt *p, const struct mipi_dsi_msg *m)
{
	normal_payloads++;
}
static void mtk_dsi_cmdq_poll(struct mtk_ddp_comp *c, struct cmdq_pkt *p, unsigned int reg, unsigned int val, unsigned int mask)
{
	assert(reg == c->regs_pa + DSI_INTSTA);
	assert(val == VM_CMD_DONE_INT_EN && mask == VM_CMD_DONE_INT_EN);
	vm_polls++;
}
static void cmdq_pkt_clear_event(struct cmdq_pkt *p, unsigned int event) { }
static void cmdq_pkt_wait_no_clear(struct cmdq_pkt *p, unsigned int event) { }
#include "dsi-functions.inc"

static struct mtk_panel_params params;
static struct mtk_panel_ext ext = { &params };
/* Deliberately distinct fixture offsets: routing must use driver_data. */
static struct mtk_dsi_driver_data driver = {
	.reg_cmdq0_ofs = 0xd00, .reg_vm_cmd_con_ofs = 0x200,
	.reg_vm_cmd_data0_ofs = 0x208, .reg_vm_cmd_data10_ofs = 0x218,
	.reg_vm_cmd_data20_ofs = 0x228, .reg_vm_cmd_data30_ofs = 0x238,
};
static struct mtk_drm_crtc crtc = { .avail_modes = { {60}, {90}, {120} }, .panel_ext = &ext };
static struct mtk_dsi dsi = {
	.ddp_comp = { .regs_pa = 0x1401a000, .mtk_crtc = &crtc },
	.ext = &ext, .driver_data = &driver, .mode_flags = MIPI_DSI_MODE_VIDEO,
};
static struct cmdq_pkt packet;
static void reset(void)
{
	memset(&params, 0, sizeof(params));
	dsi.ext = &ext;
	ext.params = &params;
	dsi.mode_flags = MIPI_DSI_MODE_VIDEO;
	nops = idle_polls = vm_polls = normal_payloads = power_depth = 0;
}
static void command(unsigned int index, unsigned int src, unsigned char data)
{
	params.dyn_fps.dfps_cmd_table[index] = (struct dfps_switch_cmd){
		.src_fps = src, .cmd_num = 2, .para_list = {0x6c, data},
	};
}
static void check_video(unsigned int expected)
{
	unsigned int i, starts = 0, payloads = 0, clears = 0;
	assert(idle_polls == 0 && normal_payloads == 0 && power_depth == 0);
	assert(vm_polls == expected);
	for (i = 0; i < nops; i++) {
		struct operation o = operations[i];

		if (o.address == dsi.ddp_comp.regs_pa + DSI_START) {
			assert(o.mask == VM_CMD_START);
			if (o.value == VM_CMD_START) starts++;
		} else if (o.address == dsi.ddp_comp.regs_pa + driver.reg_vm_cmd_con_ofs) {
			assert(o.value & VM_CMD_EN);
			assert(o.value & TS_VFP_EN);
			assert(((o.value >> 8) & 0xff) == MIPI_DSI_DCS_SHORT_WRITE_PARAM);
			assert(((o.value >> 16) & 0xff) == 0x6c);
			payloads++;
		} else if (o.address == dsi.ddp_comp.regs_pa + DSI_INTSTA) {
			assert(o.value == 0 && o.mask == VM_CMD_DONE_INT_EN);
			clears++;
		} else {
			assert(!"VDO DFPS wrote normal command registers");
		}
	}
	assert(starts == expected && payloads == expected && clears == expected);
}
int main(void)
{
	unsigned int i;

	for (i = 0; i < 3; i++) {
		reset();
		command(0, 0, i);
		mtk_dsi_send_switch_cmd_nothing(&dsi, &packet, &crtc, 0, 60 + 30 * i);
		check_video(1);
		assert((operations[0].value >> 24) == i);
	}
	puts("PASS 60/90/120 payloads use VM command engine and VM completion only");
	reset();
	command(0, 90, 0); /* excluded: source mode is 60 */
	command(1, 60, 1);
	command(2, 0, 2);
	mtk_dsi_send_switch_cmd_nothing(&dsi, &packet, &crtc, 0, 120);
	check_video(2);
	assert((operations[0].value >> 24) == 1);
	puts("PASS source-FPS filters applied per command, in table order");
	reset();
	mtk_dsi_send_switch_cmd_nothing(&dsi, &packet, &crtc, 0, 120);
	assert(nops == 0 && idle_polls == 0);
	dsi.ext = NULL;
	mtk_dsi_send_switch_cmd_nothing(&dsi, &packet, &crtc, 0, 120);
	dsi.ext = &ext;
	ext.params = NULL;
	mtk_dsi_send_switch_cmd_nothing(&dsi, &packet, &crtc, 0, 120);
	assert(nops == 0 && power_depth == 0);
	puts("PASS empty table and absent panel parameters emit no commands");
	reset();
	dsi.mode_flags = 0;
	command(0, 0, 1);
	mtk_dsi_send_switch_cmd_nothing(&dsi, &packet, &crtc, 0, 90);
	assert(idle_polls == 2 && vm_polls == 0 && power_depth == 0);
	reset();
	dsi.mode_flags = 0;
	params.vdo_mix_mode_en = true;
	command(0, 0, 1);
	mtk_dsi_send_switch_cmd_nothing(&dsi, &packet, &crtc, 0, 90);
	assert(idle_polls == 0 && power_depth == 0);
	puts("PASS command-mode pre/post idle checks and mixed-mode completion retained");
	puts("TOTAL 4/4; host recording only, not hardware/GCE execution");
	return 0;
}
