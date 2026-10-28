# LieppOS reconstruction of the Ulefone GQ5012BF1 stock aw883xx_driver.ko (v1.7.1).
#
# Object order reproduces the stock link order, which was recovered from the
# stock .text layout (first function of each translation unit):
#   0x000004 aw883xx_monitor.c        (aw883xx_monitor_start)
#   0x001b14 aw883xx_bin_parse.c      (aw883xx_dev_dsp_data_order)
#   0x003a94 aw883xx_device.c         (aw883xx_dev_get_list_head)
#   0x008694 aw883xx_init.c           (aw883xx_init_check_chipid)
#   0x0089a0 aw883xx_calib.c          (aw883xx_cali_check_result)
#   0x00e974 aw883xx_spin.c           (aw883xx_spin_check_mode)
#   0x00f7e0 aw883xx.c                (aw883xx_get_version)
#   0x015844 aw883xx_pid_2049_init.c  (aw883xx_pid_2049_dev_init)
#   0x017078 aw883xx_pid_2066_init.c  (aw883xx_pid_2066_dev_init)
#   0x017bac aw883xx_pid_2183_init.c  (aw883xx_pid_2183_dev_init)
obj-$(CONFIG_SND_SOC_AW883XX) += aw883xx_driver.o
aw883xx_driver-y := aw883xx_monitor.o \
		aw883xx_bin_parse.o \
		aw883xx_device.o \
		aw883xx_init.o \
		aw883xx_calib.o \
		aw883xx_spin.o \
		aw883xx.o \
		aw883xx_pid_2049_init.o \
		aw883xx_pid_2066_init.o \
		aw883xx_pid_2183_init.o

# NOTE (LieppOS C13): this directory is now part of the in-tree build via
# CONFIG_SND_SOC_AW883XX (see sound/soc/codecs/Makefile). For standalone builds
# use:  make -C $(KERNEL_SRC) M=$(PWD) modules   (no KBUILD_MODPOST_WARN).
