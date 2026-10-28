/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MediaTek sensor-2.0 ("hf_manager") userspace/kernel sensor descriptor.
 *
 * This is the MTK kernel-device-modules header
 *   drivers/misc/mediatek/sensor/2.0/core/hf_sensor_io.h
 * reduced to the single type that the yft_devinfo provider ABI needs.
 * hf_manager.ko hands a pointer to this structure to the
 * yft_<class>sensor_device_add() exports.
 *
 * The layout is independently proven from the stock binaries:
 *   - hf_manager.ko walks its support_list with a stride of 0x28 (40 bytes)
 *   - yft_devinfo.ko reads the device name at offset 0x08 as a char[16]
 *
 * NOTE: this must live in a header, not in yft_devinfo.c.  genksyms
 * (scripts/genksyms/parse.y, record_compound()) deliberately ignores
 * struct/union/enum definitions that appear in the main source file, so a
 * struct defined in the .c expands to "{ UNKNOWN }" and produces a different
 * MODVERSION CRC than the stock module.
 */
#ifndef _HF_SENSOR_IO_H_
#define _HF_SENSOR_IO_H_

#include <linux/types.h>

struct sensor_info {
	uint8_t sensor_type;
	uint8_t padding[3];
	uint32_t gain;
	char name[16];
	char vendor[16];
} __packed __aligned(4);

#endif /* _HF_SENSOR_IO_H_ */
