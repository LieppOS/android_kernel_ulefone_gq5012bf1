// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2026 LieppOS contributors
 *
 * Host tests for gen4m mgmt/tkip_mic.c (built by run-tests.py).
 *
 * References that are independent of the code under test:
 *   - IEEE 802.11 Michael vectors from crypto/testmgr.h;
 *   - net/mac80211/michael.c michael_mic() with the address/TID helpers
 *     of include/linux/ieee80211.h, used to produce the MIC of every frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include TKIP_MIC_SOURCE	/* code under test; static helpers visible */
#include "michael.h"		/* mac80211 reference */
#include "michael_vectors.h"

static unsigned long g_pass, g_fail;
static uint64_t g_rng = 0x4c6965707053ULL;	/* fixed seed: deterministic */

#define CHECK(cond, ...) do {						\
	if (cond) {							\
		g_pass++;						\
	} else {							\
		g_fail++;						\
		if (g_fail <= 20) {					\
			printf("FAIL %s:%d: ", __func__, __LINE__);	\
			printf(__VA_ARGS__);				\
			printf("\n");					\
		}							\
	}								\
} while (0)

static uint32_t rnd(void)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return (uint32_t)(g_rng >> 16);
}

static void rnd_bytes(uint8_t *p, size_t n)
{
	while (n--)
		*p++ = (uint8_t)rnd();
}

/* Michael over DA/SA/priority + data with the code under test. */
static void dut_mic(const uint8_t *key, const uint8_t *da, const uint8_t *sa,
		    uint8_t prio, const uint8_t *data, size_t len, uint8_t *mic)
{
	struct TKIP_MIC_CTX ctx;

	tkipMicInit(&ctx, key);
	tkipMicHeader(&ctx, da, sa, prio);
	tkipMicUpdate(&ctx, data, (uint32_t)len);
	tkipMicFinal(&ctx, mic);
}

/*
 * Build an 802.11 data header (24/26/30/32 octets) for the given DS bits and
 * place DA/SA where the standard puts them. Returns the header length.
 */
static size_t build_hdr(uint8_t *h, int tods, int fromds, int qos, int tid,
			const uint8_t *da, const uint8_t *sa)
{
	uint16_t fc = IEEE80211_FTYPE_DATA | (qos ? IEEE80211_STYPE_QOS_DATA : 0) |
		      (tods ? IEEE80211_FCTL_TODS : 0) |
		      (fromds ? IEEE80211_FCTL_FROMDS : 0) | 0x4000 /* protected */;
	size_t len = (tods && fromds) ? 30 : 24;
	uint8_t a1[6], a2[6], a3[6], a4[6];

	rnd_bytes(h, 32);
	rnd_bytes(a1, 6); rnd_bytes(a2, 6); rnd_bytes(a3, 6); rnd_bytes(a4, 6);
	if (!tods && !fromds) { memcpy(a1, da, 6); memcpy(a2, sa, 6); }
	if (!tods && fromds)  { memcpy(a1, da, 6); memcpy(a3, sa, 6); }
	if (tods && !fromds)  { memcpy(a3, da, 6); memcpy(a2, sa, 6); }
	if (tods && fromds)   { memcpy(a3, da, 6); memcpy(a4, sa, 6); }
	h[0] = fc & 0xff;
	h[1] = fc >> 8;
	memcpy(h + 4, a1, 6);
	memcpy(h + 10, a2, 6);
	memcpy(h + 16, a3, 6);
	if (tods && fromds)
		memcpy(h + 24, a4, 6);
	if (qos) {
		h[len] = (uint8_t)((rnd() & 0xf0) | (tid & 0x0f));
		h[len + 1] = (uint8_t)rnd();
		len += 2;
	}
	return len;
}

static void ref_mic(const uint8_t *key, const uint8_t *hdr, const uint8_t *data,
		    size_t len, uint8_t *mic)
{
	uint8_t h[32];

	memcpy(h, hdr, sizeof(h));
	michael_mic(key, (struct ieee80211_hdr *)h, data, len, mic);
}

