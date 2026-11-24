#include "can_codec.h"
#include "pump_fsm.h"

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

static pump_input_t healthy_input(uint8_t cmd)
{
    pump_input_t in;
    in.supply_v = 12.5;
    in.current_ma = 900.0;
    in.temp_c = 45.0;
    in.cmd_pct = cmd;
    in.clear_fault = 0;
    return in;
}

static void step_ms(pump_ctx_t *ctx, pump_input_t *in, uint32_t total_ms)
{
    uint32_t t;
    for (t = 0; t < total_ms; t += 10u) {
        (void)pump_step(ctx, in, 10u);
    }
}

static void bring_to_running(pump_ctx_t *ctx, uint8_t cmd)
{
    pump_input_t in = healthy_input(cmd);
    step_ms(ctx, &in, 100u);   /* INIT settle   */
    step_ms(ctx, &in, 320u);   /* PRECHARGE     */
}

static void test_start_sequence(void)
{
    pump_ctx_t ctx;
    pump_input_t in = healthy_input(0);

    pump_init(&ctx);
    CHECK(ctx.state == PUMP_INIT, "init state");
    CHECK(pump_output_pwm(&ctx) == 0, "pwm off during init");
    step_ms(&ctx, &in, 90u);
    CHECK(ctx.state == PUMP_INIT, "still init before 100 ms");
    step_ms(&ctx, &in, 20u);
    CHECK(ctx.state == PUMP_STANDBY, "standby after settle time");

    in.cmd_pct = 40;
    (void)pump_step(&ctx, &in, 10u);
    CHECK(ctx.state == PUMP_PRECHARGE, "precharge on command");
    CHECK(pump_output_pwm(&ctx) == 0 && pump_power_stage(&ctx) == 1,
          "precharge keeps pwm at zero with power stage on");

    step_ms(&ctx, &in, 290u);
    CHECK(ctx.state == PUMP_PRECHARGE, "precharge lasts 300 ms");
    step_ms(&ctx, &in, 20u);
    CHECK(ctx.state == PUMP_RUNNING, "running after precharge");
    CHECK(pump_output_pwm(&ctx) == 40, "pwm follows command");

    in.cmd_pct = 0;
    (void)pump_step(&ctx, &in, 10u);
    CHECK(ctx.state == PUMP_STANDBY && pump_output_pwm(&ctx) == 0,
          "command zero returns to standby");
}

static void test_overcurrent_latch(void)
{
    pump_ctx_t ctx;
    pump_input_t in = healthy_input(80);

    pump_init(&ctx);
    bring_to_running(&ctx, 80);
    CHECK(ctx.state == PUMP_RUNNING, "running before fault injection");

    in.current_ma = 2600.0;
    step_ms(&ctx, &in, 40u);
    CHECK(ctx.state == PUMP_RUNNING, "40 ms of overcurrent is not yet a trip");
    step_ms(&ctx, &in, 20u);
    CHECK(ctx.state == PUMP_FAULT, "overcurrent trips after 50 ms");
    CHECK(ctx.fault_code == PUMP_FAULT_OVERCURRENT, "fault code overcurrent");
    CHECK(pump_output_pwm(&ctx) == 0 && pump_power_stage(&ctx) == 0,
          "fault cuts pwm and power stage");

    in.current_ma = 500.0;
    step_ms(&ctx, &in, 500u);
    CHECK(ctx.state == PUMP_FAULT, "fault stays latched without clear request");
    CHECK(pump_output_pwm(&ctx) == 0, "pwm stays zero while latched");
}

static void test_undervoltage_and_overvoltage(void)
{
    pump_ctx_t ctx;
    pump_input_t in = healthy_input(60);

    pump_init(&ctx);
    bring_to_running(&ctx, 60);
    in.supply_v = 7.5;
    step_ms(&ctx, &in, 120u);
    CHECK(ctx.state == PUMP_FAULT && ctx.fault_code == PUMP_FAULT_UNDERVOLTAGE,
          "undervoltage trips after 100 ms");

    pump_init(&ctx);
    in = healthy_input(0);
    step_ms(&ctx, &in, 120u);
    in.supply_v = 17.5;
    step_ms(&ctx, &in, 60u);
    CHECK(ctx.state == PUMP_FAULT && ctx.fault_code == PUMP_FAULT_OVERVOLTAGE,
          "overvoltage trips after 50 ms");
}

