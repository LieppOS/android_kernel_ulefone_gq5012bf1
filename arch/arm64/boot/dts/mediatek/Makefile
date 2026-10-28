# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_ARCH_MEDIATEK) += mt2712-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6755-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6779-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6795-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6797-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6797-x20-dev.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt7622-rfb1.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt7622-bananapi-bpi-r64.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt7986a-rfb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt7986b-rfb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8167-pumpkin.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-elm.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-elm-hana.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-elm-hana-rev7.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-burnet.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-cozmo.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-damu.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-fennel-sku1.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-fennel-sku6.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-fennel-sku7.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-fennel14.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-fennel14-sku2.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-juniper-sku16.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-kappa.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-kenzo.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-willow-sku0.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-willow-sku1.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kakadu.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kakadu-sku22.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kodama-sku16.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kodama-sku272.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kodama-sku288.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kodama-sku32.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-krane-sku0.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-krane-sku176.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-pumpkin.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8186-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8192-asurada-hayato-r1.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8192-asurada-spherion-r0.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8192-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8195-cherry-tomato-r1.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8195-cherry-tomato-r2.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8195-cherry-tomato-r3.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8195-demo.dtb

# LieppOS GQ5012BF1 trusted board overlay (independently authenticated
# properties only; numeric wiring that remains unknown stays disabled).
dtb-$(CONFIG_ARCH_MEDIATEK) += gq5012bf1-board.dtbo
# Provenance-pending Campaign 16 experiment.  Never part of the default
# trusted DTBO set; enable explicitly for offline comparison only.
dtb-$(CONFIG_GQ5012BF1_PENDING_BOARD_DT) += gq5012bf1-board-pending.dtbo
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8195-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8516-pumpkin.dtb

# ---- LieppOS C13: vendor statements restored from the device-matching DM tree ----
DTC_INCLUDE += $(DEVICE_MODULES_PATH)/include
# arm64
DTC_INCLUDE += $(DEVICE_MODULES_PATH)/arch/$(SRCARCH)/boot/dts $(DEVICE_MODULES_PATH)/arch/$(SRCARCH)/boot/dts/mediatek
# Vendor board DTS files include shared fragments as "mediatek/foo.dtsi".
DTC_INCLUDE += $(srctree)/arch/$(SRCARCH)/boot/dts

# for cust.dtsi
#DTC_INCLUDE += $(DEVICE_MODULES_REL_PATH)/arch/$(SRCARCH)/boot/dts
DTC_INCLUDE += $(dir $(src))

# for devicetree overlay
DTC_FLAGS += -@

dtb-$(CONFIG_ARCH_MEDIATEK) += mt2712-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6755-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6779-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6795-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6797-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6797-x20-dev.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt6873-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt7622-rfb1.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt7622-bananapi-bpi-r64.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8167-pumpkin.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-elm.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-elm-hana.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-elm-hana-rev7.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8173-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-burnet.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-damu.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-fennel-sku1.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-fennel-sku6.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-fennel14.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-juniper-sku16.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-kappa.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-kenzo.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-willow-sku0.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-jacuzzi-willow-sku1.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kakadu.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kodama-sku16.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kodama-sku272.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kodama-sku288.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-kodama-sku32.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-krane-sku0.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-kukui-krane-sku176.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8183-pumpkin.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8192-evb.dtb
dtb-$(CONFIG_ARCH_MEDIATEK) += mt8516-pumpkin.dtb
