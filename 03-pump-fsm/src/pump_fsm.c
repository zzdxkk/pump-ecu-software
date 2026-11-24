#include "pump_fsm.h"

#include <string.h>

/* Calibrated limits for a 12 V auxiliary coolant pump.
 *  supply  9.0 .. 16.0 V
 *  current trip 2500 mA, 50 ms persistence
 *  coolant derate above 120 C, resume below 110 C
 */
#define SUPPLY_MIN_V        9.0
#define SUPPLY_MAX_V        16.0
#define CURRENT_TRIP_MA     2500.0
#define CURRENT_TRIP_MS     50u
#define TEMP_DERATE_C       120.0
#define TEMP_RECOVER_C      110.0
#define TEMP_TRIP_MS        200u
#define TEMP_RECOVER_MS     300u
#define UNDERVOLT_TRIP_MS   100u
#define OVERVOLT_TRIP_MS    50u
#define INIT_SETTLE_MS      100u
#define PRECHARGE_MS        300u
#define FAULT_CLEAR_MS      100u
#define DERATE_PWM_LIMIT    50u

static int inputs_plausible(const pump_input_t *in)
{
    if (in->supply_v < -1.0 || in->supply_v > 40.0) {
        return 0;
    }
    if (in->current_ma < -50.0 || in->current_ma > 20000.0) {
        return 0;
    }
    if (in->temp_c < -60.0 || in->temp_c > 200.0) {
        return 0;
    }
    return 1;
}

static void enter_state(pump_ctx_t *ctx, pump_state_t next)
{
    if (ctx->state != next) {
        ctx->state = next;
        ctx->ms_in_state = 0;
    }
}

static void trip_fault(pump_ctx_t *ctx, uint8_t code)
{
    ctx->fault_code = code;
    ctx->pwm_pct = 0;
    ctx->power_stage = 0;
    enter_state(ctx, PUMP_FAULT);
}

static void accumulate(pump_ctx_t *ctx, uint32_t dt_ms)
{
    ctx->ms_in_state += dt_ms;
}

static uint8_t clamp_cmd(uint8_t cmd)
{
    return (cmd > 100u) ? 100u : cmd;
}

void pump_init(pump_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = PUMP_INIT;
    ctx->fault_code = PUMP_FAULT_NONE;
}

uint8_t pump_output_pwm(const pump_ctx_t *ctx)
{
    if (ctx == NULL) {
        return 0;
    }
    return ctx->pwm_pct;
}

uint8_t pump_power_stage(const pump_ctx_t *ctx)
{
    if (ctx == NULL) {
        return 0;
    }
    return ctx->power_stage;
}

