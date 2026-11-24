#include "uds_diag.h"

#include <string.h>

#define PCI_SF 0x0u
#define PCI_FF 0x1u
#define PCI_CF 0x2u

#define NRC_SERVICE_NOT_SUPPORTED   0x11u
#define NRC_SUBFUNC_NOT_SUPPORTED   0x12u
#define NRC_LENGTH_INVALID          0x13u
#define NRC_CONDITIONS_NOT_CORRECT  0x22u
#define NRC_SEQUENCE_ERROR          0x24u
#define NRC_OUT_OF_RANGE            0x31u
#define NRC_SECURITY_DENIED         0x33u
#define NRC_INVALID_KEY             0x35u

void isotp_rx_init(isotp_rx_t *rx)
{
    if (rx == NULL) {
        return;
    }
    memset(rx, 0, sizeof(*rx));
}

isotp_status_t isotp_rx_frame(isotp_rx_t *rx, const uint8_t *frame, uint8_t dlc, uint8_t *complete)
{
    uint8_t type;
    uint16_t room;

    if (rx == NULL || frame == NULL || complete == NULL) {
        return ISOTP_ERR_ARG;
    }
    if (dlc == 0 || dlc > 8) {
        return ISOTP_ERR_ARG;
    }
    *complete = 0;
    type = (uint8_t)(frame[0] >> 4);

    if (type == PCI_SF) {
        uint8_t len = (uint8_t)(frame[0] & 0x0Fu);
        if (len == 0 || len > (uint8_t)(dlc - 1)) {
            return ISOTP_ERR_ARG;
        }
        memcpy(rx->buf, &frame[1], len);
        rx->total = len;
        rx->got = len;
        rx->active = 0;
        *complete = 1;
        return ISOTP_OK;
    }

    if (type == PCI_FF) {
        uint16_t total = (uint16_t)(((frame[0] & 0x0Fu) << 8) | frame[1]);
        if (total <= 7 || total > UDS_MAX_PAYLOAD) {
            rx->active = 0;
            return ISOTP_ERR_OVERFLOW;
        }
        rx->total = total;
        rx->got = (uint16_t)(dlc - 2);
        memcpy(rx->buf, &frame[2], rx->got);
        rx->next_sn = 1;
        rx->active = 1;
        return ISOTP_INCOMPLETE;
    }

    if (type == PCI_CF) {
        uint8_t sn = (uint8_t)(frame[0] & 0x0Fu);
        if (!rx->active) {
            return ISOTP_ERR_SEQ;
        }
        if (sn != rx->next_sn) {
            rx->active = 0; /* abort the transfer, tester must restart */
            return ISOTP_ERR_SEQ;
        }
        room = (uint16_t)(rx->total - rx->got);
        if (room > (uint16_t)(dlc - 1)) {
            room = (uint16_t)(dlc - 1);
        }
        memcpy(&rx->buf[rx->got], &frame[1], room);
        rx->got = (uint16_t)(rx->got + room);
        rx->next_sn = (uint8_t)((rx->next_sn + 1u) & 0x0Fu);
        if (rx->got >= rx->total) {
            rx->active = 0;
            *complete = 1;
        }
        return ISOTP_OK;
    }

    return ISOTP_ERR_ARG;
}

