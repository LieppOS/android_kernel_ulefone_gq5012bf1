// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <media/rc-core.h>

#include <mt-plat/mtk_pwm.h>
#include <mt-plat/mtk_pwm_hal.h>

#define DRIVER_NAME	"mtk-pwm-ir-tx"
#define DEVICE_NAME	"MTK PWM IR Transmitter"
#define IRTX_PWM_CLOCK		26000000U
#define IRTX_DEFAULT_CARRIER	38000U
/* Preserve the OEM waveform: one high bit followed by two low bits. */
#define IRTX_CYCLE		3U
#define IRTX_MAX_WORDS		U16_MAX
#define IRTX_MAX_DURATION	IR_MAX_DURATION
#define IRTX_MAX_COUNT		1024U

struct mtk_pwm_ir {
	struct regulator *regulator;
	unsigned int pwm_ch;
	unsigned int pwm_data_invert;
	unsigned int carrier;
	bool carrier_valid;
	bool duty_valid;
	struct platform_device *pdev;
};

static const struct pwm_spec_config irtx_pwm_config = {
	.pwm_no = 0,
	.mode = PWM_MODE_MEMORY,
	.clk_div = CLK_DIV1,
	.clk_src = PWM_CLK_NEW_MODE_BLOCK,
	.pmic_pad = 0,
	.PWM_MODE_MEMORY_REGS.IDLE_VALUE = IDLE_FALSE,
	.PWM_MODE_MEMORY_REGS.GUARD_VALUE = GUARD_FALSE,
	.PWM_MODE_MEMORY_REGS.STOP_BITPOS_VALUE = 31,
	.PWM_MODE_MEMORY_REGS.GDURATION = 0,
	.PWM_MODE_MEMORY_REGS.WAVE_NUM = 1,
};

/* HDURATION/LDURATION contain ticks minus one in 16-bit registers. */
static int mtk_pwm_ir_period(u32 carrier, u32 *ticks)
{
	if (carrier < DIV_ROUND_UP(IRTX_PWM_CLOCK, IRTX_CYCLE * (U16_MAX + 1U)) ||
	    carrier > IRTX_PWM_CLOCK / IRTX_CYCLE)
		return -EINVAL;

	*ticks = DIV_ROUND_CLOSEST_ULL(IRTX_PWM_CLOCK, (u64)carrier * IRTX_CYCLE);
	if (!*ticks || *ticks > U16_MAX + 1U)
		return -EINVAL;

	return 0;
}

static u32 mtk_pwm_ir_bits(u32 duration, u32 carrier)
{
	return DIV_ROUND_CLOSEST_ULL((u64)duration * carrier,
				     USEC_PER_SEC) * IRTX_CYCLE;
}

static int mtk_pwm_ir_tx(struct rc_dev *rcdev, unsigned int *txbuf,
			 unsigned int count)
{
	struct mtk_pwm_ir *pwm_ir = rcdev->priv;
	struct pwm_spec_config config = irtx_pwm_config;
	struct device *dev = &pwm_ir->pdev->dev;
	unsigned int original_count = count;
	u32 bits = 0, duration = 0, ticks, words, pos = 0;
	u32 i, j, total_time;
	u64 wait_us;
	u32 *wave_vir;
	dma_addr_t wave_phy;
	size_t buf_size;
	int ret, err;

	/* rc-core serializes writes and both setters with rcdev->lock. */
	if (!pwm_ir->carrier_valid || !pwm_ir->duty_valid)
		return -EINVAL;
	ret = mtk_pwm_ir_period(pwm_ir->carrier, &ticks);
	if (ret)
		return ret;
	if (!count || count > IRTX_MAX_COUNT || !(count & 1))
		return -EINVAL;

	/* Validate even the HAL padding before removing it. */
	for (i = 0; i < count; i++) {
		if (!txbuf[i] || txbuf[i] > IRTX_MAX_DURATION - duration)
			return -EINVAL;
		duration += txbuf[i];
	}
	/*
	 * The stock HAL appends 1 us to even-length Android patterns.
	 * Raw LIRC has no padding marker, so retain the OEM ambiguity.
	 */
	if (txbuf[count - 1] == 1)
		count--;
	if (!count)
		return original_count;

	for (i = 0; i < count; i++) {
		u32 n = mtk_pwm_ir_bits(txbuf[i], pwm_ir->carrier);

		if (!n || check_add_overflow(bits, n, &bits) ||
		    bits > IRTX_MAX_WORDS * 32U)
			return -EINVAL;
	}
	words = DIV_ROUND_UP(bits, 32U);
	/*
	 * Keep the OEM BUF0_SIZE encoding. Reserve an inactive guard word
	 * too, covering both count and count-minus-one hardware semantics.
	 */
	buf_size = (size_t)(words + 1) * sizeof(*wave_vir);
	/* Wait for the programmed ticks, not the requested carrier rate. */
	wait_us = DIV_ROUND_UP_ULL((u64)(words + 1) * 32 * ticks *
				  USEC_PER_SEC, IRTX_PWM_CLOCK);
	if (wait_us > U32_MAX - 100U)
		return -EINVAL;
	total_time = wait_us;
	wave_vir = dma_alloc_coherent(dev, buf_size, &wave_phy, GFP_KERNEL);
	if (!wave_vir)
		return -ENOMEM;

	memset(wave_vir, pwm_ir->pwm_data_invert ? 0xff : 0, buf_size);
	for (i = 0; i < count; i++) {
		u32 n = mtk_pwm_ir_bits(txbuf[i], pwm_ir->carrier);

		for (j = 0; j < n; j++, pos++) {
			if (!(i & 1) && !(j % IRTX_CYCLE))
				wave_vir[pos / 32] ^= BIT(pos % 32);
		}
	}

	config.pwm_no = pwm_ir->pwm_ch;
	config.PWM_MODE_MEMORY_REGS.HDURATION = ticks - 1;
	config.PWM_MODE_MEMORY_REGS.LDURATION = ticks - 1;
	config.PWM_MODE_MEMORY_REGS.BUF0_BASE_ADDR = wave_phy;
	config.PWM_MODE_MEMORY_REGS.BUF0_SIZE = words;

	/* Own one enable reference, even if another consumer has it on. */
	ret = regulator_enable(pwm_ir->regulator);
	if (ret)
		goto out_free;

	ret = pwm_set_spec_config(&config);
	if (!ret)
		usleep_range(total_time, total_time + 100);
	/* Also quiesce a partially configured PWM before freeing DMA. */
	mt_pwm_disable(config.pwm_no, config.pmic_pad);
	err = regulator_disable(pwm_ir->regulator);
	if (err) {
		dev_err(dev, "regulator_disable failed: %d\n", err);
		if (!ret)
			ret = err;
	}
	if (!ret)
		ret = original_count;

out_free:
	dma_free_coherent(dev, buf_size, wave_vir, wave_phy);
	return ret;
}

