#include "uds_diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, (msg));             \
        }                                                                      \
    } while (0)

static uint16_t run(uds_ctx_t *ctx, const uint8_t *req, uint16_t len, uint8_t *resp)
{
    uint16_t resp_len = 0;
    isotp_status_t st = uds_handle(ctx, req, len, resp, &resp_len);
    CHECK(st == ISOTP_OK, "uds_handle must not fail on valid PDU");
    return resp_len;
}

static void test_session_control(void)
{
    uds_ctx_t ctx;
    uint8_t resp[UDS_MAX_PAYLOAD];
    uint8_t req[2] = {0x10, 0x03};
    uint16_t n;

    uds_init(&ctx);
    CHECK(ctx.session == 0x01, "default session after init");

    n = run(&ctx, req, 2, resp);
    CHECK(n == 4 && resp[0] == 0x50 && resp[1] == 0x03, "extended session positive response");
    CHECK(ctx.session == 0x03, "context tracks extended session");

    req[1] = 0x05;
    n = run(&ctx, req, 2, resp);
    CHECK(n == 3 && resp[0] == 0x7F && resp[2] == 0x12, "unsupported subfunction gives NRC 0x12");
    CHECK(ctx.session == 0x03, "session unchanged after rejected request");

    req[1] = 0x03;
    n = run(&ctx, req, 1, resp);
    CHECK(n == 3 && resp[2] == 0x13, "short request gives NRC 0x13");

    req[1] = 0x02;
    (void)run(&ctx, req, 2, resp);
    CHECK((ctx.session == 0x02 && ctx.unlocked == 0), "programming session drops security access");
}

static void test_read_did(void)
{
    uds_ctx_t ctx;
    uint8_t resp[UDS_MAX_PAYLOAD];
    uint8_t req[3] = {0x22, 0xF1, 0x86};
    uint16_t n;

    uds_init(&ctx);
    n = run(&ctx, req, 3, resp);
    CHECK(n == 4 && resp[0] == 0x62 && resp[3] == 0x01, "F186 reports active session");

    req[1] = 0xF1;
    req[2] = 0x90;
    n = run(&ctx, req, 3, resp);
    CHECK(n == 20 && resp[0] == 0x62, "F190 returns 17 byte VIN");
    CHECK(resp[3] == 'L', "VIN payload starts with L");

    req[2] = 0x87;
    n = run(&ctx, req, 3, resp);
    CHECK(n == 11 && resp[0] == 0x62, "F187 returns 8 byte part number");

    req[1] = 0x12;
    req[2] = 0x34;
    n = run(&ctx, req, 3, resp);
    CHECK(n == 3 && resp[0] == 0x7F && resp[1] == 0x22 && resp[2] == 0x31,
          "unknown DID gives NRC 0x31");

    n = run(&ctx, req, 2, resp);
    CHECK(n == 3 && resp[2] == 0x13, "DID request without 2-byte id gives NRC 0x13");
}

static void test_security_access(void)
{
    uds_ctx_t ctx;
    uint8_t resp[UDS_MAX_PAYLOAD];
    uint8_t req[4] = {0x27, 0x01, 0x00, 0x00};
    uint16_t n;

    uds_init(&ctx);
    n = run(&ctx, req, 2, resp);
    CHECK(n == 4 && resp[0] == 0x67 && resp[1] == 0x01, "seed request returns seed");

    req[0] = 0x27;
    req[1] = 0x02;
    req[2] = 0xDE;
    req[3] = 0xAD;
    n = run(&ctx, req, 4, resp);
    CHECK(n == 3 && resp[2] == 0x35, "wrong key gives NRC 0x35");
    CHECK(ctx.unlocked == 0, "wrong key leaves ECU locked");

    req[1] = 0x02;
    req[2] = 0x48;
    req[3] = 0x6E; /* 0x1234 ^ 0x5A5A */
    n = run(&ctx, req, 4, resp);
    CHECK(n == 3 && resp[2] == 0x24, "key without seed gives sequence error 0x24");

    req[1] = 0x01;
    (void)run(&ctx, req, 2, resp);
    req[1] = 0x02;
    req[2] = 0x48;
    req[3] = 0x6E;
    n = run(&ctx, req, 4, resp);
    CHECK(n == 2 && resp[0] == 0x67 && resp[1] == 0x02, "correct key unlocks");
    CHECK(ctx.unlocked == 1, "context marks unlocked");

    req[1] = 0x09;
    n = run(&ctx, req, 2, resp);
    CHECK(n == 3 && resp[2] == 0x12, "unsupported security level gives NRC 0x12");
}

