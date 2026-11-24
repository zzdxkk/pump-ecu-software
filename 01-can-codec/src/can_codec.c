#include "can_codec.h"

#include <string.h>

/* Frame layout: pump status frame, 8 bytes.
 * Byte 0-1  PumpSpeed     16 bit Intel  1 rpm/LSB     0..6000
 * Byte 2-3  MotorCurrent  16 bit Intel  1 mA/LSB      0..3000
 * Byte 4    CoolantTemp    8 bit Intel  1 C/LSB, -40 offset -40..150
 * Byte 5    PumpCmd        8 bit Intel  1 %/LSB       0..100
 * Byte 6    FaultCode      8 bit Intel  1 code/LSB    0..255
 * Byte 7    StateFlags     8 bit Intel  1 flag/LSB    0..255
 * Motorola coverage: TorqueEst 16 bit Motorola 0.5 Nm/LSB 0..500
 */
static const can_signal_def_t k_signals[] = {
    {"PumpSpeed",    0,  16, BYTE_ORDER_INTEL,    1.0,   0.0,    0.0, 6000.0, 0},
    {"MotorCurrent", 16, 16, BYTE_ORDER_INTEL,    1.0,   0.0,    0.0, 3000.0, 0},
    {"CoolantTemp",  32,  8, BYTE_ORDER_INTEL,    1.0, -40.0,  -40.0,  150.0, 0},
    {"PumpCmd",      40,  8, BYTE_ORDER_INTEL,    1.0,   0.0,    0.0,  100.0, 0},
    {"FaultCode",    48,  8, BYTE_ORDER_INTEL,    1.0,   0.0,    0.0,  255.0, 0},
    {"StateFlags",   56,  8, BYTE_ORDER_INTEL,    1.0,   0.0,    0.0,  255.0, 0},
    {"TorqueEst",     7, 16, BYTE_ORDER_MOTOROLA, 0.5,   0.0,    0.0,  500.0, 0},
};

size_t can_signal_count(void)
{
    return sizeof(k_signals) / sizeof(k_signals[0]);
}

const can_signal_def_t *can_signal_at(size_t index)
{
    if (index >= can_signal_count()) {
        return NULL;
    }
    return &k_signals[index];
}

const can_signal_def_t *can_signal_lookup(const char *name)
{
    size_t i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < can_signal_count(); ++i) {
        if (strcmp(k_signals[i].name, name) == 0) {
            return &k_signals[i];
        }
    }
    return NULL;
}

static int args_ok(const uint8_t *data, size_t len, const can_signal_def_t *sig)
{
    size_t last_byte;

    if (data == NULL || sig == NULL) {
        return 0;
    }
    if (sig->length == 0 || sig->length > 64) {
        return 0;
    }
    if ((size_t)(sig->start_bit / 8) >= len) {
        return 0;
    }
    if (sig->order == BYTE_ORDER_MOTOROLA) {
        /* Replay the sawtooth walk to find the last byte touched. */
        int bit = (int)sig->start_bit;
        uint8_t off = (uint8_t)(bit % 8);
        uint8_t i;
        last_byte = (size_t)(bit / 8);
        for (i = 0; i < sig->length; ++i) {
            size_t byte = (size_t)(bit / 8);
            if (byte > last_byte) {
                last_byte = byte;
            }
            bit = (off == 0) ? bit + 15 : bit - 1;
            off = (uint8_t)(bit % 8);
        }
    } else {
        last_byte = (size_t)((sig->start_bit + sig->length - 1u) / 8u);
    }
    if (last_byte >= len) {
        return 0;
    }
    return 1;
}

/* Intel: start_bit is the LSB, bit index grows towards higher bit numbers. */
static uint64_t extract_intel(const uint8_t *data, const can_signal_def_t *sig)
{
    uint64_t raw = 0;
    uint8_t i;
    for (i = 0; i < sig->length; ++i) {
        uint16_t bit = (uint16_t)(sig->start_bit + i);
        uint8_t byte = (uint8_t)(bit / 8);
        uint8_t off = (uint8_t)(bit % 8);
        uint64_t value = (uint64_t)((data[byte] >> off) & 0x01u);
        raw |= value << i;
    }
    return raw;
}

/* Motorola: start_bit is the MSB, the bit cursor walks down within a byte
 * and jumps 15 bits when it crosses the byte boundary (DBC sawtooth). */
static uint64_t extract_motorola(const uint8_t *data, const can_signal_def_t *sig)
{
    uint64_t raw = 0;
    uint8_t i;
    int bit = (int)sig->start_bit;
    for (i = 0; i < sig->length; ++i) {
        uint8_t byte = (uint8_t)(bit / 8);
        uint8_t off = (uint8_t)(bit % 8);
        raw = (raw << 1) | (uint64_t)((data[byte] >> off) & 0x01u);
        bit = (off == 0) ? bit + 15 : bit - 1;
    }
    return raw;
}