static int mtk_pwm_ir_set_duty_cycle(struct rc_dev *dev, u32 duty_cycle)
{
	struct mtk_pwm_ir *pwm_ir = dev->priv;

	/* Only the OEM 1/3 waveform is supported; 33 is the HAL request. */
	pwm_ir->duty_valid = duty_cycle == 33;
	return pwm_ir->duty_valid ? 0 : -EINVAL;
}

static int mtk_pwm_ir_set_carrier(struct rc_dev *dev, u32 carrier)
{
	struct mtk_pwm_ir *pwm_ir = dev->priv;

	u32 ticks;
	int ret;

	ret = mtk_pwm_ir_period(carrier, &ticks);
	/* The vendor HAL writes even after a failed ioctl: fail closed. */
	pwm_ir->carrier_valid = !ret;
	if (!ret)
		pwm_ir->carrier = carrier;
	return ret;
}

static int mtk_pwm_ir_probe(struct platform_device *pdev)
{
	struct mtk_pwm_ir *pwm_ir;
	struct rc_dev *rcdev;
	int rc;
	u32 dma_mask_bit = 32;
	const char *pwm_str = NULL;

	pwm_ir = devm_kzalloc(&pdev->dev, sizeof(*pwm_ir), GFP_KERNEL);
	if (!pwm_ir)
		return -ENOMEM;

	pwm_ir->carrier = IRTX_DEFAULT_CARRIER;
	pwm_ir->carrier_valid = true;
	pwm_ir->duty_valid = true;
	rc = of_property_read_u32(pdev->dev.of_node, "pwm-ch",
				  &pwm_ir->pwm_ch);
	if (rc)
		return rc;
	if (pwm_ir->pwm_ch >= PWM_MAX)
		return -EINVAL;
	of_property_read_u32(pdev->dev.of_node, "pwm-data-invert",
			     &pwm_ir->pwm_data_invert);
	if (pwm_ir->pwm_data_invert > 1)
		return -EINVAL;
	if (of_property_read_string(pdev->dev.of_node, "pwm-supply",
		&pwm_str)) {
		pr_info("Could not get pwm-supply property form dts");
		return -ENODEV;
	}

	of_property_read_u32(pdev->dev.of_node, "dma-mask-bit", &dma_mask_bit);
	if (!dma_mask_bit || dma_mask_bit > 64)
		return -EINVAL;
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(dma_mask_bit));
	if (rc)
		return rc;

	pwm_ir->regulator = devm_regulator_get(&pdev->dev, pwm_str);
	if (IS_ERR(pwm_ir->regulator))
		return PTR_ERR(pwm_ir->regulator);

	pwm_ir->pdev = pdev;

	rc = regulator_set_voltage(pwm_ir->regulator, 2800000, 2800000);
	if (rc < 0)
		return rc;

	rcdev = devm_rc_allocate_device(&pdev->dev, RC_DRIVER_IR_RAW_TX);
	if (!rcdev)
		return -ENOMEM;

	rcdev->priv = pwm_ir;
	rcdev->driver_name = DRIVER_NAME;
	rcdev->device_name = DEVICE_NAME;
	rcdev->tx_ir = mtk_pwm_ir_tx;
	rcdev->s_tx_duty_cycle = mtk_pwm_ir_set_duty_cycle;
	rcdev->s_tx_carrier = mtk_pwm_ir_set_carrier;

	rc = devm_rc_register_device(&pdev->dev, rcdev);
	if (rc < 0)
		dev_err(&pdev->dev, "failed to register rc device\n");

	return rc;
}

static const struct of_device_id mtk_pwm_ir_of_match[] = {
	{.compatible = "mediatek,irtx-pwm",},
	{}
};

static struct platform_driver pwm_ir_driver = {
	.probe = mtk_pwm_ir_probe,
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(mtk_pwm_ir_of_match),
	},
};

module_platform_driver(pwm_ir_driver);

MODULE_DESCRIPTION("MTK PWM IR Transmitter");
MODULE_AUTHOR("Chang-An Chen <chang-an.chen@mediatek.com>");
MODULE_LICENSE("GPL");
