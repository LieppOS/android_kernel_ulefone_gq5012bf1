# SPDX-License-Identifier: GPL-2.0-only
ffa-bus-y = bus.o
ffa-driver-y = driver.o
ffa-transport-$(CONFIG_ARM_FFA_SMCCC) += smccc.o
ffa-module-objs := $(ffa-bus-y) $(ffa-driver-y) $(ffa-transport-y)
# LieppOS C13: the MediaTek vendor tree carries its own Arm FF-A transport
# (drivers/misc/mediatek/trusted_mem/ffa_v10.ko) exporting the same ffa_*
# symbols. The stock GQ5012BF1 vendor image ships ffa_v10.ko, so the upstream
# module is only built when that vendor provider is deselected.
ifneq ($(filter y m,$(CONFIG_MTK_FFA_VENDOR_TRANSPORT)),)
obj-$(CONFIG_ARM_FFA_TRANSPORT) =
else
obj-$(CONFIG_ARM_FFA_TRANSPORT) = ffa-module.o
endif