/* 1. IEEE vectors (crypto/testmgr.h) against the Michael core. */
static void test_vectors(void)
{
	size_t i;

	for (i = 0; i < sizeof(michael_vectors) / sizeof(michael_vectors[0]); i++) {
		struct TKIP_MIC_CTX ctx;
		uint8_t mic[8];

		tkipMicInit(&ctx, (const uint8_t *)michael_vectors[i].key);
		tkipMicUpdate(&ctx, (const uint8_t *)michael_vectors[i].pt,
			      (uint32_t)michael_vectors[i].psize);
		tkipMicFinal(&ctx, mic);
		CHECK(!memcmp(mic, michael_vectors[i].mic, 8), "vector %zu", i);
	}
}

/* 2. Pseudo header + data versus mac80211 for random inputs and DS modes;
 * 3. the same message fed in random segments from unaligned buffers.
 */
static void test_differential(int iterations)
{
	static uint8_t buf[2600 + 8];
	int it;

	for (it = 0; it < iterations; it++) {
		uint8_t key[8], da[6], sa[6], hdr[32], mref[8], mdut[8];
		int tods = rnd() & 1, fromds = rnd() & 1, qos = rnd() & 1;
		int tid = qos ? (int)(rnd() & 0xf) : 0;
		size_t len = (it < 64) ? (size_t)it : rnd() % 2400;
		size_t off = rnd() % 4, done = 0;
		uint8_t *data = buf + off;
		struct TKIP_MIC_CTX ctx;

		rnd_bytes(key, 8); rnd_bytes(da, 6); rnd_bytes(sa, 6);
		rnd_bytes(data, len);
		build_hdr(hdr, tods, fromds, qos, tid, da, sa);
		ref_mic(key, hdr, data, len, mref);

		dut_mic(key, da, sa, (uint8_t)tid, data, len, mdut);
		CHECK(!memcmp(mref, mdut, 8), "diff it=%d len=%zu ds=%d%d qos=%d",
		      it, len, tods, fromds, qos);

		tkipMicInit(&ctx, key);
		tkipMicHeader(&ctx, da, sa, (uint8_t)tid);
		while (done < len) {
			size_t n = 1 + rnd() % 13;

			if (n > len - done)
				n = len - done;
			tkipMicUpdate(&ctx, data + done, (uint32_t)n);
			done += n;
		}
		tkipMicFinal(&ctx, mdut);
		CHECK(!memcmp(mref, mdut, 8), "segmented it=%d len=%zu", it, len);
	}
}

static void rfb_80211(struct SW_RFB *r, uint8_t *frame, size_t hdrlen, size_t total)
{
	memset(r, 0, sizeof(*r));
	r->pvHeader = frame;
	r->u2HeaderLen = (uint16_t)hdrlen;
	r->u2PacketLen = (uint16_t)total;
	r->u2PayloadLength = (uint16_t)(total - hdrlen);
	r->ucTid = TID_NUM;
}

/* 4. tkipMicDecapsulate on 802.11-format frames (exact-size buffers). */
static void test_80211_frames(int iterations)
{
	int it;

	for (it = 0; it < iterations; it++) {
		uint8_t key[8], da[6], sa[6], hdr[32], mic[8];
		int tods = rnd() & 1, fromds = rnd() & 1, qos = rnd() & 1;
		int tid = qos ? (int)(rnd() & 0xf) : 0;
		size_t len = (it < 16) ? (size_t)it : rnd() % 1600;
		size_t hl, total, pos;
		uint8_t *f;
		struct SW_RFB r;
		u_int8_t ok;

		rnd_bytes(key, 8); rnd_bytes(da, 6); rnd_bytes(sa, 6);
		hl = build_hdr(hdr, tods, fromds, qos, tid, da, sa);
		total = hl + len + 8;
		f = malloc(total);
		memcpy(f, hdr, hl);
		rnd_bytes(f + hl, len);
		ref_mic(key, hdr, f + hl, len, mic);
		memcpy(f + hl + len, mic, 8);

		rfb_80211(&r, f, hl, total);
		ok = tkipMicDecapsulate(&r, key);
		CHECK(ok && r.u2PacketLen == total - 8 && r.u2PayloadLength == len,
		      "80211 accept it=%d", it);

		/* Any change to DA, SA, TID, data or MIC must be rejected. */
		pos = (size_t)-1;
		switch (rnd() % 5) {
		case 0: pos = (tods ? 16 : 4) + rnd() % 6; break;
		case 1: pos = (tods && fromds ? 24 : (fromds ? 16 : 10)) + rnd() % 6; break;
		case 2: if (qos) pos = hl - 2; break;
		case 3: if (len) pos = hl + rnd() % len; break;
		default: pos = hl + len + rnd() % 8; break;
		}
		if (pos != (size_t)-1) {
			uint8_t bit = (uint8_t)(1u << (pos == hl - 2 && qos ? rnd() % 4 : rnd() % 8));

			f[pos] ^= bit;
			rfb_80211(&r, f, hl, total);
			ok = tkipMicDecapsulate(&r, key);
			CHECK(!ok && r.u2PacketLen == total, "80211 reject it=%d pos=%zu", it, pos);
			f[pos] ^= bit;
		}

		/* Fields outside the Michael input must not matter: duration,
		 * sequence control, the transmitter address when it is not the
		 * SA, and the non-TID bits of QoS control.
		 */
		f[2] ^= 0xff;
		f[22] ^= 0xff;
		if (!(!tods && !fromds) && !(tods && !fromds))
			f[10] ^= 0x01;	/* Addr2 is not the SA in these modes */
		if (qos)
			f[hl - 2] ^= 0xf0;
		rfb_80211(&r, f, hl, total);
		CHECK(tkipMicDecapsulate(&r, key), "80211 non-MIC fields it=%d", it);

		/* Wrong key */
		key[rnd() % 8] ^= (uint8_t)(1u << (rnd() % 8));
		rfb_80211(&r, f, hl, total);
		CHECK(!tkipMicDecapsulate(&r, key) && r.u2PacketLen == total,
		      "80211 wrong key it=%d", it);
		free(f);
	}
}

