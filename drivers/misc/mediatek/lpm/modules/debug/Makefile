# common KOs
obj-$(CONFIG_MTK_LPM_DBG_V1) += v1/
obj-$(CONFIG_MTK_LOW_POWER_MODULE) += v2/

ifeq ($(CONFIG_MTK_LPM_MT6878),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6878),)
obj-$(CONFIG_MTK_LPM_MT6878) += mt6878/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6886),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6886),)
obj-$(CONFIG_MTK_LPM_MT6886) += mt6886/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6897),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6897),)
obj-$(CONFIG_MTK_LPM_MT6897) += mt6897/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6983),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6983),)
obj-$(CONFIG_MTK_LPM_MT6983) += mt6983/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6985),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6985),)
obj-$(CONFIG_MTK_LPM_MT6985) += mt6985/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6989),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6989),)
obj-$(CONFIG_MTK_LPM_MT6989) += mt6989/
endif
endif

# ---- LieppOS C13: the v1 and v2 LPM debug modules export the same lpm_*/
#      spm_* symbols; the stock image ships mtk-lpm-dbg-common-v2.ko, so the v1
#      API is kept behind its own disabled-by-default symbol. ----
