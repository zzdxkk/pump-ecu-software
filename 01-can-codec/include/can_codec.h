#ifndef CAN_CODEC_H
#define CAN_CODEC_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    CAN_OK = 0,
    CAN_ERR_ARG = -1,
    CAN_ERR_RANGE = -2,
    CAN_ERR_UNKNOWN_SIGNAL = -3
} can_status_t;

typedef enum {
    BYTE_ORDER_INTEL = 0,    /* little endian, LSB first (DBC '@1') */
    BYTE_ORDER_MOTOROLA = 1  /* big endian, MSB first (DBC '@0')  */
} can_byte_order_t;

typedef struct {
    const char *name;
    uint16_t start_bit;
    uint8_t length;
    can_byte_order_t order;
    double factor;
    double offset;
    double min_phys;
    double max_phys;
    uint8_t is_signed;
} can_signal_def_t;

/* Raw bit field access. Both functions validate arguments against a 8 byte frame. */
uint64_t can_extract_raw(const uint8_t *data, size_t len, const can_signal_def_t *sig);
can_status_t can_pack_raw(uint8_t *data, size_t len, const can_signal_def_t *sig, uint64_t raw);

/* Physical value conversion with range guard. */
can_status_t can_decode(const uint8_t *data, size_t len, const can_signal_def_t *sig, double *phys);
can_status_t can_encode(uint8_t *data, size_t len, const can_signal_def_t *sig, double phys);

/* Signal database used by the pump ECU frame layout. */
size_t can_signal_count(void);
const can_signal_def_t *can_signal_at(size_t index);
const can_signal_def_t *can_signal_lookup(const char *name);

#endif /* CAN_CODEC_H */