struct trans_case {
	uint16_t ethertype;	/* >= 0x0600: SNAP; else 802.3 length frame */
	int bridge_tunnel;	/* SNAP OUI 00-00-F8 */
	int expect;
};

static const uint8_t snap_rfc1042[6] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};
static const uint8_t snap_btep[6] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0xF8};

/*
 * 5. Header-translation frames: the MIC is computed by mac80211 over the
 * 802.11 MSDU (LLC/SNAP + EtherType + payload); the translated frame given to
 * the code under test is DA, SA, EtherType, payload, MIC.
 */
static void test_hdrtrans_frames(int iterations)
{
	static const struct trans_case cases[] = {
		{0x0800, 0, 1}, {0x86DD, 0, 1}, {0x888E, 0, 1}, {0x0806, 0, 1},
		{0x8100, 0, 1},
		{0x8137, 1, 1}, {0x80F3, 1, 1},	/* 802.1H Bridge-Tunnel */
		{0x0000, 0, 1},			/* 802.3 length + LLC */
		{0x8137, 0, 0},			/* RFC1042 IPX: ambiguous, rejected */
	};
	int it;

	for (it = 0; it < iterations; it++) {
		const struct trans_case *tc = &cases[it % (sizeof(cases) / sizeof(cases[0]))];
		uint8_t key[8], da[6], sa[6], hdr[32], mic[8], *msdu, *f;
		int qos = rnd() & 1, tid = qos ? (int)(rnd() & 0xf) : 0;
		int use_g4 = rnd() & 1;
		size_t plen = (it < 32) ? (size_t)(it / 9) : rnd() % 1500;
		size_t mlen, flen;
		struct HW_MAC_RX_STS_GROUP_4 g4;
		struct SW_RFB r;
		u_int8_t ok;

		rnd_bytes(key, 8); rnd_bytes(da, 6); rnd_bytes(sa, 6);
		build_hdr(hdr, 0, 1, qos, tid, da, sa);

		msdu = malloc(8 + plen + 3);
		if (tc->ethertype >= 0x0600) {
			memcpy(msdu, tc->bridge_tunnel ? snap_btep : snap_rfc1042, 6);
			msdu[6] = tc->ethertype >> 8;
			msdu[7] = tc->ethertype & 0xff;
			rnd_bytes(msdu + 8, plen);
			mlen = 8 + plen;
		} else {
			msdu[0] = 0x42; msdu[1] = 0x42; msdu[2] = 0x03;	/* non-SNAP LLC */
			rnd_bytes(msdu + 3, plen);
			mlen = 3 + plen;
		}
		ref_mic(key, hdr, msdu, mlen, mic);

		if (tc->ethertype >= 0x0600) {
			flen = 12 + (mlen - 6) + 8;
			f = malloc(flen);
			memcpy(f + 12, msdu + 6, mlen - 6);
		} else {
			flen = 14 + mlen + 8;
			f = malloc(flen);
			f[12] = (uint8_t)(mlen >> 8);
			f[13] = (uint8_t)mlen;
			memcpy(f + 14, msdu, mlen);
		}
		memcpy(f, da, 6);
		memcpy(f + 6, sa, 6);
		memcpy(f + flen - 8, mic, 8);

		memset(&r, 0, sizeof(r));
		memset(&g4, 0, sizeof(g4));
		r.pvHeader = f;
		r.u2PacketLen = (uint16_t)flen;
		r.u2HeaderLen = ETHER_HEADER_LEN;
		r.u2PayloadLength = (uint16_t)(flen - ETHER_HEADER_LEN);
		r.fgHdrTran = TRUE;
		if (use_g4) {
			g4.u2FrameCtl = (uint16_t)(hdr[0] | hdr[1] << 8);
			r.ucGroupVLD = (uint8_t)BIT(RX_GROUP_VLD_4);
			r.prRxStatusGroup4 = &g4;
			r.ucTid = (uint8_t)(qos ? tid : rnd() % 8);	/* RXD TID */
		} else {
			r.ucTid = (uint8_t)(qos ? tid : TID_NUM);
		}

		ok = tkipMicDecapsulateInRxHdrTransMode(NULL, &r, key);
		if (tc->expect)
			CHECK(ok && r.u2PacketLen == flen - 8,
			      "trans accept it=%d type=%04x g4=%d qos=%d", it,
			      tc->ethertype, use_g4, qos);
		else
			CHECK(!ok && r.u2PacketLen == flen,
			      "trans ambiguous reject it=%d", it);

		if (tc->expect) {
			size_t pos = rnd() % flen;
			uint8_t bit = (uint8_t)(1u << (rnd() % 8));

			/* An 802.3 length field is synthesized by the receiver and
			 * is not part of the MSDU, so Michael does not cover it.
			 */
			if (tc->ethertype < 0x0600 && (pos == 12 || pos == 13)) {
				f[13] ^= 0x01;
				r.u2PacketLen = (uint16_t)flen;
				CHECK(tkipMicDecapsulateInRxHdrTransMode(NULL, &r, key),
				      "length field not covered it=%d", it);
				f[13] ^= 0x01;
				pos = 14 + rnd() % (flen - 14);
			}

			r.u2PacketLen = (uint16_t)flen;
			r.u2PayloadLength = (uint16_t)(flen - ETHER_HEADER_LEN);
			f[pos] ^= bit;
			ok = tkipMicDecapsulateInRxHdrTransMode(NULL, &r, key);
			CHECK(!ok && r.u2PacketLen == flen,
			      "trans reject it=%d pos=%zu", it, pos);
			f[pos] ^= bit;

			if (qos) {	/* wrong priority must be rejected */
				r.ucTid = (uint8_t)((tid + 1) & 0xf);
				ok = tkipMicDecapsulateInRxHdrTransMode(NULL, &r, key);
				CHECK(!ok, "trans wrong TID it=%d", it);
			}
		}
		free(msdu);
		free(f);
	}
}

