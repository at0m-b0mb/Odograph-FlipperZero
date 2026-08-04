/**
 * Odograph - what the sensors you just heard actually give away.
 *
 * A tyre sensor's serial number never changes. It is not encrypted, it is not
 * rotated, and it is broadcast in the clear every time the wheel turns. That
 * makes it a licence plate that works through walls, and unlike the metal one
 * you cannot cover it up.
 *
 * This turns a set of heard sensors into a number a person can act on: how
 * many bits of identity are on the air, and how many other cars in the world
 * would answer to the same fingerprint.
 *
 * Pure arithmetic. No Flipper headers, host tested.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Light vehicles in service worldwide, to one significant figure. Used only
 * as the denominator for "how many other cars share this fingerprint", so the
 * exact figure does not matter much - an order of magnitude either way moves
 * the answer by one sensor's worth of bits.
 */
#define OD_WORLD_VEHICLES 1500000000u

typedef enum {
    OdExposureNone = 0, /* nothing heard                                  */
    OdExposureLow, /* transmissions, but no serial we could read      */
    OdExposureModerate, /* one readable serial: narrows you a long way     */
    OdExposureHigh, /* several: unique in any realistic population     */
    OdExposureCritical, /* a full set, or a serial that came back later    */
} OdExposureLevel;

/** One sensor's contribution, as the store knows it. */
typedef struct {
    uint8_t id_bits; /* width of the decoded serial: 24, 28 or 32     */
    bool decoded; /* false for heuristic fingerprints              */
    bool returned; /* heard again after a gap - persistence, proven */
} OdExposeSensor;

typedef struct {
    uint8_t sensors; /* everything heard                             */
    uint8_t decoded; /* with a real serial                           */
    uint8_t unknown; /* fingerprint only                             */

    uint16_t bits; /* identifying bits on the air, decoded only    */

    /**
     * Expected number of *other* vehicles worldwide answering to this exact
     * combination of serials. Zero means the fingerprint is unique with room
     * to spare, and @c alone is set to say so plainly.
     */
    uint32_t peers;
    bool alone;

    bool persistence; /* at least one sensor came back after a gap    */
    OdExposureLevel level;
} OdExposure;

void od_expose_compute(const OdExposeSensor* sensors, uint8_t count, OdExposure* out);

/** "NONE" / "LOW" / "MODERATE" / "HIGH" / "CRITICAL" */
const char* od_expose_level_name(OdExposureLevel level);

/** One line of plain English for the level. Never longer than 21 columns. */
const char* od_expose_level_hint(OdExposureLevel level);

#ifdef __cplusplus
}
#endif
