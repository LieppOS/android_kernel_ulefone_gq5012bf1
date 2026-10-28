# SPDX-License-Identifier: GPL-2.0
#
# Generic framework for controlling remote processors
#

obj-$(CONFIG_REMOTEPROC)		+= remoteproc.o
remoteproc-y				:= remoteproc_core.o
remoteproc-y				+= remoteproc_coredump.o
remoteproc-y				+= remoteproc_debugfs.o
remoteproc-y				+= remoteproc_sysfs.o
remoteproc-y				+= remoteproc_virtio.o
remoteproc-y				+= remoteproc_elf_loader.o
obj-$(CONFIG_REMOTEPROC_CDEV)		+= remoteproc_cdev.o
obj-$(CONFIG_IMX_REMOTEPROC)		+= imx_rproc.o
obj-$(CONFIG_IMX_DSP_REMOTEPROC)	+= imx_dsp_rproc.o
obj-$(CONFIG_INGENIC_VPU_RPROC)		+= ingenic_rproc.o
obj-$(CONFIG_MTK_SCP)			+= mtk_scp.o mtk_scp_ipi.o
obj-$(CONFIG_OMAP_REMOTEPROC)		+= omap_remoteproc.o
obj-$(CONFIG_WKUP_M3_RPROC)		+= wkup_m3_rproc.o
obj-$(CONFIG_DA8XX_REMOTEPROC)		+= da8xx_remoteproc.o
obj-$(CONFIG_KEYSTONE_REMOTEPROC)	+= keystone_remoteproc.o
obj-$(CONFIG_MESON_MX_AO_ARC_REMOTEPROC)+= meson_mx_ao_arc.o
obj-$(CONFIG_PRU_REMOTEPROC)		+= pru_rproc.o
obj-$(CONFIG_QCOM_PIL_INFO)		+= qcom_pil_info.o
obj-$(CONFIG_QCOM_RPROC_COMMON)		+= qcom_common.o
obj-$(CONFIG_QCOM_Q6V5_COMMON)		+= qcom_q6v5.o
obj-$(CONFIG_QCOM_Q6V5_ADSP)		+= qcom_q6v5_adsp.o
obj-$(CONFIG_QCOM_Q6V5_MSS)		+= qcom_q6v5_mss.o
obj-$(CONFIG_QCOM_Q6V5_PAS)		+= qcom_q6v5_pas.o
obj-$(CONFIG_QCOM_Q6V5_WCSS)		+= qcom_q6v5_wcss.o
obj-$(CONFIG_QCOM_SYSMON)		+= qcom_sysmon.o
obj-$(CONFIG_QCOM_WCNSS_PIL)		+= qcom_wcnss_pil.o
qcom_wcnss_pil-y			+= qcom_wcnss.o
qcom_wcnss_pil-y			+= qcom_wcnss_iris.o
obj-$(CONFIG_RCAR_REMOTEPROC)		+= rcar_rproc.o
obj-$(CONFIG_ST_REMOTEPROC)		+= st_remoteproc.o
obj-$(CONFIG_ST_SLIM_REMOTEPROC)	+= st_slim_rproc.o
obj-$(CONFIG_STM32_RPROC)		+= stm32_rproc.o
obj-$(CONFIG_TI_K3_DSP_REMOTEPROC)	+= ti_k3_dsp_remoteproc.o
obj-$(CONFIG_TI_K3_R5_REMOTEPROC)	+= ti_k3_r5_remoteproc.o

# ---- MediaTek/vendor objects merged from kernel_device_modules ----
ccflags-y += -I$(srctree)/drivers/remoteproc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mtk-interconnect/ \

# ---- LieppOS C13/C17: vendor statements restored from the device-matching DM tree ----
# CONFIG_MTK_CCU_RPROC selects the stock external mtkcam/ccusys mtk_ccuv.ko
# provider.  The legacy in-tree mtk_ccu.ko is a competing provider and is
# gated separately so both cannot export mtk_ccu_* at the same time.
obj-$(CONFIG_MTK_CCU_RPROC_LEGACY)	+= mtk_ccu.o
obj-$(CONFIG_MTK_CCU_MSSV)		+= mtk_ccu_mssv.o
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mtk-interconnect/ \
			 -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include \
			 -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/iommu \
			 -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3 \
			 -I$(DEVICE_MODULES_PATH)/drivers/dma-buf/heaps

mtk_ccu-objs += mtk_ccu_isp71.o mtk_ccu_common.o mtk_ccu_ipi.o
mtk_ccu_mssv-objs += mtk_ccu_isp71_mssv.o mtk_ccu_common_mssv.o mtk_ccu_ipi_mssv.o

# ---- LieppOS C13: mtk_ccu_mssv.ko duplicates the mtk_ccu.ko exports; the stock
#      vendor image ships neither (it uses mtk_ccuv.ko / clk-mt6878-ccu.ko), so
#      the MSSV variant is kept behind its own disabled-by-default symbol. ----