static void test_write_did_conditions(void)
{
    uds_ctx_t ctx;
    uint8_t resp[UDS_MAX_PAYLOAD];
    uint8_t req[3 + UDS_VIN_LEN];
    uint16_t n;
    int i;

    uds_init(&ctx);
    req[0] = 0x2E;
    req[1] = 0xF1;
    req[2] = 0x90;
    for (i = 0; i < UDS_VIN_LEN; ++i) {
        req[3 + i] = (uint8_t)('A' + (i % 26));
    }

    n = run(&ctx, req, (uint16_t)(3 + UDS_VIN_LEN), resp);
    CHECK(n == 3 && resp[2] == 0x22, "write in default session gives NRC 0x22");

    {
        uint8_t enter[2] = {0x10, 0x03};
        (void)run(&ctx, enter, 2, resp);
    }
    n = run(&ctx, req, (uint16_t)(3 + UDS_VIN_LEN), resp);
    CHECK(n == 3 && resp[2] == 0x33, "write without unlock gives NRC 0x33");

    {
        uint8_t seed[2] = {0x27, 0x01};
        uint8_t key[4] = {0x27, 0x02, 0x48, 0x6E};
        (void)run(&ctx, seed, 2, resp);
        n = run(&ctx, key, 4, resp);
        CHECK(n == 2 && resp[0] == 0x67, "unlock before write");
    }
    n = run(&ctx, req, (uint16_t)(3 + UDS_VIN_LEN), resp);
    CHECK(n == 3 && resp[0] == 0x6E, "write VIN accepted after unlock");

    n = run(&ctx, req, 4, resp);
    CHECK(n == 3 && resp[2] == 0x13, "write with wrong payload length gives NRC 0x13");

    {
        uint8_t read[3] = {0x22, 0xF1, 0x90};
        n = run(&ctx, read, 3, resp);
        CHECK(n == 20 && resp[3] == req[3], "written VIN is read back");
    }
}

static void test_tester_present_and_unknown_service(void)
{
    uds_ctx_t ctx;
    uint8_t resp[UDS_MAX_PAYLOAD];
    uint8_t req[2] = {0x3E, 0x00};
    uint16_t n;

    uds_init(&ctx);
    n = run(&ctx, req, 2, resp);
    CHECK(n == 2 && resp[0] == 0x7E, "tester present acknowledged");

    req[1] = 0x80;
    n = run(&ctx, req, 2, resp);
    CHECK(n == 2 && resp[0] == 0x7E, "suppress positive response still returns PDU here");

    {
        uint8_t bad[2] = {0x3E, 0x07};
        n = run(&ctx, bad, 2, resp);
        CHECK(n == 3 && resp[2] == 0x12, "invalid tester present subfunction");
    }

    {
        uint8_t unknown[1] = {0x31};
        n = run(&ctx, unknown, 1, resp);
        CHECK(n == 3 && resp[0] == 0x7F && resp[1] == 0x31 && resp[2] == 0x11,
              "unsupported service gives NRC 0x11");
    }
}

static void test_isotp_single_frame(void)
{
    isotp_rx_t rx;
    uint8_t frame[8] = {0x02, 0x10, 0x03, 0, 0, 0, 0, 0};
    uint8_t complete = 0;

    isotp_rx_init(&rx);
    CHECK(isotp_rx_frame(&rx, frame, 8, &complete) == ISOTP_OK, "single frame accepted");
    CHECK(complete == 1, "single frame completes immediately");
    CHECK(rx.total == 2 && rx.buf[0] == 0x10 && rx.buf[1] == 0x03, "payload copied in order");

    frame[0] = 0x09; /* claims 9 bytes, only 7 available */
    CHECK(isotp_rx_frame(&rx, frame, 8, &complete) == ISOTP_ERR_ARG, "overlong single frame rejected");
}

