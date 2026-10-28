/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ulefone/YFT shared device-information provider API (yft_devinfo.ko).
 *
 * Recovered by reverse engineering the stock GQ5012BF1 vendor_dlkm module
 *   yft_devinfo.ko
 *   sha256 0d5e547e3e6c313c88695b2c8f9aae04398e3c8822f16d57dff6aea011f821fe
 * and its seven stock consumers (focaltech_touch_spi_ft3680.ko, hynitron.ko,
 * hf_manager.ko, imgsensor.ko, sh366003_fg.ko, spi_tiny_co5300_lcd.ko,
 * aw36518.ko).
 *
 * ABI-PROVENANCE NOTE
 * -------------------
 * The stock second parameter of every yft_*_device_add() export is NOT a plain
 * `int`.  CRC-32 zero-shift analysis of the stock MODVERSION CRCs proves it is
 * a vendor enum whose genksyms expansion is exactly 73 characters long; the
 * identifier spelling is not present in any shipped artifact and could not be
 * recovered (409M semantically-plausible spellings and 56,495 real enum
 * definitions from every available kernel/vendor tree were tested).
 * Declaring it as `int` here is the honest, behaviourally identical choice and
 * costs exactly the ten *_device_add MODVERSION CRCs; see
 * kernel/phase4-yft-devinfo-reconstruction.md.
 */
#ifndef _YFT_DEVINFO_H_
#define _YFT_DEVINFO_H_

#include <linux/i2c.h>
#include <linux/seq_file.h>
#include <linux/spi/spi.h>

#include "hf_sensor_io.h"

/* Exported firmware-version strings (EXPORT_SYMBOL_GPL, char[30]). */
extern char touch_fw_version[30];
extern char second_touch_fw_version[30];
extern char fuelgauge_fw_version[30];
extern char tinylcd_fw_version[30];

/* Device registration.  `used` is 0 at registration time for every stock
 * consumer; the "currently used" device is selected later via
 * yft_set_*_device_used().
 */
int yft_camera_device_add(char *name, int used);
int yft_touchpanel_device_add(struct i2c_driver *driver, int used);
int yft_spitouchpanel_device_add(struct spi_driver *driver, int used);
int yft_accsensor_device_add(struct sensor_info *sensor, int used);
int yft_msensor_device_add(struct sensor_info *sensor, int used);
int yft_alspssensor_device_add(struct sensor_info *sensor, int used);
int yft_sarsensor_device_add(struct sensor_info *sensor, int used);
int yft_barosensor_device_add(struct sensor_info *sensor, int used);
int yft_fuelgauge_device_add(struct i2c_driver *driver, int used);
int yft_tinylcd_device_add(struct spi_driver *driver, int used);

/* Mark a previously registered device as the one actually in use. */
int yft_set_touch_device_used(char *name, int used);
int yft_set_camera_device_used(char *name, int used);
int yft_set_accsensor_device_used(char *name, int used);
int yft_set_msensor_device_used(char *name, int used);
int yft_set_alspssensor_device_used(char *name, int used);
int yft_set_sarsensor_device_used(char *name, int used);
int yft_set_barosensor_device_used(char *name, int used);
int yft_set_fuelgauge_device_used(char *name, int used);
int yft_set_tinylcd_device_used(char *name, int used);

/* Display-info list.  Both return void: proven by the stock KCFI type ids
 * _ZTSFvPciE == 0x6a3c623b and _ZTSFvP8seq_fileE == 0x9831c2fc, and confirmed
 * by the stock codegen (neither sets w0 before ret).
 */
void yft_add_display_info(char *info, int len);
void yft_display_info_dump(struct seq_file *m);

/* /proc dump helpers and the CTS-board strap. */
int yft_device_dump(struct seq_file *m);
int yft_memory_dump(struct seq_file *m);
int yft_cts_dump(struct seq_file *m);
int is_yft_cts_board(void);

#endif /* _YFT_DEVINFO_H_ */