/* 6. Malformed input: no crash (ASan/UBSan), always FALSE, no change. */
static void test_malformed(void)
{
	uint8_t key[8] = {0}, *f;
	struct SW_RFB r;
	size_t n;

	CHECK(!tkipMicDecapsulateInRxHdrTransMode(NULL, NULL, key), "NULL rfb");
	CHECK(!tkipMicDecapsulate(NULL, key), "NULL rfb 80211");
	memset(&r, 0, sizeof(r));
	CHECK(!tkipMicDecapsulateInRxHdrTransMode(NULL, &r, key), "NULL header");
	r.pvHeader = key;
	r.u2PacketLen = 8;
	CHECK(!tkipMicDecapsulateInRxHdrTransMode(NULL, &r, NULL), "NULL key");

	for (n = 0; n < ETHER_HEADER_LEN + TKIP_MIC_LEN; n++) {
		f = calloc(1, n ? n : 1);
		memset(&r, 0, sizeof(r));
		r.pvHeader = f;
		r.u2PacketLen = (uint16_t)n;
		r.fgHdrTran = TRUE;
		CHECK(!tkipMicDecapsulateInRxHdrTransMode(NULL, &r, key) &&
		      r.u2PacketLen == n, "short trans %zu", n);
		free(f);
	}
	for (n = 0; n < 32 + TKIP_MIC_LEN; n++) {
		size_t hl;

		for (hl = 0; hl <= 32; hl += 2) {
			f = calloc(1, n ? n : 1);	/* exact size */
			if (n >= 2) {
				f[0] = 0x88;	/* QoS data */
				f[1] = 0x03;	/* ToDS|FromDS: 32 octets */
			}
			rfb_80211(&r, f, hl, n);
			CHECK(!tkipMicDecapsulate(&r, key) && r.u2PacketLen == n,
			      "short 80211 len=%zu hl=%zu", n, hl);
			free(f);
		}
	}
}