isotp_status_t isotp_tx_segment(const uint8_t *payload, uint16_t len, uint8_t frames[][8], uint8_t *frame_count)
{
    uint16_t offset;
    uint8_t sn = 1;
    uint8_t n = 0;

    if (payload == NULL || frames == NULL || frame_count == NULL) {
        return ISOTP_ERR_ARG;
    }
    if (len == 0 || len > UDS_MAX_PAYLOAD) {
        return ISOTP_ERR_ARG;
    }

    if (len <= 7) {
        memset(frames[0], 0, 8);
        frames[0][0] = (uint8_t)(PCI_SF << 4 | (len & 0x0Fu));
        memcpy(&frames[0][1], payload, len);
        *frame_count = 1;
        return ISOTP_OK;
    }

    memset(frames[0], 0, 8);
    frames[0][0] = (uint8_t)(PCI_FF << 4 | ((len >> 8) & 0x0Fu));
    frames[0][1] = (uint8_t)(len & 0xFFu);
    memcpy(&frames[0][2], payload, 6);
    n = 1;
    offset = 6;
    while (offset < len) {
        uint16_t chunk = (uint16_t)(len - offset);
        if (chunk > 7) {
            chunk = 7;
        }
        memset(frames[n], 0, 8);
        frames[n][0] = (uint8_t)(PCI_CF << 4 | (sn & 0x0Fu));
        memcpy(&frames[n][1], &payload[offset], chunk);
        offset = (uint16_t)(offset + chunk);
        sn = (uint8_t)((sn + 1u) & 0x0Fu);
        ++n;
    }
    *frame_count = n;
    return ISOTP_OK;
}

void uds_init(uds_ctx_t *ctx)
{
    static const uint8_t k_vin[UDS_VIN_LEN] = {'L', 'Z', 'V', 'P', 'U', 'M', 'P', '0',
                                               '0', '0', '0', '0', '0', '0', '0', '1', '7'};
    static const uint8_t k_part[UDS_PART_LEN] = {'P', 'U', 'M', 'P', '-', '0', '1', 'A'};

    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->session = 0x01u;
    ctx->unlocked = 0;
    ctx->seed_pending = 0;
    memcpy(ctx->vin, k_vin, UDS_VIN_LEN);
    memcpy(ctx->part_no, k_part, UDS_PART_LEN);
}

static uint16_t make_negative(uint8_t *resp, uint8_t sid, uint8_t nrc)
{
    resp[0] = 0x7Fu;
    resp[1] = sid;
    resp[2] = nrc;
    return 3;
}

static uint16_t session_control(uds_ctx_t *ctx, const uint8_t *req, uint16_t len, uint8_t *resp)
{
    if (len != 2) {
        return make_negative(resp, req[0], NRC_LENGTH_INVALID);
    }
    if (req[1] != 0x01u && req[1] != 0x02u && req[1] != 0x03u) {
        return make_negative(resp, req[0], NRC_SUBFUNC_NOT_SUPPORTED);
    }
    if (req[1] != 0x01u) {
        /* Leaving the default session drops security access. */
        ctx->unlocked = 0;
        ctx->seed_pending = 0;
    }
    ctx->session = req[1];
    resp[0] = 0x50u;
    resp[1] = req[1];
    resp[2] = 0x00u; /* P2 max, 50 ms */
    resp[3] = 0x32u;
    return 4;
}

static uint16_t read_data_by_id(uds_ctx_t *ctx, const uint8_t *req, uint16_t len, uint8_t *resp)
{
    uint16_t did;

    if (len != 3) {
        return make_negative(resp, req[0], NRC_LENGTH_INVALID);
    }
    did = (uint16_t)((req[1] << 8) | req[2]);
    if (did == 0xF186u) {
        resp[0] = 0x62u;
        resp[1] = req[1];
        resp[2] = req[2];
        resp[3] = ctx->session;
        return 4;
    }
    if (did == 0xF190u) {
        resp[0] = 0x62u;
        resp[1] = req[1];
        resp[2] = req[2];
        memcpy(&resp[3], ctx->vin, UDS_VIN_LEN);
        return (uint16_t)(3 + UDS_VIN_LEN);
    }
    if (did == 0xF187u) {
        resp[0] = 0x62u;
        resp[1] = req[1];
        resp[2] = req[2];
        memcpy(&resp[3], ctx->part_no, UDS_PART_LEN);
        return (uint16_t)(3 + UDS_PART_LEN);
    }
    return make_negative(resp, req[0], NRC_OUT_OF_RANGE);
}