uint64_t can_extract_raw(const uint8_t *data, size_t len, const can_signal_def_t *sig)
{
    if (!args_ok(data, len, sig)) {
        return 0;
    }
    if (sig->order == BYTE_ORDER_MOTOROLA) {
        return extract_motorola(data, sig);
    }
    return extract_intel(data, sig);
}

static void pack_intel(uint8_t *data, const can_signal_def_t *sig, uint64_t raw)
{
    uint8_t i;
    for (i = 0; i < sig->length; ++i) {
        uint16_t bit = (uint16_t)(sig->start_bit + i);
        uint8_t byte = (uint8_t)(bit / 8);
        uint8_t off = (uint8_t)(bit % 8);
        if ((raw >> i) & 0x01u) {
            data[byte] = (uint8_t)(data[byte] | (uint8_t)(1u << off));
        } else {
            data[byte] = (uint8_t)(data[byte] & (uint8_t)~(1u << off));
        }
    }
}

static void pack_motorola(uint8_t *data, const can_signal_def_t *sig, uint64_t raw)
{
    uint8_t i;
    int bit = (int)sig->start_bit;
    for (i = 0; i < sig->length; ++i) {
        uint8_t byte = (uint8_t)(bit / 8);
        uint8_t off = (uint8_t)(bit % 8);
        uint64_t bit_val = (raw >> (sig->length - 1u - i)) & 0x01u;
        if (bit_val) {
            data[byte] = (uint8_t)(data[byte] | (uint8_t)(1u << off));
        } else {
            data[byte] = (uint8_t)(data[byte] & (uint8_t)~(1u << off));
        }
        bit = (off == 0) ? bit + 15 : bit - 1;
    }
}

can_status_t can_pack_raw(uint8_t *data, size_t len, const can_signal_def_t *sig, uint64_t raw)
{
    if (!args_ok(data, len, sig)) {
        return CAN_ERR_ARG;
    }
    if (sig->length < 64) {
        uint64_t mask = ((uint64_t)1 << sig->length) - 1u;
        if ((raw & ~mask) != 0u) {
            return CAN_ERR_RANGE;
        }
    }
    if (sig->order == BYTE_ORDER_MOTOROLA) {
        pack_motorola(data, sig, raw);
    } else {
        pack_intel(data, sig, raw);
    }
    return CAN_OK;
}

static double raw_to_phys(const can_signal_def_t *sig, uint64_t raw)
{
    if (sig->is_signed && sig->length > 0 && sig->length < 64) {
        uint64_t sign_bit = (uint64_t)1 << (sig->length - 1u);
        if (raw & sign_bit) {
            int64_t signed_raw = (int64_t)raw - ((int64_t)1 << sig->length);
            return ((double)signed_raw * sig->factor) + sig->offset;
        }
    }
    return ((double)raw * sig->factor) + sig->offset;
}

can_status_t can_decode(const uint8_t *data, size_t len, const can_signal_def_t *sig, double *phys)
{
    double value;
    if (phys == NULL || !args_ok(data, len, sig)) {
        return CAN_ERR_ARG;
    }
    value = raw_to_phys(sig, can_extract_raw(data, len, sig));
    if (value < sig->min_phys - 1e-9 || value > sig->max_phys + 1e-9) {
        *phys = value;
        return CAN_ERR_RANGE;
    }
    *phys = value;
    return CAN_OK;
}

can_status_t can_encode(uint8_t *data, size_t len, const can_signal_def_t *sig, double phys)
{
    double raw_d;
    uint64_t raw;
    uint64_t mask;

    if (data == NULL || sig == NULL) {
        return CAN_ERR_ARG;
    }
    if (phys < sig->min_phys || phys > sig->max_phys) {
        return CAN_ERR_RANGE;
    }
    raw_d = (phys - sig->offset) / sig->factor;
    if (sig->length < 64) {
        mask = ((uint64_t)1 << sig->length) - 1u;
    } else {
        mask = ~(uint64_t)0;
    }
    if (sig->is_signed) {
        /* Two's complement: negative values fold into the upper half of the field. */
        double limit_low = -((double)((uint64_t)1 << (sig->length - 1u)));
        double limit_high = ((double)((uint64_t)1 << (sig->length - 1u))) - 1.0;
        if (raw_d < limit_low || raw_d > limit_high) {
            return CAN_ERR_RANGE;
        }
        raw = (uint64_t)(int64_t)(raw_d >= 0.0 ? (raw_d + 0.5) : (raw_d - 0.5));
    } else {
        if (raw_d < 0.0 || raw_d > (double)mask) {
            return CAN_ERR_RANGE;
        }
        raw = (uint64_t)(raw_d + 0.5);
    }
    raw &= mask;
    return can_pack_raw(data, len, sig, raw);
}
