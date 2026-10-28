// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from GQ5012BF1 stock camera_eeprom_v4l2.ko. */
#define PFX "CAM_CAL"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__
#include <linux/kernel.h>
#include "cam_cal_list.h"
#include "eeprom_i2c_common_driver.h"
#include "eeprom_i2c_custom_driver.h"
#include "cam_cal_config.h"

static unsigned int do_single_lsc_imx989ryyb(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);
static unsigned int do_2a_gain_imx989ryyb(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);
static unsigned int do_lens_id_imx989ryyb(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);

static struct STRUCT_CALIBRATION_LAYOUT_STRUCT cal_layout_table = {
	0x00000000, 0x77007001, CAM_CAL_SINGLE_EEPROM_DATA,
	{
		{0, 0, 0, do_module_version},
		{0, 1, 2, do_part_number},
		{0x00000001, 0x0000001D, 0x0000074C, do_single_lsc_imx989ryyb},
		{0x00000001, 0x00000007, 0x0000000E, do_2a_gain_imx989ryyb},
		{0x00000001, 0x00000771, 0x000005DC, do_pdaf},
		{0, 0, 0, do_stereo_data},
		{0, 0, 0x8000, do_dump_all},
		{0, 8, 2, do_lens_id_imx989ryyb}
	}
};

struct STRUCT_CAM_CAL_CONFIG_STRUCT imx989ryyb_cust_eeprom = {
	.name = "imx989ryyb_cust_eeprom",
	.check_layout_function = layout_check,
	.read_function = Common_read_region,
	.layout = &cal_layout_table,
	.sensor_id = 0x98A,
	.i2c_write_id = 0xA0,
	.max_size = 0x4000,
	.enable_preload = 1,
	.preload_size = 0x4000,
	.has_stored_data = 0,
};

static unsigned int do_single_lsc_imx989ryyb(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
		(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	int read_data_size;

	if (pCamCalData->DataVer >= CAM_CAL_TYPE_NUM) {
		show_cmd_error_log(pCamCalData->Command);
		return CAM_CAL_ERR_NO_DEVICE;
	}
	pCamCalData->SingleLsc.TableRotation = 0;
	pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType = 2;
	pCamCalData->SingleLsc.LscTable.MtkLcsData.PixId = 8;
	pCamCalData->SingleLsc.LscTable.MtkLcsData.TableSize = CAM_CAL_SINGLE_LSC_SIZE;
	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, start_addr, CAM_CAL_SINGLE_LSC_SIZE,
		(unsigned char *)&pCamCalData->SingleLsc.LscTable.MtkLcsData.SlimLscType);
	if (read_data_size == CAM_CAL_SINGLE_LSC_SIZE)
		err = CAM_CAL_ERR_NO_ERR;
	else {
		err = CamCalReturnErr[pCamCalData->Command];
		show_cmd_error_log(pCamCalData->Command);
	}
	if (pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType != 2 &&
		pCamCalData->SingleLsc.LscTable.MtkLcsData.PixId != 8 &&
		pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType != 0x4c &&
		pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType != 7)
		yft_otp_state_main = 0;
	return err;
}

static unsigned int do_2a_gain_imx989ryyb(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
		(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	int read_data_size;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	unsigned long long CalGain = 0, FacGain = 0;
	unsigned short AFMacro = 0, AFInf = 0;
	int tempMax = 0;
	int CalR = 1, CalGr = 1, CalGb = 1, CalG = 1, CalB = 1;
	int FacR = 1, FacGr = 1, FacGb = 1, FacG = 1, FacB = 1;
	int otp_state;

	memset(&pCamCalData->Single2A, 0,
		sizeof(struct STRUCT_CAM_CAL_SINGLE_2A_STRUCT));
	if (pCamCalData->DataVer >= CAM_CAL_TYPE_NUM) {
		show_cmd_error_log(pCamCalData->Command);
		return CAM_CAL_ERR_NO_DEVICE;
	}

	pCamCalData->Single2A.S2aVer = 1;
	pCamCalData->Single2A.S2aBitEn = 3;
	pCamCalData->Single2A.S2aAfBitflagEn = 0x0c;
	pCamCalData->Single2A.S2aAwb.rGainSetNum = 0;
	read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		0x0a, 1, (unsigned char *)&AFMacro);

	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0x0b, 8, (unsigned char *)&CalGain);
	if (read_data_size > 0) {
		CalR = CalGain & 0xffff;
		CalB = (CalGain >> 16) & 0xffff;
		CalGr = (CalGain >> 32) & 0xffff;
		CalGb = (CalGain >> 48) & 0xffff;
		CalG = (CalGr + CalGb + 1) >> 1;
		tempMax = max(max(CalR, CalG), CalB);
		err = CAM_CAL_ERR_NO_ERR;
		otp_state = 1;
	} else {
		pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
		show_cmd_error_log(pCamCalData->Command);
		otp_state = 0;
	}
	if (CalGain && CalR && CalG && CalB) {
		pCamCalData->Single2A.S2aAwb.rGainSetNum++;
		pCamCalData->Single2A.S2aAwb.rUnitGainu4R =
			(tempMax * 512 + (CalR >> 1)) / CalR;
		pCamCalData->Single2A.S2aAwb.rUnitGainu4G =
			(tempMax * 512 + (CalG >> 1)) / CalG;
		pCamCalData->Single2A.S2aAwb.rUnitGainu4B =
			(tempMax * 512 + (CalB >> 1)) / CalB;
	}

	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0x13, 8, (unsigned char *)&FacGain);
	if (read_data_size > 0) {
		FacR = FacGain & 0xffff;
		FacB = (FacGain >> 16) & 0xffff;
		FacGr = (FacGain >> 32) & 0xffff;
		FacGb = (FacGain >> 48) & 0xffff;
		FacG = (FacGr + FacGb + 1) >> 1;
		tempMax = max(max(FacR, FacG), FacB);
		err = CAM_CAL_ERR_NO_ERR;
	} else {
		pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
		show_cmd_error_log(pCamCalData->Command);
		otp_state = 0;
	}
	if (FacGain && FacR && FacG && FacB) {
		pCamCalData->Single2A.S2aAwb.rGoldGainu4R =
			(tempMax * 512 + (FacR >> 1)) / FacR;
		pCamCalData->Single2A.S2aAwb.rGoldGainu4G =
			(tempMax * 512 + (FacG >> 1)) / FacG;
		pCamCalData->Single2A.S2aAwb.rGoldGainu4B =
			(tempMax * 512 + (FacB >> 1)) / FacB;
	}

	pCamCalData->Single2A.S2aAwb.rValueR = CalR;
	pCamCalData->Single2A.S2aAwb.rValueGr = CalGr;
	pCamCalData->Single2A.S2aAwb.rValueGb = CalGb;
	pCamCalData->Single2A.S2aAwb.rValueB = CalB;
	pCamCalData->Single2A.S2aAwb.rGoldenR = FacR;
	pCamCalData->Single2A.S2aAwb.rGoldenGr = FacGr;
	pCamCalData->Single2A.S2aAwb.rGoldenGb = FacGb;
	pCamCalData->Single2A.S2aAwb.rGoldenB = FacB;

	read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		0x76a, 1, (unsigned char *)&AFMacro);
	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0x76d, 2, (unsigned char *)&AFMacro);
	if (read_data_size > 0)
		err = CAM_CAL_ERR_NO_ERR;
	else {
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
	yft_otp_state_main = otp_state;
	return err;
}

static unsigned int do_lens_id_imx989ryyb(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData)
{
	return do_lens_id_base(pdata, start_addr, block_size, pGetSensorCalData);
}
