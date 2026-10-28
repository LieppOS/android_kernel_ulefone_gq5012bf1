// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>

extern int will_ldo_vout(int ldo_num, int value);
extern int will_ldo_en(int ldo_num, int enable);

int custom_ldo_vout(int ldo_num, int value)
{
	return will_ldo_vout(ldo_num, value);
}
EXPORT_SYMBOL(custom_ldo_vout);

int custom_ldo_en(int ldo_num, int enable)
{
	return will_ldo_en(ldo_num, enable);
}
EXPORT_SYMBOL(custom_ldo_en);

MODULE_DESCRIPTION("Custom Ldo Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
