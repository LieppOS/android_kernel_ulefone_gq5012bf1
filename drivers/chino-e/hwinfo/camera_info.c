// SPDX-License-Identifier: GPL-2.0
/*
 * Camera identity storage exported by the ODM bootinfo driver.
 *
 * bootinfo.c (same vendor drop) defines these arrays and exports them for the
 * camera stack; the sensor drivers fill them at probe time (for example
 * sc202cs_mipi_raw_Sensor.c appends the module ID read from the sensor EEPROM
 * to depth_camera_sn).  bootinfo.c itself cannot be built in this source tree
 * because it also references ODM charger/typec/haptic globals whose providers
 * are not part of the authorized source set.  Keep the camera identity
 * storage in its own compilation unit so the camera stack can link without
 * the unrelated sysfs/debug nodes.
 */
#include <linux/module.h>

char main_module_id[64];
EXPORT_SYMBOL(main_module_id);

char depth_camera_sn[64];
EXPORT_SYMBOL(depth_camera_sn);
