/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef FOCALTECH_C56_SPI_H
#define FOCALTECH_C56_SPI_H

/* Independently authored C56-H7 transfer admission.
 * Stock 6629ec6148ac uses 4096 as a cache/allocation cutoff and sends
 * 0x7ff0-byte PRAM payloads through fts_write (REA evidence in H7 research).
 * Do not turn that cache cutoff into a controller-wide transfer limit.
 * Only framed PRAM writes gain a larger allowance; read/direct paths retain
 * their previous ceiling. This admits packets, not proof of hardware success.
 */
#define FTS_C56_SPI_CACHE_SIZE 4096U
#define FTS_C56_SPI_PRAM_PAYLOAD_MAX 32752U
#define FTS_C56_SPI_PRAM_OVERHEAD 7U
#define FTS_C56_SPI_PRAM_FRAME_MAX \
	(FTS_C56_SPI_PRAM_PAYLOAD_MAX + FTS_C56_SPI_PRAM_OVERHEAD)

static inline int fts_c56_spi_frame_allowed(const unsigned char *tx,
					  const unsigned char *rx,
					  unsigned int len, int allow_bulk)
{
	unsigned int payload;

	if (!tx || !rx || !len)
		return 0;
	if (len <= FTS_C56_SPI_CACHE_SIZE)
		return 1;
	/* Short-circuit bounds before inspecting the four-byte command header. */
	if (!allow_bulk || len > FTS_C56_SPI_PRAM_FRAME_MAX)
		return 0;
	if (tx[0] != 0xae || tx[1] != 0x00)
		return 0;
	payload = ((unsigned int)tx[2] << 8) | tx[3];
	return payload == len - FTS_C56_SPI_PRAM_OVERHEAD &&
		tx[4] == 0 && tx[5] == 0 && tx[6] == 0;
}

#endif
