/*
 * vpr_cs_ll_control.h — Bluetooth CS LL Control PDU definitions
 *
 * Opcodes and packed structs per Bluetooth Core Spec v5.4+
 * Vol 6, Part F — Link Layer Channel Sounding Control PDUs.
 *
 * These definitions are used by the VPR peer-exchange state machine and by
 * the Arduino-side raw LL-control transport. A sketch can now queue these
 * payloads over an active BLE connection with queueChannelSoundingLlControlPdu();
 * the full CS procedure state machine still lives above this raw transport.
 */
#ifndef VPR_CS_LL_CONTROL_H_
#define VPR_CS_LL_CONTROL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opcodes (BT Core Spec v5.4+ Vol 6 Part F §4.1) ────────────── */

#define VPR_CS_LL_CS_REQ        0x2CU  /* Capability / config exchange request  */
#define VPR_CS_LL_CS_RSP        0x2DU  /* Capability / config exchange response */
#define VPR_CS_LL_CS_CFG        0x2EU  /* Config set (from initiator)           */
#define VPR_CS_LL_CS_PROC_REQ   0x2FU  /* Procedure exchange request            */
#define VPR_CS_LL_CS_PROC_RSP   0x30U  /* Procedure exchange response           */
#define VPR_CS_LL_CS_SEC_REQ    0x31U  /* Security material exchange request    */
#define VPR_CS_LL_CS_SEC_RSP    0x32U  /* Security material exchange response   */
#define VPR_CS_LL_CS_START      0x33U  /* Start of CS procedure                 */
#define VPR_CS_LL_CS_TERMINATE  0x34U  /* Terminate CS procedure (normal)       */
#define VPR_CS_LL_CS_ABORT      0x35U  /* Abort CS procedure (error)            */

/* Raw over-air CS LL Control PDU sizes used by the current VPR peer-exchange
 * validator and two-board diagnostics. The payload length byte excludes the
 * opcode/length header. */
#define VPR_CS_LL_REQ_RSP_PDU_LEN       4U
#define VPR_CS_LL_REQ_RSP_PAYLOAD_LEN   2U
#define VPR_CS_LL_CFG_PDU_LEN           23U
#define VPR_CS_LL_CFG_PAYLOAD_LEN       21U
#define VPR_CS_LL_PROC_PDU_LEN          21U
#define VPR_CS_LL_PROC_PAYLOAD_LEN      19U
#define VPR_CS_LL_SEC_PDU_LEN           3U
#define VPR_CS_LL_SEC_PAYLOAD_LEN       1U
#define VPR_CS_LL_START_PDU_LEN         16U
#define VPR_CS_LL_START_PAYLOAD_LEN     14U
#define VPR_CS_LL_TERMINATE_ABORT_PDU_LEN     3U
#define VPR_CS_LL_TERMINATE_ABORT_PAYLOAD_LEN 1U

#define VPR_CS_LL_STATIC_ASSERT(name, cond) \
  typedef char vpr_cs_ll_static_assert_##name[(cond) ? 1 : -1]

/* ── Common header — 2 bytes ───────────────────────────────────── */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;
  uint8_t len; /* payload length (excluding this header) */
} vpr_cs_ll_pdu_header_t;

/* ── LL_CS_REQ / LL_CS_RSP ──────────────────────────────────────── */
/* Total: 4 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;   /* VPR_CS_LL_CS_REQ or VPR_CS_LL_CS_RSP */
  uint8_t len;      /* 2 */
  uint8_t aar;      /* Access Address Reuse flag (0 or 1)   */
  uint8_t config_id; /* 0 = use default / implicit config    */
} vpr_cs_ll_pdu_req_rsp_t;

VPR_CS_LL_STATIC_ASSERT(req_rsp_size,
                        sizeof(vpr_cs_ll_pdu_req_rsp_t) ==
                            VPR_CS_LL_REQ_RSP_PDU_LEN);

/* ── LL_CS_CFG ──────────────────────────────────────────────────── */
/* Total: 23 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;   /* VPR_CS_LL_CS_CFG */
  uint8_t len;      /* 21 */
  uint8_t config_id;
  uint8_t params[VPR_CS_LL_CFG_PAYLOAD_LEN - 1U];
} vpr_cs_ll_pdu_cfg_t;

