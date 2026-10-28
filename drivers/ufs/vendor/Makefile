# SPDX-License-Identifier: GPL-2.0
# Mediatek UFS extended features makefile
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/vendor/

# must after -I$(DEVICE_MODULES_PATH)/drivers/ufs/
ccflags-y += -I$(srctree)/drivers/ufs/
ccflags-y += -I$(srctree)/drivers/ufs/core/
ccflags-y += -I$(srctree)/drivers/ufs/host/

# UFS KO without iSE
obj-$(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)  += ufs-mediatek-mod.o
ufs-mediatek-mod-y                              += ../ufs-mediatek.o ../ufs-mediatek-sysfs.o ../ufs-mediatek-mimic.o
ufs-mediatek-mod-$(if $(CONFIG_MTK_BLOCK_IO_TRACER),y,)  += ../ufs-mediatek-btag.o
ufs-mediatek-mod-$(if $(CONFIG_RPMB),y,)                 += ../ufs-mediatek-rpmb.o
ufs-mediatek-mod-$(if $(CONFIG_RPMB),y,)                 += ../ufs-mediatek-ise-dummy.o
ufs-mediatek-mod-$(CONFIG_UFSFEATURE) += ufsfeature.o ufsfeature-mimic.o
ufs-mediatek-mod-$(CONFIG_UFSHID) += ufshid.o

# UFS KO with iSE enabled
obj-$(CONFIG_UFS_MEDIATEK_ISE)           += ufs-mediatek-mod-ise.o
ufs-mediatek-mod-ise-y                              += ../ufs-mediatek.o ../ufs-mediatek-sysfs.o ../ufs-mediatek-mimic.o
ufs-mediatek-mod-ise-$(if $(CONFIG_MTK_BLOCK_IO_TRACER),y,)  += ../ufs-mediatek-btag.o
ufs-mediatek-mod-ise-$(if $(CONFIG_RPMB),y,)                 += ../ufs-mediatek-rpmb.o
ufs-mediatek-mod-ise-$(CONFIG_RPMB)                 += ../ufs-mediatek-ise.o
ufs-mediatek-mod-ise-$(if $(CONFIG_UFSFEATURE),y,) += ufsfeature.o ufsfeature-mimic.o
ufs-mediatek-mod-ise-$(if $(CONFIG_UFSHID),y,) += ufshid.o

# ---- LieppOS integration: ACK 6.1 UFS core headers live in include/ufs ----
ccflags-y += -I$(srctree)/include/ufs
ccflags-y += -I$(srctree)/drivers/scsi

# ---- LieppOS C13: aggregate inclusion when the feature is built as a module
#      (the stock ufs-mediatek-mod.ko contains btag + rpmb objects although
#       RPMB/MTK_BLOCK_IO_TRACER are =m in the device config) ----

# ---- LieppOS C13: the iSE module variant is gated by its own symbol because both
#      variants export identical ufs_mtk_* symbols (modpost duplicate-export error).
#      Stock ships the plain ufs-mediatek-mod.ko. ----