static void test_overtemp_derate_and_recover(void)
{
    pump_ctx_t ctx;
    pump_input_t in = healthy_input(90);

    pump_init(&ctx);
    bring_to_running(&ctx, 90);
    CHECK(pump_output_pwm(&ctx) == 90, "full command before derate");

    in.temp_c = 130.0;
    step_ms(&ctx, &in, 210u);
    CHECK(ctx.state == PUMP_DERATE, "over temperature enters derate");
    CHECK(pump_output_pwm(&ctx) <= 50, "derate limits pwm to 50 percent");

    in.temp_c = 100.0;
    step_ms(&ctx, &in, 320u);
    CHECK(ctx.state == PUMP_RUNNING, "cooling below 110 C for 300 ms restores running");
    CHECK(pump_output_pwm(&ctx) == 90, "command restored after recovery");
}

static void test_fault_clear_gate(void)
{
    pump_ctx_t ctx;
    pump_input_t in = healthy_input(70);

    pump_init(&ctx);
    bring_to_running(&ctx, 70);
    in.current_ma = 3000.0;
    step_ms(&ctx, &in, 80u);
    CHECK(ctx.state == PUMP_FAULT, "trip before clear test");

    in.cmd_pct = 0;
    in.clear_fault = 1; /* current still high, clear must be refused */
    step_ms(&ctx, &in, 200u);
    CHECK(ctx.state == PUMP_FAULT, "clear refused while current still out of range");

    in.current_ma = 800.0;
    step_ms(&ctx, &in, 60u);
    CHECK(ctx.state == PUMP_FAULT, "clear needs 100 ms of healthy inputs");
    step_ms(&ctx, &in, 60u);
    CHECK(ctx.state == PUMP_STANDBY, "healthy clear returns to standby");
    CHECK(ctx.fault_code == PUMP_FAULT_NONE, "fault code cleared");
    CHECK(pump_output_pwm(&ctx) == 0, "pwm stays zero after recovery until next command");

    in.clear_fault = 0;
    in.cmd_pct = 30;
    step_ms(&ctx, &in, 330u);
    CHECK(ctx.state == PUMP_RUNNING && pump_output_pwm(&ctx) == 30,
          "pump can restart after a cleared fault");
}

static void test_implausible_inputs(void)
{
    pump_ctx_t ctx;
    pump_input_t in = healthy_input(50);

    pump_init(&ctx);
    bring_to_running(&ctx, 50);
    in.temp_c = 250.0; /* open circuit on the NTC */
    (void)pump_step(&ctx, &in, 10u);
    CHECK(ctx.state == PUMP_FAULT && ctx.fault_code == PUMP_FAULT_SENSOR,
          "implausible sensor value trips sensor fault immediately");

    pump_init(&ctx);
    in = healthy_input(50);
    (void)pump_step(&ctx, &in, 0u);
    CHECK(ctx.state == PUMP_INIT, "zero dt is rejected and state untouched");
}

static void test_argument_validation(void)
{
    pump_ctx_t ctx;
    pump_input_t in = healthy_input(10);

    CHECK(pump_step(NULL, &in, 10u) == PUMP_ERR_ARG, "NULL context rejected");
    pump_init(&ctx);
    CHECK(pump_step(&ctx, NULL, 10u) == PUMP_ERR_ARG, "NULL input rejected");
    CHECK(pump_output_pwm(NULL) == 0 && pump_power_stage(NULL) == 0,
          "NULL output accessors are safe");
}

/* Property style fault injection: random sensor traces must never produce an
 * unsafe output combination. */
