#ifndef PUMP_FSM_H
#define PUMP_FSM_H

#include <stdint.h>

typedef enum {
    PUMP_INIT = 0,
    PUMP_STANDBY,
    PUMP_PRECHARGE,
    PUMP_RUNNING,
    PUMP_DERATE,
    PUMP_FAULT
} pump_state_t;

typedef enum {
    PUMP_OK = 0,
    PUMP_ERR_ARG = -1
} pump_status_t;

#define PUMP_FAULT_NONE          0x00u
#define PUMP_FAULT_OVERCURRENT   0x01u
#define PUMP_FAULT_UNDERVOLTAGE  0x02u
#define PUMP_FAULT_OVERVOLTAGE   0x03u
#define PUMP_FAULT_OVERTEMP      0x04u
#define PUMP_FAULT_SENSOR        0x05u

typedef struct {
    double supply_v;
    double current_ma;
    double temp_c;
    uint8_t cmd_pct;
    uint8_t clear_fault;
} pump_input_t;

typedef struct {
    pump_state_t state;
    uint8_t pwm_pct;
    uint8_t fault_code;
    uint8_t power_stage;
    uint32_t ms_in_state;
    uint32_t overcurrent_ms;
    uint32_t overtemp_ms;
    uint32_t undervolt_ms;
    uint32_t overvolt_ms;
    uint32_t recovery_ms;
} pump_ctx_t;

void pump_init(pump_ctx_t *ctx);
pump_status_t pump_step(pump_ctx_t *ctx, const pump_input_t *in, uint32_t dt_ms);

/* Safety outputs, exposed so tests and the CAN layer can assert on them. */
uint8_t pump_output_pwm(const pump_ctx_t *ctx);
uint8_t pump_power_stage(const pump_ctx_t *ctx);

#endif /* PUMP_FSM_H */