static void test_isotp_multi_frame(void)
{
    isotp_rx_t rx;
    uint8_t complete = 0;
    uint8_t ff[8] = {0x10, 0x1B, 0x2E, 0xF1, 0x90, 0x4C, 0x5A, 0x56};
    uint8_t cf1[8] = {0x21, 0x50, 0x55, 0x4D, 0x50, 0x30, 0x30, 0x30};
    uint8_t cf2[8] = {0x22, 0x30, 0x30, 0x30, 0x30, 0x31, 0x37, 0xAA};
    uint8_t cf3[8] = {0x23, 0xAA, 0xAA, 0xAA, 0, 0, 0, 0};
    uint8_t bad_cf[8] = {0x25, 0x01, 0, 0, 0, 0, 0, 0};

    isotp_rx_init(&rx);
    CHECK(isotp_rx_frame(&rx, ff, 8, &complete) == ISOTP_INCOMPLETE, "first frame starts transfer");
    CHECK(complete == 0 && rx.total == 27, "27 byte payload announced");
    CHECK(isotp_rx_frame(&rx, cf1, 8, &complete) == ISOTP_OK, "first consecutive frame ok");
    CHECK(isotp_rx_frame(&rx, cf2, 8, &complete) == ISOTP_OK, "second consecutive frame ok");
    CHECK(complete == 0, "transfer is still open after 20 of 27 bytes");
    CHECK(isotp_rx_frame(&rx, cf3, 8, &complete) == ISOTP_OK, "third consecutive frame ok");
    CHECK(complete == 1, "payload complete after last frame");
    CHECK(rx.got == 27 && rx.buf[0] == 0x2E && rx.buf[1] == 0xF1 && rx.buf[2] == 0x90,
          "reassembled payload keeps header bytes");
    CHECK(rx.buf[3] == 'L' && rx.buf[19] == 0xAA && rx.buf[20] == 0xAA,
          "boundary bytes preserved");

    isotp_rx_init(&rx);
    (void)isotp_rx_frame(&rx, ff, 8, &complete);
    CHECK(isotp_rx_frame(&rx, bad_cf, 8, &complete) == ISOTP_ERR_SEQ,
          "sequence number mismatch aborts transfer");
    CHECK(rx.active == 0, "receiver goes idle after sequence error");
}

static void test_isotp_tx(void)
{
    uint8_t payload[20];
    uint8_t frames[16][8];
    uint8_t count = 0;
    int i;

    for (i = 0; i < 20; ++i) {
        payload[i] = (uint8_t)i;
    }
    CHECK(isotp_tx_segment(payload, 20, frames, &count) == ISOTP_OK, "tx segmentation ok");
    CHECK(count == 3, "20 bytes need FF + 2 CF");
    CHECK(frames[0][0] == 0x10 && frames[0][1] == 0x14, "first frame announces 20 bytes");
    CHECK(frames[1][0] == 0x21 && frames[2][0] == 0x22, "consecutive frames carry SN 1 then 2");
    CHECK(frames[0][6] == 4 && frames[1][1] == 6, "byte stream split at 6 then 7");

    CHECK(isotp_tx_segment(payload, 3, frames, &count) == ISOTP_OK, "single frame tx ok");
    CHECK(count == 1 && frames[0][0] == 0x03, "single frame carries length in low nibble");
    CHECK(isotp_tx_segment(payload, 0, frames, &count) == ISOTP_ERR_ARG, "empty payload rejected");
}

static void test_isotp_to_uds_end_to_end(void)
{
    isotp_rx_t rx;
    uds_ctx_t ctx;
    uint8_t frames[16][8];
    uint8_t count = 0;
    uint8_t resp[UDS_MAX_PAYLOAD];
    uint16_t resp_len = 0;
    uint8_t complete = 0;
    uint8_t req[20];
    int i;

    /* Tester writes a 20 byte VIN request, ECU answers 0x7F 2E 22 in default session. */
    req[0] = 0x2E;
    req[1] = 0xF1;
    req[2] = 0x90;
    for (i = 0; i < UDS_VIN_LEN; ++i) {
        req[3 + i] = (uint8_t)('0' + (i % 10));
    }
    uds_init(&ctx);
    isotp_rx_init(&rx);

    CHECK(isotp_tx_segment(req, 20, frames, &count) == ISOTP_OK, "segment 20 byte request");
    for (i = 0; i < count; ++i) {
        (void)isotp_rx_frame(&rx, frames[i], 8, &complete);
        if (complete) {
            CHECK(uds_handle(&ctx, rx.buf, rx.total, resp, &resp_len) == ISOTP_OK, "uds handles reassembled request");
        }
    }
    CHECK(complete == 1, "request fully reassembled");
    CHECK(resp_len == 3 && resp[0] == 0x7F && resp[1] == 0x2E && resp[2] == 0x22,
          "default session write is refused with conditions not correct");
}

int main(void)
{
    test_session_control();
    test_read_did();
    test_security_access();
    test_write_did_conditions();
    test_tester_present_and_unknown_service();
    test_isotp_single_frame();
    test_isotp_multi_frame();
    test_isotp_tx();
    test_isotp_to_uds_end_to_end();

    printf("uds_diag: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
