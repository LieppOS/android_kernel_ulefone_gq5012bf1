// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define PFX "CAM_CAL"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__

#include <linux/kernel.h>
#include "cam_cal_list.h"
#include "eeprom_i2c_common_driver.h"
#include "eeprom_i2c_custom_driver.h"
#include "cam_cal_config.h"

static unsigned int do_single_lsc_ov64b(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData);
static unsigned int do_2a_gain_ov64b(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData);

static struct STRUCT_CALIBRATION_LAYOUT_STRUCT cal_layout_table = {
	0x00000000, 0x77006901, CAM_CAL_SINGLE_EEPROM_DATA,
	{
		{0x00000000, 0x00000000, 0x00000000, do_module_version},
		{0x00000000, 0x00000001, 0x00000002, do_part_number},
		{0x00000000, 0x0000001D, 0x0000074C, do_single_lsc_ov64b},
		{0x00000001, 0x00000007, 0x0000000E, do_2a_gain_ov64b},
		{0x00000000, 0x00000820, 0x000005F9, do_pdaf},
		{0x00000000, 0x00000FAE, 0x00000550, do_stereo_data},
		{0x00000000, 0x00000000, 0x00001600, do_dump_all},
		{0x00000000, 0x00000008, 0x00000002, do_lens_id}
	}
};

struct STRUCT_CAM_CAL_CONFIG_STRUCT ov64b_cust_eeprom = {
	.name = "ov64b_cust_eeprom",
	.check_layout_function = layout_check,
	.read_function = Common_read_region,
	.layout = &cal_layout_table,
	.sensor_id = OV64B_SENSOR_ID,
	.i2c_write_id = 0xA6,
	.max_size = 0x4000,
	.enable_preload = 1,
	.preload_size = 0x1500,
	.has_stored_data = 1,
};


static unsigned int do_single_lsc_ov64b(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
				(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;

	int read_data_size;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	unsigned short table_size;

	if (pCamCalData->DataVer >= CAM_CAL_TYPE_NUM) {
		err = CAM_CAL_ERR_NO_DEVICE;
		error_log("Read Failed\n");
		show_cmd_error_log(pCamCalData->Command);
		return err;
	}
	if (block_size != CAM_CAL_SINGLE_LSC_SIZE)
		error_log("block_size(%d) is not match (%d)\n",
				block_size, CAM_CAL_SINGLE_LSC_SIZE);

	pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType = 2;//mtk type
	pCamCalData->SingleLsc.LscTable.MtkLcsData.PixId = 8;

	table_size = 1868;

	debug_log("lsc table_size %d\n", table_size);
	pCamCalData->SingleLsc.LscTable.MtkLcsData.TableSize = table_size;
	if (table_size > 0) {
		pCamCalData->SingleLsc.TableRotation = 0;
		debug_log("u4Offset=%d u4Length=%d", start_addr, table_size);
		read_data_size = read_data(pdata,
			pCamCalData->sensorID, pCamCalData->deviceID,
			start_addr, table_size, (unsigned char *)
			&pCamCalData->SingleLsc.LscTable.MtkLcsData.SlimLscType);
		if (table_size == read_data_size)
			err = CAM_CAL_ERR_NO_ERR;
		else {
			error_log("Read Failed\n");
			err = CamCalReturnErr[pCamCalData->Command];
			show_cmd_error_log(pCamCalData->Command);
		}
	}

	debug_log("======================SingleLsc Data==================\n");
	debug_log("[1st] = %x, %x, %x, %x\n",
		pCamCalData->SingleLsc.LscTable.Data[0],
		pCamCalData->SingleLsc.LscTable.Data[1],
		pCamCalData->SingleLsc.LscTable.Data[2],
		pCamCalData->SingleLsc.LscTable.Data[3]);
	debug_log("[1st] = SensorLSC(1)?MTKLSC(2)?  %x\n",
		pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType);
	debug_log("CapIspReg =0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[0],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[1],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[2],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[3],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[4]);
	debug_log("RETURN = 0x%x\n", err);
	debug_log("======================SingleLsc Data==================\n");

	if (pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType != 2 &&
		pCamCalData->SingleLsc.LscTable.MtkLcsData.PixId != 8 &&
		pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType != 0x4c &&
		pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType != 7)
		yft_otp_state_main3 = 0;

	return err;
}

static unsigned int do_2a_gain_ov64b(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
		(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	int read_data_size;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	unsigned short AFMacro = 0, AFInf = 0;
	int otp_state;

	memset(&pCamCalData->Single2A, 0,
		sizeof(struct STRUCT_CAM_CAL_SINGLE_2A_STRUCT));
	if (pCamCalData->DataVer >= CAM_CAL_TYPE_NUM) {
		show_cmd_error_log(pCamCalData->Command);
		return CAM_CAL_ERR_NO_DEVICE;
	}
	pCamCalData->Single2A.S2aVer = 1;
	pCamCalData->Single2A.S2aBitEn = 2;
	pCamCalData->Single2A.S2aAfBitflagEn = 0;
	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0x76d, 2, (unsigned char *)&AFMacro);
	if (read_data_size > 0) {
		err = CAM_CAL_ERR_NO_ERR;
		otp_state = 1;
	} else {
		pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
		show_cmd_error_log(pCamCalData->Command);
		otp_state = 0;
	}
	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0x76b, 2, (unsigned char *)&AFInf);
	if (read_data_size > 0)
		err = CAM_CAL_ERR_NO_ERR;
	else {
		pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
		show_cmd_error_log(pCamCalData->Command);
		otp_state = 0;
	}
	pCamCalData->Single2A.S2aAf[0] = AFMacro;
	pCamCalData->Single2A.S2aAf[1] = AFInf;
	if (AFMacro > AFInf)
		otp_state = 0;
	yft_otp_state_main3 = otp_state;
	return err;
}
