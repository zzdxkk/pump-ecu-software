#ifndef UDS_DIAG_H
#define UDS_DIAG_H

#include <stddef.h>
#include <stdint.h>

#define UDS_MAX_PAYLOAD 64
#define UDS_VIN_LEN 17
#define UDS_PART_LEN 8

typedef struct {
    uint8_t buf[UDS_MAX_PAYLOAD];
    uint16_t total;
    uint16_t got;
    uint8_t next_sn;
    uint8_t active;
} isotp_rx_t;

typedef enum {
    ISOTP_OK = 0,
    ISOTP_INCOMPLETE = 1,
    ISOTP_ERR_ARG = -1,
    ISOTP_ERR_SEQ = -2,
    ISOTP_ERR_OVERFLOW = -3
} isotp_status_t;

void isotp_rx_init(isotp_rx_t *rx);
isotp_status_t isotp_rx_frame(isotp_rx_t *rx, const uint8_t *frame, uint8_t dlc, uint8_t *complete);
isotp_status_t isotp_tx_segment(const uint8_t *payload, uint16_t len, uint8_t frames[][8], uint8_t *frame_count);

typedef struct {
    uint8_t session;        /* 0x01 default, 0x02 programming, 0x03 extended */
    uint8_t unlocked;       /* security access level 1 granted */
    uint8_t seed_pending;  /* seed issued, key expected */
    uint16_t seed;
    uint8_t vin[UDS_VIN_LEN];
    uint8_t part_no[UDS_PART_LEN];
} uds_ctx_t;

void uds_init(uds_ctx_t *ctx);

/* Handles one complete request PDU (already reassembled by ISO-TP).
 * Positive response = SID + 0x40, negative response = 7F SID NRC. */
isotp_status_t uds_handle(uds_ctx_t *ctx,
                          const uint8_t *req,
                          uint16_t req_len,
                          uint8_t *resp,
                          uint16_t *resp_len);

#endif /* UDS_DIAG_H */