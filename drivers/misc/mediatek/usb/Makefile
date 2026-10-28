# SPDX-License-Identifier: GPL-2.0


# LieppOS batch04:30 targets and genuine source-only external API producers.
obj-y += usb_boost/
obj-y += usb_logger/
obj-y += usb_meta/
obj-y += usb_offload/
obj-y += usb_rndis/
obj-y += usb_xhci/

# ---- LieppOS C13: vendor statements restored from the device-matching DM tree ----
obj-$(CONFIG_MTK_USB_META)	+= usb_meta/
obj-$(CONFIG_MTK_ECCCI_C2K_USB)	+= c2k_usb/
obj-$(CONFIG_MTK_USB_F_RNDIS)	+= usb_rndis/
obj-$(CONFIG_MTK_USB_BOOST)	+= usb_boost/
obj-$(CONFIG_MTK_USB_OFFLOAD)	+= usb_offload/
obj-$(CONFIG_DEVICE_MODULES_USB_XHCI_MTK) += usb_xhci/
obj-$(CONFIG_MTK_USB_LOGGER)	+= usb_logger/
