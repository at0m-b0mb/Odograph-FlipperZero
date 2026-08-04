/**
 * Odograph - edge stream to chip stream.
 *
 * The CC1101 hands us the demodulator output as a run-length stream: a level
 * and how many microseconds it held. This turns that into the bit-per-chip
 * buffer od_tpms.c wants, decides where one burst ends and the next begins,
 * and runs the protocol scan at the right moments.
 *
 * Pure logic, no Flipper headers - the host test drives it with synthetic
 * runs, including the ragged ones a real radio produces.
 */
#pragma once

#include "od_tpms.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Manchester and differential Manchester both cap a run at two chips. Anything
 * longer is not part of a packet, so it ends the burst.
 */
#define OD_MAX_RUN_CHIPS 4

/**
 * How much of a full buffer we carry into the next one. Every protocol we
 * decode fits in under 224 chips including its preamble, so a packet can never
 * be lost by straddling a buffer boundary.
 */
#define OD_CARRY_CHIPS 256

typedef struct {
    uint8_t chips[OD_MAX_CHIPS / 8];
    uint16_t count;

    uint16_t chip_us;
    uint16_t half_chip_us; /* runs shorter than this are noise, not chips */

    OdTpmsFoundCb callback;
    void* context;

    /* diagnostics, surfaced in the UI so silence is never a black box */
    uint32_t edges; /* level transitions handed to us          */
    uint32_t glitches; /* runs too short to be a chip             */
    uint32_t bursts; /* buffers actually handed to the scanner  */
    uint32_t packets; /* packets that passed an integrity check  */
} OdDemod;

/** @param chip_us OD_CHIP_US_FSK or OD_CHIP_US_OOK */
void od_demod_init(OdDemod* d, uint16_t chip_us, OdTpmsFoundCb callback, void* context);

/** Retune to a different chip period. Drops whatever is half-captured. */
void od_demod_set_chip_us(OdDemod* d, uint16_t chip_us);

/** Drop the buffer without scanning it. Counters survive. */
void od_demod_reset(OdDemod* d);

/** Zero the diagnostic counters too. */
void od_demod_reset_stats(OdDemod* d);

/** Feed one run from the radio. */
void od_demod_feed(OdDemod* d, bool level, uint32_t duration_us);

/** Scan whatever is buffered right now, then keep the tail. */
void od_demod_flush(OdDemod* d);

#ifdef __cplusplus
}
#endif
