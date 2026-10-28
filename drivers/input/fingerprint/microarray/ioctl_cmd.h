/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MicroArray Fprint Driver Code - ioctl_cmd.h (v4.0.06 command numbering;
 * public: HelloVolla UT-kernel-volla-mt6768 981578e37858).
 */
#ifndef __IOCTL_CMD_H__
#define __IOCTL_CMD_H__

/* MA_DRV_VERSION (0x4006) is a .data object in the stock module, see madev.c */

#define MA_IOC_MAGIC		'M'
#define TIMEOUT_WAKELOCK	_IO(MA_IOC_MAGIC, 1)
#define SLEEP			_IO(MA_IOC_MAGIC, 2)
#define WAKEUP			_IO(MA_IOC_MAGIC, 3)
#define ENABLE_CLK		_IO(MA_IOC_MAGIC, 4)
#define DISABLE_CLK		_IO(MA_IOC_MAGIC, 5)
#define ENABLE_INTERRUPT	_IO(MA_IOC_MAGIC, 6)
#define DISABLE_INTERRUPT	_IO(MA_IOC_MAGIC, 7)
#define TAP_DOWN		_IO(MA_IOC_MAGIC, 8)
#define TAP_UP			_IO(MA_IOC_MAGIC, 9)
#define SINGLE_TAP		_IO(MA_IOC_MAGIC, 11)
#define DOUBLE_TAP		_IO(MA_IOC_MAGIC, 12)
#define LONG_TAP		_IO(MA_IOC_MAGIC, 13)
#define SET_MODE		_IOW(MA_IOC_MAGIC, 33, unsigned int)
#define GET_MODE		_IOR(MA_IOC_MAGIC, 34, unsigned int)
#define MA_IOC_GVER		_IOR(MA_IOC_MAGIC, 35, unsigned int)
#define SCREEN_OFF		_IO(MA_IOC_MAGIC, 36)
#define SCREEN_ON		_IO(MA_IOC_MAGIC, 37)
#define SET_SPI_SPEED		_IOW(MA_IOC_MAGIC, 38, unsigned int)
#define WAIT_FACTORY_CMD	_IO(MA_IOC_MAGIC, 39)
#define WAKEUP_FINGERPRINTD	_IO(MA_IOC_MAGIC, 40)
#define WAIT_FINGERPRINTD_RESPONSE	_IOR(MA_IOC_MAGIC, 41, unsigned int)
#define WAKEUP_FACTORY_TEST_SEND_FINGERPRINTD_RESPONSE	_IOW(MA_IOC_MAGIC, 42, unsigned int)
#define WAIT_SCREEN_STATUS_CHANGE	_IOR(MA_IOC_MAGIC, 43, unsigned int)
#define GET_INTERRUPT_STATUS	_IOR(MA_IOC_MAGIC, 44, unsigned int)
#define SYNC			_IO(MA_IOC_MAGIC, 45)
#define SYNC2			_IO(MA_IOC_MAGIC, 46)
#define GET_SCREEN_STATUS	_IOR(MA_IOC_MAGIC, 47, unsigned int)

#endif /* __IOCTL_CMD_H__ */
