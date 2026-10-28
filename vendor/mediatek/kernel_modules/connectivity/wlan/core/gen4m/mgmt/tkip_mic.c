/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 LieppOS contributors
 */

/*! \file   tkip_mic.c
 *  \brief  Software TKIP Michael MIC verification for received MSDUs.
 *
 * LieppOS independent implementation (C50). It replaces a vendor file that
 * carries a no-redistribution notice; it is not derived from that file.
 *
 * The algorithm follows the Michael definition in IEEE Std 802.11-2020,
 * 12.5.2.3 (TKIP MIC): the MIC is computed over the pseudo header
 * DA || SA || Priority || 0 || 0 || 0 followed by the MSDU data, padded with
 * 0x5a and four to seven zero octets to a multiple of four octets, processed
 * as little-endian 32-bit words with the 64-bit Michael key (K0, K1).
 *
 * The interface is the one declared in include/mgmt/privacy.h and used by
 * nic/que_mgt.c for frames whose hardware decryption reported
 * CIPHER_SUITE_TKIP_WO_MIC (decrypted, MIC not verified). On success the
 * eight MIC octets are removed from the frame length so that only the MSDU
 * is indicated to the host; on failure the frame is left unchanged and
 * FALSE is returned so that the caller applies its MIC-error policy.
 */

#include "precomp.h"

#define TKIP_MIC_HDR_LEN	16	/* DA, SA, Priority, three zero octets */

/* 802.1H: Bridge-Tunnel encapsulation for AARP and IPX, RFC 1042 otherwise */
static const uint8_t aucTkipMicRfc1042[] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};
static const uint8_t aucTkipMicBridgeTunnel[] = {
	0xAA, 0xAA, 0x03, 0x00, 0x00, 0xF8};

struct TKIP_MIC_CTX {
	uint32_t u4L;
	uint32_t u4R;
	uint32_t u4Word;	/* partially collected little-endian word */
	uint8_t ucWordLen;	/* number of octets in u4Word */
};

static inline uint32_t tkipMicRol32(uint32_t u4Val, uint8_t ucShift)
{
	return (u4Val << ucShift) | (u4Val >> (32 - ucShift));
}

static inline uint32_t tkipMicRor32(uint32_t u4Val, uint8_t ucShift)
{
	return (u4Val >> ucShift) | (u4Val << (32 - ucShift));
}

static inline uint32_t tkipMicXswap(uint32_t u4Val)
{
	return ((u4Val & 0xff00ff00) >> 8) | ((u4Val & 0x00ff00ff) << 8);
}

/* Michael block function b(L, R) applied after L ^= M(i). */
static void tkipMicBlock(struct TKIP_MIC_CTX *prCtx, uint32_t u4Msg)
{
	uint32_t l = prCtx->u4L ^ u4Msg;
	uint32_t r = prCtx->u4R;

	r ^= tkipMicRol32(l, 17);
	l += r;
	r ^= tkipMicXswap(l);
	l += r;
	r ^= tkipMicRol32(l, 3);
	l += r;
	r ^= tkipMicRor32(l, 2);
	l += r;

	prCtx->u4L = l;
	prCtx->u4R = r;
}

static void tkipMicInit(struct TKIP_MIC_CTX *prCtx, const uint8_t *pucKey)
{
	prCtx->u4L = (uint32_t)pucKey[0] | ((uint32_t)pucKey[1] << 8) |
		     ((uint32_t)pucKey[2] << 16) | ((uint32_t)pucKey[3] << 24);
	prCtx->u4R = (uint32_t)pucKey[4] | ((uint32_t)pucKey[5] << 8) |
		     ((uint32_t)pucKey[6] << 16) | ((uint32_t)pucKey[7] << 24);
	prCtx->u4Word = 0;
	prCtx->ucWordLen = 0;
}

/* Octet-wise update: any length, any alignment, any number of segments. */
static void tkipMicUpdate(struct TKIP_MIC_CTX *prCtx,
			  const uint8_t *pucData, uint32_t u4Len)
{
	uint32_t i;

	for (i = 0; i < u4Len; i++) {
		prCtx->u4Word |= (uint32_t)pucData[i] << (8 * prCtx->ucWordLen);
		if (++prCtx->ucWordLen == 4) {
			tkipMicBlock(prCtx, prCtx->u4Word);
			prCtx->u4Word = 0;
			prCtx->ucWordLen = 0;
		}
	}
}

static void tkipMicFinal(struct TKIP_MIC_CTX *prCtx, uint8_t *pucMic)
{
	static const uint8_t aucPad[8] = {0x5a};
	uint32_t i;

	/* 0x5a followed by zeros up to the next word boundary, then one
	 * all-zero word: four to seven zero octets in total.
	 */
	tkipMicUpdate(prCtx, aucPad, 1);
	tkipMicUpdate(prCtx, &aucPad[1], (4 - prCtx->ucWordLen) % 4);
	tkipMicBlock(prCtx, 0);

	for (i = 0; i < 4; i++) {
		pucMic[i] = (uint8_t)(prCtx->u4L >> (8 * i));
		pucMic[4 + i] = (uint8_t)(prCtx->u4R >> (8 * i));
	}
}

