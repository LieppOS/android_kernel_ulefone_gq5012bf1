# SPDX-License-Identifier: GPL-2.0

# The link order is important here. ufshcd-core must initialize
# before vendor drivers.
obj-$(CONFIG_SCSI_UFSHCD)	+= core/ host/

# ---- MediaTek/vendor objects merged from kernel_device_modules ----
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/blocktag/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/vendor
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/
ccflags-y += -I$(srctree)/drivers/ufs/core/
ccflags-y += -I$(srctree)/drivers/devfreq/
ccflags-y += -I$(srctree)/kernel/
obj-$(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK) += vendor/

# ---- LieppOS integration: propagate vendor include paths to subdirectories ----
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/blocktag/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/vendor
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/
subdir-ccflags-y += -I$(srctree)/drivers/ufs/core/
subdir-ccflags-y += -I$(srctree)/drivers/devfreq/
subdir-ccflags-y += -I$(srctree)/kernel/

# ---- LieppOS integration: ACK 6.1 UFS core headers live in include/ufs ----
subdir-ccflags-y += -I$(srctree)/include/ufs

# ---- LieppOS C13 autofix: '../scsi_priv.h' resolved to drivers/scsi ----
ccflags-y += -I$(srctree)/drivers/scsi

# ---- LieppOS C13: vendor dbg module (from device-matching DM tree Makefile) ----
obj-$(CONFIG_SCSI_UFS_MEDIATEK_DBG) += ufs-mediatek-dbg.o
