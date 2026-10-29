/* SPDX-License-Identifier: GPL-2.0-only */
/* HOST UNIT TEST ONLY. Real extracted transport/PRAM functions, mock SPI API.
 * No device, no actual firmware programming or ECC/hardware-success claim.
 */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "focaltech_c56_spi.h"

typedef uint8_t u8;
typedef uint32_t u32;
#define GFP_KERNEL 0
#define FTS_MAX_BUS_BUF 4096
#define FTS_ERROR(...) do { } while (0)
#define FTS_DEBUG(...) do { } while (0)
#define FTS_INFO(...) do { } while (0)
#define FTS_ALLOW_FW_PROGRAMMING 1
#define FTS_FLASH_PACKET_LENGTH_SPI (32U * 1024U - 16U)
#define FTS_PRAM_SADDR 0U
#define FTS_DRAM_SADDR 0xd00000U
#define FTS_ROMBOOT_CMD_SET_PRAM_ADDR 0xad
#define FTS_ROMBOOT_CMD_SET_PRAM_ADDR_LEN 4
#define FTS_ROMBOOT_CMD_WRITE 0xae
#define PRAMBOOT_MIN_SIZE 0x120U
#define BYTE_OFF_16(x) (((x) >> 16) & 0xff)
#define BYTE_OFF_8(x) (((x) >> 8) & 0xff)
#define BYTE_OFF_0(x) ((x) & 0xff)

struct mutex { int held; };
struct spi_device { int unused; };
struct spi_transfer { const void *tx_buf; void *rx_buf; unsigned int len; };
struct spi_message { struct spi_transfer *xfer; };
struct fts_ts_data {
	struct spi_device *spi;
	struct mutex bus_lock;
	u32 dummy_byte;
	u8 *bus_tx_buf, *bus_rx_buf;
};
struct upgrade_setting { u32 app2_offset; };
struct fts_upgrade {
	struct fts_ts_data *ts_data;
	struct upgrade_setting *setting_nf;
};
static struct fts_ts_data *fts_data;
static struct fts_upgrade *fwupgrade;
static int stopped, alloc_calls, alloc_fail_at, live_allocs;
static int spi_calls, spi_error;
static u8 spi_status;
static unsigned int frame_len, payload_total, address_frames, data_frames;
static u8 frame[FTS_C56_SPI_PRAM_FRAME_MAX];
static const u8 *expected_firmware;
static unsigned int expected_firmware_len;

static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = 0; }
static int fts_c56_stopping(void) { return stopped; }
static void udelay(unsigned int us) { (void)us; }
static void *test_alloc(size_t n)
{
	void *p;
	alloc_calls++;
	if (alloc_fail_at && alloc_calls == alloc_fail_at)
		return NULL;
	p = calloc(1, n);
	assert(p);
	live_allocs++;
	return p;
}
static void *kzalloc(size_t n, int flags) { (void)flags; return test_alloc(n); }
static void kfree(void *p) { if (p) { live_allocs--; free(p); } }
static void *vmalloc(size_t n) { return test_alloc(n); }
static void vfree(void *p) { kfree(p); }
static void spi_message_init(struct spi_message *m) { m->xfer = NULL; }
static void spi_message_add_tail(struct spi_transfer *x, struct spi_message *m) { m->xfer = x; }
static int spi_sync(struct spi_device *spi, struct spi_message *m)
{
	const u8 *tx = m->xfer->tx_buf;
	u8 *rx = m->xfer->rx_buf;
	unsigned int len = m->xfer->len, payload;
	assert(spi && m->xfer && fts_data->bus_lock.held);
	assert(len >= 4 && len <= sizeof(frame));
	spi_calls++;
	frame_len = len;
	memcpy(frame, tx, len);
	rx[3] = spi_status;
	payload = ((unsigned int)tx[2] << 8) | tx[3];
	if (expected_firmware && !spi_error && !spi_status) {
		if (tx[0] == 0xad) {
			unsigned int addr = ((unsigned int)tx[7] << 16) | ((unsigned int)tx[8] << 8) | tx[9];
			assert(payload == 3 && addr == payload_total);
			address_frames++;
		} else {
			assert(tx[0] == 0xae && len == payload + 7U);
			assert(payload_total + payload <= expected_firmware_len);
			assert(memcmp(tx + 7, expected_firmware + payload_total, payload) == 0);
			payload_total += payload;
			data_frames++;
		}
	}
	return spi_error;
}

/* Generated privately from the exact H4/H7 source. No copied driver payload
 * is committed alongside this independently authored unit-test shim. */
#include C56_SPI_UNDER_TEST

static void reset_test(void)
{
	assert(live_allocs == 0 && !fts_data->bus_lock.held);
	stopped = alloc_calls = alloc_fail_at = spi_calls = spi_error = 0;
	spi_status = 0;
	payload_total = address_frames = data_frames = 0;
	expected_firmware = NULL;
	memset(fts_data->bus_tx_buf, 0xa5, FTS_MAX_BUS_BUF);
	memset(fts_data->bus_rx_buf, 0xa5, FTS_MAX_BUS_BUF);
}