static void test_random_fault_injection(void)
{
    pump_ctx_t ctx;
    int i;
    int unsafe_pwm = 0;
    int unsafe_state = 0;
    int bad_fault_code = 0;
    pump_input_t in;

    srand(20261124);
    pump_init(&ctx);

    for (i = 0; i < 4000; ++i) {
        in.supply_v = 4.0 + (double)(rand() % 160) / 10.0;   /* 4.0 .. 19.9 V */
        in.current_ma = (double)(rand() % 4000);             /* 0 .. 3999 mA */
        in.temp_c = -20.0 + (double)(rand() % 180);          /* -20 .. 159 C */
        in.cmd_pct = (uint8_t)(rand() % 130);                /* includes >100 */
        in.clear_fault = (uint8_t)((rand() % 5 == 0) ? 1 : 0);

        (void)pump_step(&ctx, &in, (uint32_t)(1 + rand() % 40));

        if (pump_output_pwm(&ctx) > 100) {
            unsafe_pwm = 1;
        }
        if (pump_output_pwm(&ctx) > in.cmd_pct) {
            unsafe_pwm = 1;
        }
        if (ctx.state == PUMP_FAULT && pump_output_pwm(&ctx) != 0) {
            unsafe_state = 1;
        }
        if (ctx.fault_code > PUMP_FAULT_SENSOR) {
            bad_fault_code = 1;
        }
    }

    CHECK(unsafe_pwm == 0, "pwm never exceeds command or 100 percent");
    CHECK(unsafe_state == 0, "fault state always drives pwm to zero");
    CHECK(bad_fault_code == 0, "fault code stays inside the defined set");
}

/* Integration test: CAN command frame -> FSM -> CAN status frame. */
static void test_can_integration(void)
{
    const can_signal_def_t *cmd_sig = can_signal_lookup("PumpCmd");
    const can_signal_def_t *speed_sig = can_signal_lookup("PumpSpeed");
    const can_signal_def_t *fault_sig = can_signal_lookup("FaultCode");
    uint8_t rx_frame[8] = {0};
    uint8_t tx_frame[8] = {0};
    double phys = 0.0;
    pump_ctx_t ctx;
    pump_input_t in = healthy_input(0);

    CHECK(cmd_sig != NULL && speed_sig != NULL && fault_sig != NULL,
          "integration needs the three pump signals");

    /* Tester requests 60 % over CAN. */
    CHECK(can_encode(rx_frame, 8, cmd_sig, 60.0) == CAN_OK, "encode command frame");
    CHECK(can_decode(rx_frame, 8, cmd_sig, &phys) == CAN_OK, "decode command frame");
    in.cmd_pct = (uint8_t)phys;

    pump_init(&ctx);
    step_ms(&ctx, &in, 100u);
    step_ms(&ctx, &in, 320u);
    CHECK(ctx.state == PUMP_RUNNING, "fsm reaches running from CAN command");
    CHECK(pump_output_pwm(&ctx) == 60, "fsm pwm matches decoded CAN command");

    /* ECU publishes status: 4200 rpm, 950 mA, fault none. */
    CHECK(can_encode(tx_frame, 8, speed_sig, 4200.0) == CAN_OK, "encode speed");
    CHECK(can_encode(tx_frame, 8, fault_sig, (double)ctx.fault_code) == CAN_OK, "encode fault code");
    CHECK(can_decode(tx_frame, 8, speed_sig, &phys) == CAN_OK && phys == 4200.0,
          "status frame carries speed");
    CHECK(can_decode(tx_frame, 8, fault_sig, &phys) == CAN_OK &&
          phys == (double)PUMP_FAULT_NONE, "status frame carries fault code");
}

int main(void)
{
    test_start_sequence();
    test_overcurrent_latch();
    test_undervoltage_and_overvoltage();
    test_overtemp_derate_and_recover();
    test_fault_clear_gate();
    test_implausible_inputs();
    test_argument_validation();
    test_random_fault_injection();
    test_can_integration();

    printf("pump_fsm: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
