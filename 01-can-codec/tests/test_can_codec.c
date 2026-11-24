#include "can_codec.h"

#include <math.h>
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

static int close_to(double a, double b)
{
    return fabs(a - b) < 1e-6;
}

static void test_roundtrip_intel(void)
{
    const can_signal_def_t *speed = can_signal_lookup("PumpSpeed");
    uint8_t frame[8] = {0};
    uint64_t raw;
    double phys;
    uint32_t v;

    CHECK(speed != NULL, "PumpSpeed must exist in signal database");
    for (v = 0; v <= 6000; v += 137) {
        can_status_t st = can_encode(frame, 8, speed, (double)v);
        CHECK(st == CAN_OK, "encode within range must succeed");
        st = can_decode(frame, 8, speed, &phys);
        CHECK(st == CAN_OK, "decode must succeed");
        CHECK(close_to(phys, (double)v), "roundtrip must be lossless for 1:1 signal");
    }

    memset(frame, 0, sizeof(frame));
    raw = 0x10000u;
    CHECK(can_pack_raw(frame, 8, speed, raw) == CAN_ERR_RANGE,
          "raw value wider than signal length must be rejected");
}

static void test_known_frame_bytes(void)
{
    const can_signal_def_t *speed = can_signal_lookup("PumpSpeed");
    const can_signal_def_t *cur = can_signal_lookup("MotorCurrent");
    const can_signal_def_t *temp = can_signal_lookup("CoolantTemp");
    uint8_t frame[8] = {0x10, 0x27, 0x2C, 0x01, 0x41, 0x32, 0x03, 0x04};
    double phys;

    CHECK(can_decode(frame, 8, speed, &phys) == CAN_ERR_RANGE,
          "0x2710 = 10000 rpm exceeds the documented 0..6000 range");

    frame[0] = 0x88;
    frame[1] = 0x13; /* 0x1388 = 5000 rpm */
    CHECK(can_decode(frame, 8, speed, &phys) == CAN_OK, "speed decode 5000");
    CHECK(close_to(phys, 5000.0), "speed must decode to 5000 rpm");

    CHECK(can_decode(frame, 8, cur, &phys) == CAN_OK, "current decode");
    CHECK(close_to(phys, 300.0), "0x012C = 300 mA");

    CHECK(can_decode(frame, 8, temp, &phys) == CAN_OK, "temp decode");
    CHECK(close_to(phys, 25.0), "0x41 = 65, offset -40 gives 25 C");
}

static void test_range_guard(void)
{
    const can_signal_def_t *temp = can_signal_lookup("CoolantTemp");
    const can_signal_def_t *cmd = can_signal_lookup("PumpCmd");
    uint8_t frame[8] = {0};

    CHECK(can_encode(frame, 8, temp, 160.0) == CAN_ERR_RANGE, "temp above 150 must be rejected");
    CHECK(can_encode(frame, 8, temp, -50.0) == CAN_ERR_RANGE, "temp below -40 must be rejected");
    CHECK(can_encode(frame, 8, temp, -40.0) == CAN_OK, "temp at lower bound must be accepted");
    CHECK(can_encode(frame, 8, cmd, 101.0) == CAN_ERR_RANGE, "command above 100% must be rejected");
    CHECK(can_encode(frame, 8, cmd, 0.0) == CAN_OK, "command 0% must be accepted");
}

static void test_motorola_layout(void)
{
    const can_signal_def_t *torque = can_signal_lookup("TorqueEst");
    uint8_t frame[8] = {0};
    double phys;

    CHECK(torque != NULL, "TorqueEst must exist");
    CHECK(can_encode(frame, 8, torque, 100.0) == CAN_OK, "encode motorola 100 Nm");
    CHECK(can_decode(frame, 8, torque, &phys) == CAN_OK, "decode motorola 100 Nm");
    CHECK(close_to(phys, 100.0), "motorola roundtrip must be lossless");
    /* 100 Nm at 0.5 factor = raw 200, MSB starts at bit 7 (byte 0 bit 7) */
    CHECK(frame[0] == 0x00 && frame[1] == 0xC8,
          "motorola packing must place MSB in byte 0 and LSB in byte 1");
}

static void test_signed_signal(void)
{
    can_signal_def_t sig;
    uint8_t frame[8] = {0};
    double phys;

    memset(&sig, 0, sizeof(sig));
    sig.name = "SignedTemp";
    sig.start_bit = 0;
    sig.length = 8;
    sig.order = BYTE_ORDER_INTEL;
    sig.factor = 1.0;
    sig.offset = 0.0;
    sig.min_phys = -128.0;
    sig.max_phys = 127.0;
    sig.is_signed = 1;

    frame[0] = 0xF6; /* -10 in two's complement */
    CHECK(can_decode(frame, 8, &sig, &phys) == CAN_OK, "signed decode");
    CHECK(close_to(phys, -10.0), "0xF6 must decode to -10");

    CHECK(can_encode(frame, 8, &sig, -55.0) == CAN_OK, "signed encode");
    CHECK(frame[0] == 0xC9, "-55 must be packed as 0xC9");
}

static void test_argument_validation(void)
{
    const can_signal_def_t *speed = can_signal_lookup("PumpSpeed");
    uint8_t frame[8] = {0};
    double phys;

    CHECK(can_extract_raw(NULL, 8, speed) == 0, "NULL frame must return 0");
    CHECK(can_decode(frame, 8, NULL, &phys) == CAN_ERR_ARG, "NULL signal must be rejected");
    CHECK(can_decode(frame, 8, speed, NULL) == CAN_ERR_ARG, "NULL output must be rejected");
    CHECK(can_pack_raw(frame, 1, speed, 1) == CAN_ERR_ARG, "frame shorter than signal must be rejected");
    CHECK(can_encode(NULL, 8, speed, 100.0) == CAN_ERR_ARG, "NULL encode target must be rejected");
    CHECK(can_signal_lookup("NotASignal") == NULL, "unknown signal returns NULL");
}

static void test_database_iteration(void)
{
    size_t i;
    size_t count = can_signal_count();

    CHECK(count >= 7, "signal database must expose the 7 layout signals");
    for (i = 0; i < count; ++i) {
        const can_signal_def_t *s = can_signal_at(i);
        CHECK(s != NULL, "signal_at must never return NULL inside range");
        CHECK(s->length > 0 && s->length <= 32, "signal length must be sane");
        CHECK(s->factor > 0.0, "factor must be positive");
    }
    CHECK(can_signal_at(count) == NULL, "signal_at out of range returns NULL");
}

int main(void)
{
    test_roundtrip_intel();
    test_known_frame_bytes();
    test_range_guard();
    test_motorola_layout();
    test_signed_signal();
    test_argument_validation();
    test_database_iteration();

    printf("can_codec: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
