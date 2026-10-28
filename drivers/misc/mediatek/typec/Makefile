# SPDX-License-Identifier: GPL-2.0

subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/tcpc



# LieppOS batch04:30 targets and genuine source-only external API producers.
obj-y += mux/
obj-y += tcpc/

# ---- LieppOS C13: vendor statements restored from the device-matching DM tree ----
obj-$(CONFIG_TCPC_CLASS) += tcpc/
obj-$(CONFIG_MTK_USB_TYPEC_MUX) += mux/

