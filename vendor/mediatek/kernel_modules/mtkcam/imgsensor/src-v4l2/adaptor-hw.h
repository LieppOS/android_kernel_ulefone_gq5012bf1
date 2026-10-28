/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 MediaTek Inc. */

#ifndef __ADAPTOR_HW_H__
#define __ADAPTOR_HW_H__

int adaptor_hw_power_on(struct adaptor_ctx *ctx);
int adaptor_hw_power_off(struct adaptor_ctx *ctx);
int adaptor_hw_init(struct adaptor_ctx *ctx);
int adaptor_hw_sensor_reset(struct adaptor_ctx *ctx);


/* GQ5012BF1: WL2864C/WL2868C camera LDO (custom_ldo.ko, CUSTOM-LDO track) */
int custom_ldo_vout(int ldo_num, int value);
int custom_ldo_en(int ldo_num, int enable);

#endif
