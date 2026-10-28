# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for kernel SPMI framework.
#
obj-$(CONFIG_SPMI)	+= spmi.o

obj-$(CONFIG_SPMI_HISI3670)	+= hisi-spmi-controller.o
obj-$(CONFIG_SPMI_MSM_PMIC_ARB)	+= spmi-pmic-arb.o
obj-$(CONFIG_SPMI_MTK_PMIF)	+= spmi-mtk-pmif.o

# ---- MediaTek/vendor objects merged from kernel_device_modules ----
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/

# ---- LieppOS C13: vendor statements restored from the device-matching DM tree ----
obj-$(CONFIG_DEVICE_MODULES_SPMI_MTK_PMIF)	+= spmi-mtk-pmif.o spmi-mtk-mpu.o
spmi-mtk-pmif-objs := spmi-mtk-pmif-core.o spmi-mtk-debug.o
