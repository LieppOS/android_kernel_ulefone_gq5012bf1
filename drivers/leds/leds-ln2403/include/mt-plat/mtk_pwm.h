/* Exact public ABI consumed from the MT6878 MediaTek PWM provider. */
#ifndef __MT_PWM_H__
#define __MT_PWM_H__
#include <linux/types.h>
#include <linux/dma-mapping.h>

struct pwm_spec_config {
	u32 pwm_no;
	u32 mode;
	u32 clk_div;
	u32 clk_src;
	u8 intr;
	u8 pmic_pad;
	union {
		struct {
			u16 IDLE_VALUE;
			u16 GUARD_VALUE;
			u16 GDURATION;
			u16 WAVE_NUM;
			u16 DATA_WIDTH;
			u16 THRESH;
		} PWM_MODE_OLD_REGS;
		struct {
			u32 IDLE_VALUE, GUARD_VALUE, STOP_BITPOS_VALUE;
			u16 HDURATION, LDURATION;
			u32 GDURATION, SEND_DATA0, SEND_DATA1, WAVE_NUM;
		} PWM_MODE_FIFO_REGS;
		struct {
			u32 IDLE_VALUE, GUARD_VALUE, STOP_BITPOS_VALUE;
			u16 HDURATION, LDURATION, GDURATION;
			dma_addr_t BUF0_BASE_ADDR;
			u16 BUF0_SIZE, WAVE_NUM;
		} PWM_MODE_MEMORY_REGS;
		struct {
			u16 IDLE_VALUE, GUARD_VALUE;
			u32 STOP_BITPOS_VALUE;
			u16 HDURATION, LDURATION, GDURATION;
			dma_addr_t BUF0_BASE_ADDR;
			u16 BUF0_SIZE;
			dma_addr_t BUF1_BASE_ADDR;
			u16 BUF1_SIZE, WAVE_NUM;
			u32 VALID;
		} PWM_MODE_RANDOM_REGS;
		struct {
			u16 PWM3_DELAY_DUR;
			u32 PWM3_DELAY_CLK;
			u16 PWM4_DELAY_DUR;
			u32 PWM4_DELAY_CLK;
			u16 PWM5_DELAY_DUR;
			u32 PWM5_DELAY_CLK;
		} PWM_MODE_DELAY_REGS;
	};
};

s32 pwm_set_spec_config(struct pwm_spec_config *conf);
void mt_pwm_disable(u32 pwm_no, u8 pmic_pad);
#endif
