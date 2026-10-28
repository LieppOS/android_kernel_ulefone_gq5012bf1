# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_MTK_UART_APDMA) += mtk-uart-apdma.o
obj-$(CONFIG_MTK_HSDMA) += mtk-hsdma.o
obj-$(CONFIG_MTK_CQDMA) += mtk-cqdma.o

# ---- MediaTek/vendor objects merged from kernel_device_modules ----
ccflags-y += -I$(srctree)/drivers/dma/mediatek

# ---- LieppOS C13: vendor statements restored from the device-matching DM tree ----
obj-$(CONFIG_DEVICE_MODULES_MTK_UART_APDMA) += mtk-uart-apdma.o
obj-$(CONFIG_DEVICE_MODULES_MTK_CQDMA) += mtk-cqdma.o
