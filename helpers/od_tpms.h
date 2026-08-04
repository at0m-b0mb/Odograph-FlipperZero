/**
 * Odograph - TPMS protocol matcher.
 *
 * Pure logic. Takes a buffer of demodulated *chips* (the raw half-bit stream
 * the CC1101's discriminator hands us, one bit per chip) and tries every
 * supported tyre-sensor protocol against it. Every candidate is gated on that
 * protocol's own CRC or checksum, so a match is a match - we never guess a
 * sensor ID out of noise.
 *
 * No Flipper headers here on purpose: this file compiles on the host, and
 * test/host_tpms_test.c hammers it with synthesised bursts.
 *
 * Protocol field layouts, checksum polynomials and timings were taken from the
 * public rtl_433 protocol documentation (see PROTOCOLS.md for the per-protocol
 * citation). The implementation below is original.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Chip (half-bit) period of each radio profile, in microseconds. */
#define OD_CHIP_US_FSK 52 /* 19.2 kchip/s - Ford/Renault/Citroen/VDO/Toyota */
#define OD_CHIP_US_OOK 120 /* 8.3 kchip/s  - Schrader ASK                    */

/** Longest burst we will hold. 1024 chips is ~53 ms of FSK, ~123 ms of OOK -
 * comfortably more than the ~10 ms a single TPMS packet occupies. */
#define OD_MAX_CHIPS 1024

typedef enum {
    OdProtoUnknown = 0, /* clean Manchester burst, no protocol claimed it */
    OdProtoFord,
    OdProtoRenault,
    OdProtoCitroen,
    OdProtoHyundaiVdo,
    OdProtoToyota,
    OdProtoSchrader,
    OdProtoCount,
} OdProto;

/** Human name, e.g. "Ford". Safe for every OdProto value. */
const char* od_tpms_proto_name(OdProto proto);

/** Which car makes ship this sensor family, e.g. "Ford / Lincoln". */
const char* od_tpms_proto_makes(OdProto proto);

/**
 * How wide this family's serial really is: 24, 28 or 32 bits. Used wherever a
 * serial has to be rendered without a live reading to take the width from -
 * the garage list, for one, where getting it wrong pads a 28-bit Schrader
 * serial out to eight digits and makes it look like a different sensor.
 */
uint8_t od_tpms_proto_id_bits(OdProto proto);

/** One successfully decoded tyre-sensor transmission. */
typedef struct {
    OdProto proto;

    uint32_t id; /* the persistent sensor serial - the whole point */
    uint8_t id_bits; /* 24, 28 or 32: how wide that serial really is */

    int16_t pressure_dkpa; /* kPa x 10 */
    bool pressure_valid;

    int16_t temp_c;
    bool temp_valid;

    bool moving; /* sensor says the wheel is turning     */
    bool moving_valid;

    uint8_t raw[10]; /* decoded payload, pre-interpretation  */
    uint8_t raw_len;
} OdTpmsReading;

/**
 * Called once per decoded packet. A single burst usually contains several
 * repeats of the same packet, so expect to be called more than once.
 */
typedef void (*OdTpmsFoundCb)(const OdTpmsReading* reading, void* context);

/**
 * Try every protocol against a chip buffer.
 *
 * @param chips      bit-packed chip stream, MSB-first within each byte
 * @param chip_count number of valid chips in @p chips
 * @param chip_us    the chip period the buffer was sampled at; selects which
 *                   protocol families are plausible (52 -> FSK set, 120 -> ASK)
 * @return number of packets that passed their integrity check
 */
uint8_t od_tpms_scan(
    const uint8_t* chips,
    uint16_t chip_count,
    uint16_t chip_us,
    OdTpmsFoundCb callback,
    void* context);

/* ------------------------------------------------------------ internals --
 * Exposed for the host test only.
 */

/** CRC-8, MSB-first, no reflection, no final xor. */
uint8_t od_tpms_crc8(const uint8_t* data, size_t len, uint8_t poly, uint8_t init);

/** Read chip @p index out of a bit-packed buffer. */
static inline bool od_chip_at(const uint8_t* chips, uint16_t index) {
    return (chips[index >> 3] >> (7 - (index & 7))) & 1;
}

/** Write chip @p index into a bit-packed buffer. */
static inline void od_chip_set(uint8_t* chips, uint16_t index, bool value) {
    uint8_t mask = (uint8_t)(1u << (7 - (index & 7)));
    if(value) {
        chips[index >> 3] |= mask;
    } else {
        chips[index >> 3] &= (uint8_t)~mask;
    }
}

#ifdef __cplusplus
}
#endif