static uint16_t security_access(uds_ctx_t *ctx, const uint8_t *req, uint16_t len, uint8_t *resp)
{
    if (len < 2) {
        return make_negative(resp, req[0], NRC_LENGTH_INVALID);
    }
    if (req[1] == 0x01u) {
        if (len != 2) {
            return make_negative(resp, req[0], NRC_LENGTH_INVALID);
        }
        ctx->seed = 0x1234u;
        ctx->seed_pending = 1;
        ctx->unlocked = 0;
        resp[0] = 0x67u;
        resp[1] = 0x01u;
        resp[2] = (uint8_t)(ctx->seed >> 8);
        resp[3] = (uint8_t)(ctx->seed & 0xFFu);
        return 4;
    }
    if (req[1] == 0x02u) {
        uint16_t key;
        uint16_t expected;
        if (len != 4) {
            return make_negative(resp, req[0], NRC_LENGTH_INVALID);
        }
        if (!ctx->seed_pending) {
            return make_negative(resp, req[0], NRC_SEQUENCE_ERROR);
        }
        key = (uint16_t)((req[2] << 8) | req[3]);
        expected = (uint16_t)(ctx->seed ^ 0x5A5Au);
        ctx->seed_pending = 0;
        if (key != expected) {
            ctx->unlocked = 0;
            return make_negative(resp, req[0], NRC_INVALID_KEY);
        }
        ctx->unlocked = 1;
        resp[0] = 0x67u;
        resp[1] = 0x02u;
        return 2;
    }
    return make_negative(resp, req[0], NRC_SUBFUNC_NOT_SUPPORTED);
}

static uint16_t write_data_by_id(uds_ctx_t *ctx, const uint8_t *req, uint16_t len, uint8_t *resp)
{
    uint16_t did;

    if (ctx->session != 0x03u) {
        return make_negative(resp, req[0], NRC_CONDITIONS_NOT_CORRECT);
    }
    if (!ctx->unlocked) {
        return make_negative(resp, req[0], NRC_SECURITY_DENIED);
    }
    if (len < 4) {
        return make_negative(resp, req[0], NRC_LENGTH_INVALID);
    }
    did = (uint16_t)((req[1] << 8) | req[2]);
    if (did != 0xF190u) {
        return make_negative(resp, req[0], NRC_OUT_OF_RANGE);
    }
    if (len != (uint16_t)(3 + UDS_VIN_LEN)) {
        return make_negative(resp, req[0], NRC_LENGTH_INVALID);
    }
    memcpy(ctx->vin, &req[3], UDS_VIN_LEN);
    resp[0] = 0x6Eu;
    resp[1] = req[1];
    resp[2] = req[2];
    return 3;
}

isotp_status_t uds_handle(uds_ctx_t *ctx, const uint8_t *req, uint16_t req_len, uint8_t *resp, uint16_t *resp_len)
{
    if (ctx == NULL || req == NULL || req_len == 0 || resp == NULL || resp_len == NULL) {
        return ISOTP_ERR_ARG;
    }

    switch (req[0]) {
    case 0x10u:
        *resp_len = session_control(ctx, req, req_len, resp);
        break;
    case 0x11u:
        *resp_len = 2;
        resp[0] = 0x51u;
        resp[1] = (req_len >= 2) ? req[1] : 0x01u;
        uds_init(ctx); /* reset restores defaults */
        break;
    case 0x22u:
        *resp_len = read_data_by_id(ctx, req, req_len, resp);
        break;
    case 0x27u:
        *resp_len = security_access(ctx, req, req_len, resp);
        break;
    case 0x2Eu:
        *resp_len = write_data_by_id(ctx, req, req_len, resp);
        break;
    case 0x3Eu:
        if (req_len != 2) {
            *resp_len = make_negative(resp, req[0], NRC_LENGTH_INVALID);
        } else if (req[1] != 0x00u && req[1] != 0x80u) {
            *resp_len = make_negative(resp, req[0], NRC_SUBFUNC_NOT_SUPPORTED);
        } else {
            resp[0] = 0x7Eu;
            resp[1] = 0x00u;
            *resp_len = 2;
        }
        break;
    default:
        *resp_len = make_negative(resp, req[0], NRC_SERVICE_NOT_SUPPORTED);
        break;
    }
    return ISOTP_OK;
}
