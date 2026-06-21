/*
 * vpr_cs_ll_control.h — Bluetooth CS LL Control PDU definitions
 *
 * Opcodes and packed structs per Bluetooth Core Spec v5.4+
 * Vol 6, Part F — Link Layer Channel Sounding Control PDUs.
 *
 * These are NOT currently exchanged over-the-air (no second board,
 * no RADIO access from the VPR).  The header exists so the VPR
 * peer-exchange state machine can reference real PDU types rather
 * than magic numbers, and so serialisation helpers are ready when
 * the link-layer transport is implemented.
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

/* ── LL_CS_CFG (from initiator) ─────────────────────────────────── */
/* Total: 23 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;           /* VPR_CS_LL_CS_CFG         */
  uint8_t len;              /* 21                        */
  uint8_t config_id;
  uint8_t main_mode_type;
  uint8_t sub_mode_type;
  uint8_t min_main_mode_steps;
  uint8_t max_main_mode_steps;
  uint8_t main_mode_repetition;
  uint8_t mode0_steps;
  uint8_t role;             /* 0 = initiator, 1 = reflector */
  uint8_t rtt_type;
  uint8_t cs_sync_phy;
  uint8_t channel_map[10];
  uint8_t channel_map_repetition;
  uint8_t channel_selection_type;
  uint8_t ch3c_shape;
  uint8_t ch3c_jump;
} vpr_cs_ll_pdu_cfg_t;

/* ── LL_CS_PROC_REQ / LL_CS_PROC_RSP ────────────────────────────── */
/* Total: 21 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t  opcode;          /* VPR_CS_LL_CS_PROC_REQ or _RSP */
  uint8_t  len;             /* 19                            */
  uint8_t  config_id;
  uint16_t max_procedure_len;     /* in μs */
  uint16_t min_procedure_interval; /* in units of 1.25 ms */
  uint16_t max_procedure_interval; /* in units of 1.25 ms */
  uint16_t max_procedure_count;
  uint32_t min_subevent_len; /* in μs */
  uint32_t max_subevent_len; /* in μs */
  uint8_t  tone_antenna_config_selection;
  uint8_t  phy;              /* 1 = LE 1M, 2 = LE 2M */
  int8_t   tx_power_delta;  /* dBm delta (signed) */
} vpr_cs_ll_pdu_proc_req_rsp_t;

/* ── LL_CS_SEC_REQ / LL_CS_SEC_RSP ──────────────────────────────── */
/* Total: 3 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;    /* VPR_CS_LL_CS_SEC_REQ or _RSP */
  uint8_t len;       /* 1                            */
  uint8_t config_id;
} vpr_cs_ll_pdu_sec_req_rsp_t;

/* ── LL_CS_START ────────────────────────────────────────────────── */
/* Total: 16 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t  opcode;              /* VPR_CS_LL_CS_START */
  uint8_t  len;                 /* 14                  */
  uint8_t  config_id;
  uint8_t  subevent_len;        /* in μs units */
  uint8_t  subevents_per_event;
  uint16_t subevent_interval;   /* in units of 1.25 ms */
  uint16_t event_interval;      /* in units of 1.25 ms */
  uint16_t procedure_interval;  /* in units of 1.25 ms */
  uint16_t procedure_count;
  uint8_t  snr_control_initiator;
  uint8_t  snr_control_reflector;
} vpr_cs_ll_pdu_start_t;

/* ── LL_CS_TERMINATE / LL_CS_ABORT ──────────────────────────────── */
/* Total: 3 bytes */

typedef struct __attribute__((__packed__)) {
  uint8_t opcode;  /* VPR_CS_LL_CS_TERMINATE or _ABORT */
  uint8_t len;     /* 1                                 */
  uint8_t reason;  /* 0x00 = normal, else abort reason   */
} vpr_cs_ll_pdu_terminate_abort_t;

/* ── Helpers ────────────────────────────────────────────────────── */

/* Encode a CS LL PDU into the link-layer data-channel format.
 * 'pdu' points to one of the packed structs above.
 * 'out_buf' must be at least pdu_len + 2 bytes.
 * On success the CID (0x0025 = CS), opcode, len, and payload are
 * written to out_buf and *out_len is the total encoded length. */
static inline bool vpr_cs_ll_encode_pdu(const void *pdu, size_t pdu_len,
                                         uint8_t *out_buf, size_t *out_len) {
  if (pdu == NULL || out_buf == NULL || out_len == NULL || pdu_len < 2U) {
    return false;
  }
  out_buf[0] = 0x25U;  /* CID low byte (CS) */
  out_buf[1] = 0x00U;  /* CID high byte       */
  for (size_t i = 0U; i < pdu_len; ++i) {
    out_buf[2U + i] = ((const uint8_t *)pdu)[i];
  }
  *out_len = 2U + pdu_len;
  return true;
}

/* Decode the opcode and length from a raw CS LL PDU buffer.
 * Returns false if the buffer is too short or the CID doesn't match. */
static inline bool vpr_cs_ll_decode_header(const uint8_t *buf, size_t buf_len,
                                            vpr_cs_ll_pdu_header_t *out_hdr) {
  if (buf == NULL || buf_len < 4U || out_hdr == NULL) {
    return false;
  }
  /* Verify CS CID (0x0025) */
  if (buf[0] != 0x25U || buf[1] != 0x00U) {
    return false;
  }
  out_hdr->opcode = buf[2];
  out_hdr->len    = buf[3];
  return true;
}

#ifdef __cplusplus
}
#endif

#endif /* VPR_CS_LL_CONTROL_H_ */