pump_status_t pump_step(pump_ctx_t *ctx, const pump_input_t *in, uint32_t dt_ms)
{
    uint8_t cmd;

    if (ctx == NULL || in == NULL || dt_ms == 0u) {
        return PUMP_ERR_ARG;
    }
    cmd = clamp_cmd(in->cmd_pct);
    accumulate(ctx, dt_ms);

    if (!inputs_plausible(in)) {
        trip_fault(ctx, PUMP_FAULT_SENSOR);
        return PUMP_OK;
    }

    /* Fault latches regardless of the current state; only an explicit clear
     * request with healthy inputs brings the pump back. */
    if (ctx->state != PUMP_FAULT) {
        if (in->current_ma > CURRENT_TRIP_MA) {
            ctx->overcurrent_ms += dt_ms;
        } else {
            ctx->overcurrent_ms = 0;
        }
        if (in->supply_v < SUPPLY_MIN_V) {
            ctx->undervolt_ms += dt_ms;
        } else {
            ctx->undervolt_ms = 0;
        }
        if (in->supply_v > SUPPLY_MAX_V) {
            ctx->overvolt_ms += dt_ms;
        } else {
            ctx->overvolt_ms = 0;
        }
        if (in->temp_c > TEMP_DERATE_C) {
            ctx->overtemp_ms += dt_ms;
        } else {
            ctx->overtemp_ms = 0;
        }

        if (ctx->overcurrent_ms >= CURRENT_TRIP_MS) {
            trip_fault(ctx, PUMP_FAULT_OVERCURRENT);
            return PUMP_OK;
        }
        if (ctx->undervolt_ms >= UNDERVOLT_TRIP_MS) {
            trip_fault(ctx, PUMP_FAULT_UNDERVOLTAGE);
            return PUMP_OK;
        }
        if (ctx->overvolt_ms >= OVERVOLT_TRIP_MS) {
            trip_fault(ctx, PUMP_FAULT_OVERVOLTAGE);
            return PUMP_OK;
        }
        if (ctx->overtemp_ms >= TEMP_TRIP_MS && ctx->state != PUMP_DERATE) {
            enter_state(ctx, PUMP_DERATE);
            ctx->pwm_pct = (cmd < DERATE_PWM_LIMIT) ? cmd : DERATE_PWM_LIMIT;
            return PUMP_OK;
        }
    }

    switch (ctx->state) {
    case PUMP_INIT:
        ctx->pwm_pct = 0;
        ctx->power_stage = 0;
        if (ctx->ms_in_state >= INIT_SETTLE_MS) {
            enter_state(ctx, PUMP_STANDBY);
        }
        break;

    case PUMP_STANDBY:
        ctx->pwm_pct = 0;
        ctx->power_stage = 0;
        if (cmd > 0u) {
            ctx->power_stage = 1;
            enter_state(ctx, PUMP_PRECHARGE);
        }
        break;

    case PUMP_PRECHARGE:
        ctx->pwm_pct = 0;
        ctx->power_stage = 1;
        if (cmd == 0u) {
            enter_state(ctx, PUMP_STANDBY);
        } else if (ctx->ms_in_state >= PRECHARGE_MS) {
            ctx->pwm_pct = cmd;
            enter_state(ctx, PUMP_RUNNING);
        }
        break;

    case PUMP_RUNNING:
        ctx->power_stage = 1;
        if (cmd == 0u) {
            ctx->pwm_pct = 0;
            enter_state(ctx, PUMP_STANDBY);
        } else {
            ctx->pwm_pct = cmd;
            if (in->temp_c > TEMP_DERATE_C) {
                enter_state(ctx, PUMP_DERATE);
                ctx->pwm_pct = (cmd < DERATE_PWM_LIMIT) ? cmd : DERATE_PWM_LIMIT;
            }
        }
        break;

    case PUMP_DERATE:
        ctx->power_stage = 1;
        ctx->pwm_pct = (cmd < DERATE_PWM_LIMIT) ? cmd : DERATE_PWM_LIMIT;
        if (cmd == 0u) {
            ctx->pwm_pct = 0;
            enter_state(ctx, PUMP_STANDBY);
        } else if (in->temp_c <= TEMP_RECOVER_C && ctx->ms_in_state >= TEMP_RECOVER_MS) {
            enter_state(ctx, PUMP_RUNNING);
        }
        break;

    case PUMP_FAULT:
    default:
        ctx->pwm_pct = 0;
        ctx->power_stage = 0;
        if (in->clear_fault) {
            if (in->supply_v >= SUPPLY_MIN_V && in->supply_v <= SUPPLY_MAX_V &&
                in->current_ma <= CURRENT_TRIP_MA && in->temp_c <= TEMP_DERATE_C) {
                ctx->recovery_ms += dt_ms;
            } else {
                ctx->recovery_ms = 0;
            }
            if (ctx->recovery_ms >= FAULT_CLEAR_MS) {
                ctx->fault_code = PUMP_FAULT_NONE;
                ctx->overcurrent_ms = 0;
                ctx->undervolt_ms = 0;
                ctx->overvolt_ms = 0;
                ctx->overtemp_ms = 0;
                ctx->recovery_ms = 0;
                enter_state(ctx, PUMP_STANDBY);
            }
        } else {
            ctx->recovery_ms = 0;
        }
        break;
    }

    return PUMP_OK;
}
