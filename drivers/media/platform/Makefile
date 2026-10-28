# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the video capture/playback device drivers.
#

# Place here, alphabetically sorted by directory
# (e. g. LC_ALL=C sort Makefile)
obj-y += allegro-dvt/
obj-y += amlogic/
obj-y += amphion/
obj-y += aspeed/
obj-y += atmel/
obj-y += cadence/
obj-y += chips-media/
obj-y += intel/
obj-y += marvell/
obj-y += mediatek/
obj-y += nvidia/
obj-y += nxp/
obj-y += qcom/
obj-y += renesas/
obj-y += rockchip/
obj-y += samsung/
obj-y += st/
obj-y += sunxi/
obj-y += ti/
obj-y += verisilicon/
obj-y += via/
obj-y += xilinx/

# Please place here only ancillary drivers that aren't SoC-specific
# Please keep it alphabetically sorted by Kconfig name
# (e. g. LC_ALL=C sort Makefile)
obj-$(CONFIG_VIDEO_MEM2MEM_DEINTERLACE)	+= m2m-deinterlace.o
obj-$(CONFIG_VIDEO_MUX)			+= video-mux.o

# ---- MediaTek/vendor objects merged from kernel_device_modules ----
obj-$(CONFIG_DEVICE_MODULES_VIDEO_MEDIATEK_VCODEC)	+= mtk-vcodec/
obj-$(CONFIG_VIDEO_MEDIATEK_VCU)	+= mtk-vcu/
obj-$(CONFIG_DEVICE_MODULES_VIDEO_MEDIATEK_JPEG)	+= mtk-jpeg/
obj-y					+= mtk-isp/
# C29: use the C27/C28 external request_firmware variant, not embedded models.
ifneq ($(CONFIG_LIEPPOS_EXTERNAL_IMGSYS),y)
obj-$(CONFIG_MTK_CAMERA_FD_ISP7S_ISP7SP)	+= mtk-aie/
endif
obj-$(CONFIG_MTK_CAMERA_DPE_ISP7SP)	 += mtk-dpe/
