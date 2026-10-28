// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from GQ5012BF1 stock camera_eeprom_v4l2.ko. */
#define PFX "CAM_CAL"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__
#include <linux/kernel.h>
#include "cam_cal_list.h"
#include "eeprom_i2c_common_driver.h"
#include "eeprom_i2c_custom_driver.h"
#include "cam_cal_config.h"

static unsigned int do_single_lsc_s5khp3sp23(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);
static unsigned int do_2a_gain_s5khp3sp23(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);
static unsigned int do_lens_id_s5khp3sp23(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);
static unsigned int do_pdaf_s5khp3sp23(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData);

static struct STRUCT_CALIBRATION_LAYOUT_STRUCT cal_layout_table = {
	0x0000000E, 0x010B00FF, CAM_CAL_SINGLE_EEPROM_DATA,
	{
		{0, 0, 0, do_module_version},
		{0, 1, 2, do_part_number},
		{0x00000000, 0x00000043, 0x0000074C, do_single_lsc_s5khp3sp23},
		{0x00000001, 0x00000031, 0x0000000E, do_2a_gain_s5khp3sp23},
		{0x00000001, 0x00000907, 0x000005DC, do_pdaf_s5khp3sp23},
		{0, 0, 0, do_stereo_data},
		{0, 0, 0x8000, do_dump_all},
		{0, 8, 2, do_lens_id_s5khp3sp23}
	}
};

struct STRUCT_CAM_CAL_CONFIG_STRUCT s5khp3sp23_cust_eeprom = {
	.name = "s5khp3sp23_cust_eeprom",
	.check_layout_function = layout_check,
	.read_function = Common_read_region,
	.layout = &cal_layout_table,
	.sensor_id = 0x1B75,
	.i2c_write_id = 0xA0,
	.max_size = 0x4000,
	.enable_preload = 1,
	.preload_size = 0x4000,
	.has_stored_data = 0,
};

static unsigned int do_single_lsc_s5khp3sp23(struct EEPROM_DRV_FD_DATA *pdata,
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

static unsigned int do_2a_gain_s5khp3sp23(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
		(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	int read_data_size;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	unsigned short AFInf = 0, AFMacro = 0;

	printk("do_2a_gain_s5khp3sp23 block_size=%d sensor_id=%x\n",
		block_size, pCamCalData->sensorID);
	memset(&pCamCalData->Single2A, 0,
		sizeof(struct STRUCT_CAM_CAL_SINGLE_2A_STRUCT));
	if (pCamCalData->DataVer >= CAM_CAL_TYPE_NUM) {
		show_cmd_error_log(pCamCalData->Command);
		return CAM_CAL_ERR_NO_DEVICE;
	}

	pCamCalData->Single2A.S2aVer = 1;
	pCamCalData->Single2A.S2aBitEn = 2;
	pCamCalData->Single2A.S2aAfBitflagEn = 0;
	printk("do_2a_gain_s5khp3sp23 S2aBitEn=0x%02x",
		pCamCalData->Single2A.S2aBitEn);

	read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
		0x76a, 1, (unsigned char *)&AFInf);
	printk("do_2a_gain_s5khp3sp23  AF valid=%d \n", AFInf);

	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0x22, 2, (unsigned char *)&AFInf);
	if (read_data_size > 0)
		err = CAM_CAL_ERR_NO_ERR;
	else {
		pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
		show_cmd_error_log(pCamCalData->Command);
	}
	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0x24, 2, (unsigned char *)&AFMacro);
	if (read_data_size > 0)
		err = CAM_CAL_ERR_NO_ERR;
	else {
		pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
		show_cmd_error_log(pCamCalData->Command);
	}
	AFInf = swab16(AFInf);
	AFMacro = swab16(AFMacro);
	pCamCalData->Single2A.S2aAf[0] = AFInf;
	pCamCalData->Single2A.S2aAf[1] = AFMacro;

	printk("======================AF CAM_CAL==================\n");
	printk("xqadd00 [AFInf] = %d\n", AFInf);
	printk("xqadd00 [AFMacro] = %d\n", AFMacro);
	printk("======================AF CAM_CAL==================\n");

	return err;
}

static unsigned int do_pdaf_s5khp3sp23(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
		(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	int read_data_size;
	int read_data_size_2;
	int err = CamCalReturnErr[pCamCalData->Command];

	pCamCalData->PDAF.Size_of_PDAF = block_size;
	printk("PDAF start_addr =%x table_size=%d\n", start_addr, block_size);
	read_data_size = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0x907, 0x1f0,
		(unsigned char *)&pCamCalData->PDAF.Data[0]);
	read_data_size_2 = read_data(pdata, pCamCalData->sensorID,
		pCamCalData->deviceID, 0xafb, 0x3ec,
		(unsigned char *)&pCamCalData->PDAF.Data[0x1f0]);
	if (read_data_size + read_data_size_2 > 0 || read_data_size > 0)
		err = CAM_CAL_ERR_NO_ERR;

	printk("======================PDAF Data==================\n");
	printk("First five %x, %x, %x, %x, %x\n",
		pCamCalData->PDAF.Data[0], pCamCalData->PDAF.Data[1],
		pCamCalData->PDAF.Data[2], pCamCalData->PDAF.Data[3],
		pCamCalData->PDAF.Data[4]);
	printk("RETURN = 0x%x\n", err);
	printk("======================PDAF Data==================\n");
	return err;
}

static unsigned int do_lens_id_s5khp3sp23(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size,
		unsigned int *pGetSensorCalData)
{
	return do_lens_id_base(pdata, start_addr, block_size, pGetSensorCalData);
}
