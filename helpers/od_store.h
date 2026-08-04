/**
 * Odograph - the sensor table.
 *
 * Every decoded packet lands here and is folded into a per-serial record. The
 * table is what makes the point: a tyre sensor is not an event, it is an
 * identity that keeps coming back, and this is where "keeps coming back" is
 * actually measured.
 *
 * Written from the radio worker thread, read from the GUI thread, so every
 * entry point takes the lock.
 */
#pragma once

#include "od_tpms.h"

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Distinct serials we track in one session. Four wheels a car, so this is
 * six vehicles' worth - more than a car park's worth of drive-bys. */
#define OD_MAX_SENSORS 24

/** Vehicles saved to the SD card and recognised across reboots. */
#define OD_GARAGE_MAX 8

/**
 * A silence longer than this makes the next sighting a separate visit. Coming
 * back after a real gap is the difference between "I heard a car" and "I can
 * follow this car", so it gets counted on its own.
 */
#define OD_RETURN_GAP_MS (3u * 60u * 1000u)

/** Sightings kept per sensor for the timeline strip. */
#define OD_TRACK_POINTS 32

typedef struct {
    uint32_t id;
    OdProto proto;
    uint8_t id_bits;

    int16_t pressure_dkpa;
    bool pressure_valid;
    int16_t temp_c;
    bool temp_valid;
    bool moving;
    bool moving_valid;

    uint16_t hits; /* packets that passed an integrity check   */
    uint8_t returns; /* separate visits after OD_RETURN_GAP_MS   */
    uint32_t first_tick; /* first heard this session                 */
    uint32_t last_tick;

    int8_t rssi; /* most recent                              */
    int8_t rssi_best;
    uint32_t frequency;

    bool known; /* saved in the garage                      */
    uint32_t known_since; /* unix timestamp of the first ever sighting */

    /* one sample per sighting, most recent last, for the timeline */
    uint8_t track[OD_TRACK_POINTS];
    uint8_t track_count;

    uint8_t raw[10];
    uint8_t raw_len;
} OdSensor;

typedef enum {
    OdStoreIgnored = 0, /* table full                                    */
    OdStoreUpdated, /* known serial, folded in                       */
    OdStoreConfirmed, /* just crossed the confirmation threshold       */
} OdStoreResult;

typedef struct OdStore OdStore;

OdStore* od_store_alloc(void);
void od_store_free(OdStore* store);

/** Forget every sighting. The garage on the SD card is untouched. */
void od_store_clear(OdStore* store);

/**
 * Fold one decoded packet in.
 *
 * @param min_hits how many decodes a serial needs before it counts as real;
 *                 the return value reports the crossing exactly once
 */
OdStoreResult od_store_add(
    OdStore* store,
    const OdTpmsReading* reading,
    int8_t rssi,
    uint32_t frequency,
    uint8_t min_hits);

/** Serials seen at all, confirmed or not. */
uint8_t od_store_count(OdStore* store);

/** Serials that have met the confirmation threshold. */
uint8_t od_store_confirmed_count(OdStore* store, uint8_t min_hits);

/** Total packets that passed an integrity check this session. */
uint32_t od_store_packets(OdStore* store);

/** Copy out one confirmed sensor by its position in the confirmed list. */
bool od_store_get_confirmed(OdStore* store, uint8_t index, uint8_t min_hits, OdSensor* out);

/** Copy out one sensor by its raw table index. */
bool od_store_get(OdStore* store, uint8_t index, OdSensor* out);

/* ------------------------------------------------------------- garage ---
 * Sensors you have chosen to remember. Saved to the SD card, matched on every
 * later run - which is how Odograph can tell you, days later, that your car
 * just announced itself again.
 */

typedef struct {
    uint32_t id;
    OdProto proto;
    uint32_t first_seen; /* unix timestamp */
    uint32_t last_seen;
    char label[14];
} OdGarageEntry;

/** Load the garage from the SD card. Call once at start-up. */
void od_store_garage_load(OdStore* store);

/** How many vehicles are remembered. */
uint8_t od_store_garage_count(OdStore* store);

bool od_store_garage_get(OdStore* store, uint8_t index, OdGarageEntry* out);

/** Remember this serial, or forget it if it is already remembered.
 * @return true if the sensor is remembered afterwards */
bool od_store_garage_toggle(OdStore* store, uint32_t id, OdProto proto, const char* label);

/** Forget every remembered vehicle, on the card too. */
void od_store_garage_clear(OdStore* store);

#ifdef __cplusplus
}
#endif