/* 7. MIC comparison: every single-octet difference is detected. */
static void test_compare(void)
{
	uint8_t a[8], b[8];
	int i, v;

	rnd_bytes(a, 8);
	memcpy(b, a, 8);
	CHECK(tkipMicEqual(a, b), "equal");
	for (i = 0; i < 8; i++)
		for (v = 1; v < 256; v++) {
			b[i] = a[i] ^ (uint8_t)v;
			CHECK(!tkipMicEqual(a, b), "diff octet %d", i);
			b[i] = a[i];
		}
}

/* 8. Priority mapping for header-translation frames without QoS. */
static void test_priority(void)
{
	uint8_t key[8], da[6], sa[6], hdr[32], mic[8], *f, *msdu;
	struct HW_MAC_RX_STS_GROUP_4 g4;
	struct SW_RFB r;
	size_t plen = 40, flen = 12 + 2 + plen + 8;

	rnd_bytes(key, 8); rnd_bytes(da, 6); rnd_bytes(sa, 6);
	build_hdr(hdr, 0, 1, 0, 0, da, sa);	/* non-QoS: priority 0 */
	msdu = malloc(8 + plen);
	memcpy(msdu, snap_rfc1042, 6);
	msdu[6] = 0x08; msdu[7] = 0x00;
	rnd_bytes(msdu + 8, plen);
	ref_mic(key, hdr, msdu, 8 + plen, mic);
	f = malloc(flen);
	memcpy(f, da, 6); memcpy(f + 6, sa, 6);
	memcpy(f + 12, msdu + 6, 2 + plen);
	memcpy(f + flen - 8, mic, 8);

	memset(&r, 0, sizeof(r));
	memset(&g4, 0, sizeof(g4));
	r.pvHeader = f; r.u2PacketLen = (uint16_t)flen; r.fgHdrTran = TRUE;
	g4.u2FrameCtl = (uint16_t)(hdr[0] | hdr[1] << 8);
	r.ucGroupVLD = (uint8_t)BIT(RX_GROUP_VLD_4);
	r.prRxStatusGroup4 = &g4;
	r.ucTid = 5;	/* RXD TID ignored for non-QoS */
	CHECK(tkipMicDecapsulateInRxHdrTransMode(NULL, &r, key), "non-QoS g4 TID ignored");

	r.u2PacketLen = (uint16_t)flen;
	r.ucGroupVLD = 0;
	r.prRxStatusGroup4 = NULL;
	r.ucTid = TID_NUM;	/* set by nicRxDefragMPDU for non-QoS */
	CHECK(tkipMicDecapsulateInRxHdrTransMode(NULL, &r, key), "non-QoS TID_NUM -> 0");

	r.u2PacketLen = (uint16_t)flen;
	r.ucTid = 5;
	CHECK(!tkipMicDecapsulateInRxHdrTransMode(NULL, &r, key), "no g4, TID 5 used");
	free(msdu);
	free(f);
}

int main(int argc, char **argv)
{
	int iterations = argc > 1 ? atoi(argv[1]) : 20000;
	struct { const char *name; unsigned long before; } t;

#define RUN(fn, ...) do {						\
	t.name = #fn; t.before = g_fail;				\
	unsigned long p0 = g_pass;					\
	fn(__VA_ARGS__);						\
	printf("%-24s checks=%-8lu failures=%lu\n", t.name,		\
	       g_pass - p0 + g_fail - t.before, g_fail - t.before);	\
} while (0)

	RUN(test_vectors);
	RUN(test_differential, iterations);
	RUN(test_80211_frames, iterations);
	RUN(test_hdrtrans_frames, iterations);
	RUN(test_malformed);
	RUN(test_compare);
	RUN(test_priority);
	printf("TOTAL checks=%lu passed=%lu failed=%lu\n", g_pass + g_fail, g_pass, g_fail);
	return g_fail ? 1 : 0;
}