VPR_CS_LL_STATIC_ASSERT(cfg_size,
                        sizeof(vpr_cs_ll_pdu_cfg_t) ==
                            VPR_CS_LL_CFG_PDU_LEN);

/* ── LL_CS_PROC_REQ / LL_CS_PROC_RSP ────────────────────────────── */
/* Total: 21 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;   /* VPR_CS_LL_CS_PROC_REQ or _RSP */
  uint8_t len;      /* 19 */
  uint8_t config_id;
  uint8_t params[VPR_CS_LL_PROC_PAYLOAD_LEN - 1U];
} vpr_cs_ll_pdu_proc_req_rsp_t;

VPR_CS_LL_STATIC_ASSERT(proc_size,
                        sizeof(vpr_cs_ll_pdu_proc_req_rsp_t) ==
                            VPR_CS_LL_PROC_PDU_LEN);

/* ── LL_CS_SEC_REQ / LL_CS_SEC_RSP ──────────────────────────────── */
/* Total: 3 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;    /* VPR_CS_LL_CS_SEC_REQ or _RSP */
  uint8_t len;       /* 1                            */
  uint8_t config_id;
} vpr_cs_ll_pdu_sec_req_rsp_t;

VPR_CS_LL_STATIC_ASSERT(sec_size,
                        sizeof(vpr_cs_ll_pdu_sec_req_rsp_t) ==
                            VPR_CS_LL_SEC_PDU_LEN);

/* ── LL_CS_START ────────────────────────────────────────────────── */
/* Total: 16 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;   /* VPR_CS_LL_CS_START */
  uint8_t len;      /* 14 */
  uint8_t config_id;
  uint8_t params[VPR_CS_LL_START_PAYLOAD_LEN - 1U];
} vpr_cs_ll_pdu_start_t;

VPR_CS_LL_STATIC_ASSERT(start_size,
                        sizeof(vpr_cs_ll_pdu_start_t) ==
                            VPR_CS_LL_START_PDU_LEN);

/* ── LL_CS_TERMINATE / LL_CS_ABORT ──────────────────────────────── */
/* Total: 3 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;  /* VPR_CS_LL_CS_TERMINATE or _ABORT */
  uint8_t len;     /* 1                                 */
  uint8_t reason;  /* 0x00 = normal, else abort reason   */
} vpr_cs_ll_pdu_terminate_abort_t;

VPR_CS_LL_STATIC_ASSERT(terminate_abort_size,
                        sizeof(vpr_cs_ll_pdu_terminate_abort_t) ==
                            VPR_CS_LL_TERMINATE_ABORT_PDU_LEN);

/* ── Helpers ────────────────────────────────────────────────────── */

/* Encode a CS LL Control PDU payload.
 * 'pdu' points to one of the packed structs above, beginning with
 * opcode and len. This is not an L2CAP frame and must not be CID-wrapped:
 * LL control PDUs are carried by the controller's LL control channel.
 * 'out_buf' must be at least pdu_len bytes. */
static inline bool vpr_cs_ll_encode_pdu(const void *pdu, size_t pdu_len,
                                         uint8_t *out_buf, size_t *out_len) {
  if (pdu == NULL || out_buf == NULL || out_len == NULL || pdu_len < 2U) {
    return false;
  }
  for (size_t i = 0U; i < pdu_len; ++i) {
    out_buf[i] = ((const uint8_t *)pdu)[i];
  }
  *out_len = pdu_len;
  return true;
}

/* Decode the opcode and length from a raw CS LL Control PDU buffer.
 * Returns false if the buffer is too short or the embedded payload length
 * does not fit inside the provided buffer. */
static inline bool vpr_cs_ll_decode_header(const uint8_t *buf, size_t buf_len,
                                            vpr_cs_ll_pdu_header_t *out_hdr) {
  if (buf == NULL || buf_len < 2U || out_hdr == NULL) {
    return false;
  }
  out_hdr->opcode = buf[0];
  out_hdr->len = buf[1];
  if ((size_t)out_hdr->len + 2U > buf_len) {
    return false;
  }
  return true;
}

#ifdef __cplusplus
}
#endif

#endif /* VPR_CS_LL_CONTROL_H_ */
