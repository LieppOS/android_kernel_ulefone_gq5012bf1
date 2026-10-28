ifeq ($(CONFIG_MTK_GCOV_KERNEL),y)
GCOV_PROFILE := y
endif

ccflags-y += \
	-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sspm/ \
	-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/ \

mtk-dvfsrc-helper-objs += dvfsrc-helper.o dvfsrc-debug-v3.o dvfsrc-sysfs.o
mtk-dvfsrc-helper-$(CONFIG_MTK_DVFSRC_MET) += dvfsrc-met.o
mtk-dvfsrc-helper-$(CONFIG_MTK_DVFSRC_MET_MT6873) += dvfsrc-met-v6873.o
mtk-dvfsrc-helper-${CONFIG_MTK_DVFSRC_MET_MT6873} += dvfsrc-met-v6983.o
mtk-dvfsrc-helper-$(CONFIG_MTK_DVFSRC_MB)	+= dvfsrc-mb.o

# LieppOS batch04:30 targets and genuine source-only external API producers.

# ---- LieppOS C13: vendor statements restored from the device-matching DM tree ----
obj-$(CONFIG_MTK_DVFSRC_HELPER) += mtk-dvfsrc-helper.o
mtk-dvfsrc-helper-objs += dvfsrc-helper.o dvfsrc-debug-v3.o dvfsrc-sysfs.o
mtk-dvfsrc-helper-$(CONFIG_MTK_DVFSRC_MET) += dvfsrc-met.o
mtk-dvfsrc-helper-$(CONFIG_MTK_DVFSRC_MET_MT6873) += dvfsrc-met-v6873.o
mtk-dvfsrc-helper-${CONFIG_MTK_DVFSRC_MET_MT6873} += dvfsrc-met-v6983.o
mtk-dvfsrc-helper-$(CONFIG_MTK_DVFSRC_MB)	+= dvfsrc-mb.o
