// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from GQ5012BF1 stock camera_eeprom_v4l2.ko. */
#define PFX "CAM_CAL"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__
#include <linux/kernel.h>
#include "cam_cal_list.h"
#include "eeprom_i2c_common_driver.h"
#include "eeprom_i2c_custom_driver.h"
#include "cam_cal_config.h"

static unsigned int do_single_lsc_imx616(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);
static unsigned int do_2a_gain_imx616(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);
static unsigned int do_lens_id_imx616(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);

static struct STRUCT_CALIBRATION_LAYOUT_STRUCT cal_layout_table = {
	0x00000001, 0x010B00FF, CAM_CAL_SINGLE_EEPROM_DATA,
	{
		{0, 0, 0, do_module_version},
		{0, 1, 2, do_part_number},
		{0x00000001, 0x00000016, 0x0000074C, do_single_lsc_imx616},
		{0x00000001, 0x00000010, 0x00000008, do_2a_gain_imx616},
		{0x00000000, 0x00000771, 0x000005DC, do_pdaf},
		{0, 0, 0, do_stereo_data},
		{0, 0, 0x8000, do_dump_all},
		{0, 8, 2, do_lens_id_imx616}
	}
};

struct STRUCT_CAM_CAL_CONFIG_STRUCT imx616_cust_eeprom = {
	.name = "imx616_cust_eeprom",
	.check_layout_function = layout_check,
	.read_function = Common_read_region,
	.layout = &cal_layout_table,
	.sensor_id = 0x616,
	.i2c_write_id = 0xA0,
	.max_size = 0x4000,
	.enable_preload = 1,
	.preload_size = 0x4000,
	.has_stored_data = 0,
};

static unsigned int do_single_lsc_imx616(struct EEPROM_DRV_FD_DATA *pdata,
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

	printk("lsc table_size %d\n", table_size);
	pCamCalData->SingleLsc.LscTable.MtkLcsData.TableSize = table_size;
	if (table_size > 0) {
		pCamCalData->SingleLsc.TableRotation = 0;
		printk("u4Offset=%d u4Length=%d", start_addr, table_size);
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

	printk("======================SingleLsc Data==================\n");
	printk("[1st] = %x, %x, %x, %x\n",
		pCamCalData->SingleLsc.LscTable.Data[0],
		pCamCalData->SingleLsc.LscTable.Data[1],
		pCamCalData->SingleLsc.LscTable.Data[2],
		pCamCalData->SingleLsc.LscTable.Data[3]);
	printk("[1st] = SensorLSC(1)?MTKLSC(2)?  %x\n",
		pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType);
	printk("CapIspReg =0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[0],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[1],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[2],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[3],
		pCamCalData->SingleLsc.LscTable.MtkLcsData.CapIspReg[4]);
	printk("RETURN = 0x%x\n", err);
	printk("======================SingleLsc Data==================\n");


	return err;
}

static unsigned int do_2a_gain_imx616(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
		(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	int read_data_size;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	unsigned long long CalGain = 0, FacGain = 0;
	unsigned short AFInf = 0;
	int tempMax = 0;
	int CalR = 1, CalGr = 1, CalGb = 1, CalG = 1, CalB = 1;
	int FacR = 1, FacGr = 1, FacGb = 1, FacG = 1, FacB = 1;

	printk("do_2a_gain_imx616 block_size=%d sensor_id=%x\n",
		block_size, pCamCalData->sensorID);
	memset(&pCamCalData->Single2A, 0,
		sizeof(struct STRUCT_CAM_CAL_SINGLE_2A_STRUCT));
	if (pCamCalData->DataVer >= CAM_CAL_TYPE_NUM) {
		show_cmd_error_log(pCamCalData->Command);
		return CAM_CAL_ERR_NO_DEVICE;
	}

	pCamCalData->Single2A.S2aVer = 1;
	pCamCalData->Single2A.S2aBitEn = 1;
	pCamCalData->Single2A.S2aAfBitflagEn = 0x0c;
	printk("do_2a_gain_imx616 S2aBitEn=0x%02x",
		pCamCalData->Single2A.S2aBitEn);

	pCamCalData->Single2A.S2aAwb.rGainSetNum = 0;
	read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		0x0a, 1, (unsigned char *)&AFInf);
	printk("do_2a_gain_imx616  AWB valid=%d \n", AFInf);
	printk("do_2a_gain_imx616 5100K AWB\n");

	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0x10, 4, (unsigned char *)&CalGain);
	if (read_data_size > 0) {
		printk("do_2a_gain_imx616 Read CalGain OK %x\n",
			read_data_size);
		CalR = CalGain & 0xff;
		CalGr = (CalGain >> 8) & 0xff;
		CalGb = (CalGain >> 16) & 0xff;
		CalB = (CalGain >> 24) & 0xff;
		CalG = (CalGr + CalGb + 1) >> 1;
		tempMax = max(max(CalR, CalG), CalB);
		printk("UnitR:%d, UnitG:%d, UnitB:%d, New Unit Max=%d",
			CalR, CalG, CalB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;
	} else {
		pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
		show_cmd_error_log(pCamCalData->Command);
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
		pCamCalData->deviceID, 0x14, 4, (unsigned char *)&FacGain);
	if (read_data_size > 0) {
		printk("Read FacGain OK\n");
		FacR = FacGain & 0xff;
		FacGr = (FacGain >> 8) & 0xff;
		FacGb = (FacGain >> 16) & 0xff;
		FacB = (FacGain >> 24) & 0xff;
		FacG = (FacGr + FacGb + 1) >> 1;
		tempMax = max(max(FacR, FacG), FacB);
		printk("GoldenR:%d, GoldenG:%d, GoldenB:%d, New Golden Max=%d",
			FacR, FacG, FacB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;
	} else {
		pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
		show_cmd_error_log(pCamCalData->Command);
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

	printk("======================AWB CAM_CAL==================\n");
	printk("AWB Calibration @5100K\n");
	printk("[CalGain] = 0x%llx\n", CalGain);
	printk("[FacGain] = 0x%llx\n", FacGain);
	printk("[rCalGain.u4R] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4R);
	printk("[rCalGain.u4G] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4G);
	printk("[rCalGain.u4B] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4B);
	printk("[rFacGain.u4R] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4R);
	printk("[rFacGain.u4G] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4G);
	printk("[rFacGain.u4B] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4B);

	return err;
}

static unsigned int do_lens_id_imx616(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData)
{
	return do_lens_id_base(pdata, start_addr, block_size, pGetSensorCalData);
}