static void admission_tests(void)
{
	u8 *tx = calloc(1, FTS_C56_SPI_PRAM_FRAME_MAX + 1U);
	u8 rx = 0;
	unsigned int len;
	assert(tx);
	for (len = 1; len <= FTS_C56_SPI_PRAM_FRAME_MAX; len++) {
		unsigned int payload = len > 7 ? len - 7 : 0;
		tx[0] = 0xae; tx[1] = 0; tx[2] = payload >> 8; tx[3] = payload;
		assert(fts_c56_spi_frame_allowed(tx, &rx, len, 1));
		assert(fts_c56_spi_frame_allowed(tx, &rx, len, 0) == (len <= 4096));
	}
	assert(!fts_c56_spi_frame_allowed(NULL, &rx, 1, 1));
	assert(!fts_c56_spi_frame_allowed(tx, NULL, 1, 1));
	assert(!fts_c56_spi_frame_allowed(tx, &rx, 0, 1));
	assert(!fts_c56_spi_frame_allowed(tx, &rx, UINT_MAX, 1));
	assert(!fts_c56_spi_frame_allowed(tx, &rx, FTS_C56_SPI_PRAM_FRAME_MAX + 1U, 1));
	tx[0] = 0xad;
	assert(!fts_c56_spi_frame_allowed(tx, &rx, 32759, 1));
	tx[0] = 0xae; tx[1] = 0xa0;
	assert(!fts_c56_spi_frame_allowed(tx, &rx, 32759, 1));
	tx[1] = 0; tx[3] ^= 1;
	assert(!fts_c56_spi_frame_allowed(tx, &rx, 32759, 1));
	tx[3] ^= 1; tx[4] = 1;
	assert(!fts_c56_spi_frame_allowed(tx, &rx, 32759, 1));
	free(tx);
}

int main(void)
{
	struct spi_device spi = {0};
	struct fts_ts_data data = {.spi = &spi, .dummy_byte = 3};
	struct upgrade_setting setting = {.app2_offset = 1024U * 1024U};
	struct fts_upgrade upg = {.ts_data = &data, .setting_nf = &setting};
	u8 *input = calloc(1, 32754), *firmware = malloc(118972);
	unsigned int i, n;
	const unsigned int sizes[] = {0, 1, 128, 3511, 3512, 4086, 4087, 4088, 4089, 4090, 4091, 4092, 20716, 32752};
	assert(input && firmware);
	data.bus_tx_buf = calloc(1, FTS_MAX_BUS_BUF);
	data.bus_rx_buf = calloc(1, FTS_MAX_BUS_BUF);
	assert(data.bus_tx_buf && data.bus_rx_buf);
	fts_data = &data; fwupgrade = &upg;
	for (i = 0; i < 32754; i++) input[i] = (u8)(i * 17U + 3U);
	input[0] = 0xae;
	for (i = 0; i < 118972; i++) firmware[i] = (u8)(i * 29U + 7U);
	admission_tests();

#if C56_H4_CONTROL
	(void)n; (void)sizes;
	reset_test();
	assert(fts_write(input, 32753) == -EIO && spi_calls == 0 && live_allocs == 0);
	assert(fts_dpram_write(firmware, 118972, true) == -EIO && payload_total == 0 && live_allocs == 0);
	puts("H4 negative control: exact real transport/PRAM functions reject the first large packet as in H6");
#else
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		reset_test(); n = sizes[i];
		assert(fts_write(input, n + 1) == 0 && spi_calls == 1 && live_allocs == 0);
		assert(frame[0] == 0xae && frame[1] == 0);
		assert((((unsigned int)frame[2] << 8) | frame[3]) == n);
		assert(frame_len == (n ? n + 7U : 4U));
		if (n) assert(memcmp(frame + 7, input + 1, n) == 0);
	}
	reset_test();
	assert(fts_write(input, 2) == 0);
	for (i = frame_len; i < FTS_MAX_BUS_BUF; i++) assert(data.bus_tx_buf[i] == 0);
	for (i = 4; i < FTS_MAX_BUS_BUF; i++) assert(data.bus_rx_buf[i] == 0);
	reset_test(); assert(fts_write(NULL, 1) == -EINVAL && !spi_calls);
	assert(fts_write(input, 0) == -EINVAL && !spi_calls);
	assert(fts_write(input, UINT_MAX) == -EINVAL && !alloc_calls);
	assert(fts_write(input, 32754) == -EINVAL && !alloc_calls);
	reset_test(); stopped = 1;
	assert(fts_write(input, 32753) == -ESHUTDOWN && !spi_calls && !alloc_calls);
	for (i = 1; i <= 2; i++) {
		reset_test(); alloc_fail_at = i;
		assert(fts_write(input, 32753) == -ENOMEM && !spi_calls && !live_allocs);
	}
	reset_test(); spi_error = -ETIMEDOUT;
	assert(fts_write(input, 32753) == -EIO && spi_calls == 3 && !live_allocs);
	reset_test(); spi_status = 0xa0;
	assert(fts_write(input, 32753) == -EIO && spi_calls == 3 && !live_allocs);
	reset_test(); input[0] = 0x90;
	assert(fts_write(input, 32753) == -EIO && spi_calls == 0 && !live_allocs);
	input[0] = 0xae;
	reset_test(); expected_firmware = firmware; expected_firmware_len = 118972;
	assert(fts_dpram_write(firmware, 118972, true) == 0);
	assert(data_frames == 4 && address_frames == 4 && spi_calls == 8);
	assert(payload_total == 118972 && !live_allocs);
	puts("H7 HOST UNIT PASS: real PRAM/write functions serialize all 118972 synthetic bytes; mock spi_sync ONLY, not hardware/ECC verification");
#endif
	assert(!data.bus_lock.held && !live_allocs);
	free(data.bus_tx_buf); free(data.bus_rx_buf); free(input); free(firmware);
	return 0;
}