static void tkipMicWipe(void *pvBuf, uint32_t u4Len)
{
	volatile uint8_t *pucBuf = pvBuf;

	while (u4Len--)
		*pucBuf++ = 0;
}

static void tkipMicHeader(struct TKIP_MIC_CTX *prCtx, const uint8_t *pucDA,
			  const uint8_t *pucSA, uint8_t ucPriority)
{
	uint8_t aucPseudo[4] = {ucPriority, 0, 0, 0};

	tkipMicUpdate(prCtx, pucDA, MAC_ADDR_LEN);
	tkipMicUpdate(prCtx, pucSA, MAC_ADDR_LEN);
	tkipMicUpdate(prCtx, aucPseudo, sizeof(aucPseudo));
}

/* Constant-time comparison of the computed and received MIC. */
static u_int8_t tkipMicEqual(const uint8_t *pucA, const uint8_t *pucB)
{
	uint8_t ucDiff = 0;
	uint32_t i;

	for (i = 0; i < TKIP_MIC_LEN; i++)
		ucDiff |= pucA[i] ^ pucB[i];

	return ucDiff == 0;
}

/* Remove the verified MIC from the frame that will be indicated. */
static void tkipMicStrip(struct SW_RFB *prSwRfb)
{
	prSwRfb->u2PacketLen -= TKIP_MIC_LEN;
	if (prSwRfb->u2PayloadLength >= TKIP_MIC_LEN)
		prSwRfb->u2PayloadLength -= TKIP_MIC_LEN;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Verify and remove the TKIP MIC of an MSDU in 802.11 format.
 *
 * \param[in] prSwRfb   RFB whose pvHeader points to the 802.11 MAC header
 *                      (u2HeaderLen octets) followed by the MSDU and the MIC.
 * \param[in] pucMicKey 8-octet Michael key for the receive direction.
 *
 * \retval TRUE  MIC verified; the MIC has been removed from u2PacketLen.
 * \retval FALSE MIC mismatch or malformed input; the frame is unchanged.
 */
/*----------------------------------------------------------------------------*/
u_int8_t tkipMicDecapsulate(struct SW_RFB *prSwRfb, uint8_t *pucMicKey)
{
	struct TKIP_MIC_CTX rCtx;
	uint8_t aucMic[TKIP_MIC_LEN];
	const uint8_t *pucHdr, *pucDA, *pucSA, *pucData;
	uint16_t u2FrameCtrl, u2MinHdrLen;
	uint32_t u4DataLen;
	uint8_t ucPriority = 0;
	u_int8_t fgOk;

	if (!prSwRfb || !pucMicKey || !prSwRfb->pvHeader)
		return FALSE;

	if (prSwRfb->u2PacketLen < WLAN_MAC_HEADER_LEN + TKIP_MIC_LEN)
		return FALSE;

	pucHdr = (const uint8_t *)prSwRfb->pvHeader;
	u2FrameCtrl = (uint16_t)pucHdr[0] | ((uint16_t)pucHdr[1] << 8);

	u2MinHdrLen = WLAN_MAC_HEADER_LEN;
	if (RXM_IS_FROM_DS_TO_DS(u2FrameCtrl))
		u2MinHdrLen = WLAN_MAC_HEADER_A4_LEN;
	if (RXM_IS_QOS_DATA_FRAME(u2FrameCtrl))
		u2MinHdrLen += 2;

	if (prSwRfb->u2HeaderLen < u2MinHdrLen ||
	    prSwRfb->u2PacketLen < prSwRfb->u2HeaderLen + TKIP_MIC_LEN)
		return FALSE;

	/* Addr1 at 4, Addr2 at 10, Addr3 at 16, Addr4 at 24 */
	switch (u2FrameCtrl & (MASK_FC_TO_DS | MASK_FC_FROM_DS)) {
	case 0:
		pucDA = pucHdr + 4;
		pucSA = pucHdr + 10;
		break;
	case MASK_FC_FROM_DS:
		pucDA = pucHdr + 4;
		pucSA = pucHdr + 16;
		break;
	case MASK_FC_TO_DS:
		pucDA = pucHdr + 16;
		pucSA = pucHdr + 10;
		break;
	default:
		pucDA = pucHdr + 16;
		pucSA = pucHdr + 24;
		break;
	}

	if (RXM_IS_QOS_DATA_FRAME(u2FrameCtrl))
		ucPriority = pucHdr[u2MinHdrLen - 2] & MASK_QC_TID;

	pucData = pucHdr + prSwRfb->u2HeaderLen;
	u4DataLen = prSwRfb->u2PacketLen - prSwRfb->u2HeaderLen -
		    TKIP_MIC_LEN;

	tkipMicInit(&rCtx, pucMicKey);
	tkipMicHeader(&rCtx, pucDA, pucSA, ucPriority);
	tkipMicUpdate(&rCtx, pucData, u4DataLen);
	tkipMicFinal(&rCtx, aucMic);

	fgOk = tkipMicEqual(aucMic, pucData + u4DataLen);
	tkipMicWipe(&rCtx, sizeof(rCtx));
	tkipMicWipe(aucMic, sizeof(aucMic));

	if (!fgOk)
		return FALSE;

	tkipMicStrip(prSwRfb);
	return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Verify and remove the TKIP MIC of an MSDU delivered in RX header
 *        translation mode.
 *
 * With header translation the hardware replaces the 802.11 MAC header and
 * the 802.1H LLC/SNAP header with an Ethernet header (DA, SA, EtherType).
 * The MSDU over which the MIC was computed is reconstructed as the removed
 * LLC/SNAP header (RFC 1042, or Bridge-Tunnel for AARP/IPX) followed by the
 * EtherType and payload. A length field below 0x0600 denotes an 802.3 frame
 * whose LLC header was not removed; its MSDU starts after the length field.
 * Frames without header translation are handled as 802.11 frames.
 *
 * \param[in] prAdapter Adapter (unused; part of the existing interface).
 * \param[in] prSwRfb   RFB of the complete (defragmented) MSDU plus MIC.
 * \param[in] pucMicKey 8-octet Michael key for the receive direction.
 *
 * \retval TRUE  MIC verified; the MIC has been removed from u2PacketLen.
 * \retval FALSE MIC mismatch or malformed input; the frame is unchanged.
 */
/*----------------------------------------------------------------------------*/
u_int8_t tkipMicDecapsulateInRxHdrTransMode(
	struct ADAPTER *prAdapter,
	struct SW_RFB *prSwRfb,
	uint8_t *pucMicKey)
{
	struct TKIP_MIC_CTX rCtx;
	uint8_t aucMic[TKIP_MIC_LEN];
	const uint8_t *pucFrame, *pucData;
	uint32_t u4DataLen;
	uint16_t u2EtherType, u2FrameCtrl;
	uint8_t ucPriority = 0;
	u_int8_t fgOk;

	if (!prSwRfb || !pucMicKey || !prSwRfb->pvHeader)
		return FALSE;

	if (!prSwRfb->fgHdrTran)
		return tkipMicDecapsulate(prSwRfb, pucMicKey);

	if (prSwRfb->u2PacketLen < ETHER_HEADER_LEN + TKIP_MIC_LEN)
		return FALSE;

	pucFrame = (const uint8_t *)prSwRfb->pvHeader;
	u2EtherType = ((uint16_t)pucFrame[ETHER_TYPE_LEN_OFFSET] << 8) |
		      pucFrame[ETHER_TYPE_LEN_OFFSET + 1];

	/* Priority: QoS TID, or 0 for non-QoS data (802.11-2020 12.5.2.3.3) */
	if ((prSwRfb->ucGroupVLD & BIT(RX_GROUP_VLD_4)) &&
	    prSwRfb->prRxStatusGroup4) {
		u2FrameCtrl = HAL_RX_STATUS_GET_FRAME_CTL_FIELD(
					prSwRfb->prRxStatusGroup4);
		if (RXM_IS_QOS_DATA_FRAME(u2FrameCtrl))
			ucPriority = prSwRfb->ucTid & MASK_QC_TID;
	} else if (prSwRfb->ucTid < TID_NUM) {
		ucPriority = prSwRfb->ucTid & MASK_QC_TID;
	}

	tkipMicInit(&rCtx, pucMicKey);
	tkipMicHeader(&rCtx, pucFrame, pucFrame + MAC_ADDR_LEN, ucPriority);

	if (u2EtherType >= ETHER_TYPE_MIN) {
		if (u2EtherType == ETH_P_AARP || u2EtherType == ETH_P_IPX)
			tkipMicUpdate(&rCtx, aucTkipMicBridgeTunnel,
				      sizeof(aucTkipMicBridgeTunnel));
		else
			tkipMicUpdate(&rCtx, aucTkipMicRfc1042,
				      sizeof(aucTkipMicRfc1042));
		pucData = pucFrame + ETHER_TYPE_LEN_OFFSET;
	} else {
		pucData = pucFrame + ETHER_HEADER_LEN;
	}
	u4DataLen = prSwRfb->u2PacketLen - TKIP_MIC_LEN -
		    (uint32_t)(pucData - pucFrame);

	tkipMicUpdate(&rCtx, pucData, u4DataLen);
	tkipMicFinal(&rCtx, aucMic);

	fgOk = tkipMicEqual(aucMic, pucData + u4DataLen);
	tkipMicWipe(&rCtx, sizeof(rCtx));
	tkipMicWipe(aucMic, sizeof(aucMic));

	if (!fgOk)
		return FALSE;

	tkipMicStrip(prSwRfb);
	return TRUE;
}
