// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/kernel.h>
#include "cam_cal_list.h"
#include "eeprom_i2c_common_driver.h"
#include "eeprom_i2c_custom_driver.h"
#include "kd_imgsensor.h"

#define MAX_EEPROM_SIZE_32K 0x8000
#define MAX_EEPROM_SIZE_16K 0x4000

struct stCAM_CAL_LIST_STRUCT g_camCalList[] = {
	{0x0989, 0xA0, Common_read_region},
	{0x098A, 0xA0, Common_read_region},
	{0x38E1, 0xA0, Common_read_region},
	{0x38E2, 0xA0, Common_read_region},
	{0x566442, 0xA6, Common_read_region},
	{0xA042, 0xA0, Common_read_region},
	{0x0616, 0xA0, Common_read_region},
	{0x1B74, 0xA0, Common_read_region},
	{0x1B75, 0xA0, Common_read_region},
	{0x0350, 0xA0, Common_read_region},
	{0x08E1, 0xA0, Common_read_region},
	{0, 0, 0}
};

unsigned int cam_cal_get_sensor_list(
	struct stCAM_CAL_LIST_STRUCT **ppCamcalList)
{
	if (ppCamcalList == NULL)
		return 1;

	*ppCamcalList = &g_camCalList[0];
	return 0;
}


