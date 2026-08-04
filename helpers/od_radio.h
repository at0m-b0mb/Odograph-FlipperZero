/**
 * Odograph - the listening half.
 *
 * Brings the internal CC1101 up in asynchronous receive, feeds the raw edge
 * stream through the demodulator, and files whatever decodes into the store.
 * Strictly listen-only: Odograph has no transmit path at all.
 */
#pragma once

#include "od_store.h"

#include <furi.h>
#include <gui/view_dispatcher.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The frequencies tyre sensors actually use. */
typedef struct {
    uint32_t frequency;
    const char* label; /* "433.92"          */
    const char* region; /* "EU / world"      */
} OdBand;

#define OD_BAND_COUNT 3
extern const OdBand od_bands[OD_BAND_COUNT];

/** 433.92 MHz: everywhere outside North America and Japan. */
#define OD_BAND_DEFAULT 1

/**
 * Receive profiles. The two FSK profiles hear the same sensors; Standard uses
 * the firmware's stock wideband FSK preset, which is the one every Flipper
 * captures raw FSK with, while Tuned narrows the channel filter and matches
 * the data rate to 19.2 kchip/s for a quieter, more sensitive listen. If one
 * will not decode your car, try the other.
 */
typedef enum {
    OdProfileFsk = 0, /* stock FM476, 52 us chips  */
    OdProfileFskTuned, /* TPMS-tuned, 52 us chips   */
    OdProfileAsk, /* OOK 270 kHz, 120 us chips */
    OdProfileAuto, /* rotate band and profile   */
    OdProfileCount,
} OdProfile;

extern const char* const od_profile_names[OdProfileCount];

/** How long Auto sits on one band/profile pair before moving on, in ms. */
#define OD_HOP_DWELL_MS 4000

typedef struct OdRadio OdRadio;

/**
 * @param view_dispatcher where to post from the worker thread
 * @param confirm_event   custom event id posted when a serial is confirmed
 */
OdRadio* od_radio_alloc(ViewDispatcher* view_dispatcher, uint32_t confirm_event, OdStore* store);
void od_radio_free(OdRadio* radio);

/** Retune. Safe while running - the change lands on the next worker pass. */
void od_radio_configure(OdRadio* radio, uint8_t band_index, uint8_t profile, uint8_t min_hits);

void od_radio_start(OdRadio* radio);
void od_radio_stop(OdRadio* radio);
bool od_radio_is_running(OdRadio* radio);

/** Carrier strength in dBm, sampled by the worker. */
float od_radio_rssi(OdRadio* radio);

/**
 * Level transitions the demodulator has produced since the scan started.
 * A rising count with no decodes means the radio is hearing something that
 * is not a tyre sensor - which is how you tell "wrong band" from "no cars".
 */
uint32_t od_radio_edges(OdRadio* radio);

/** Bursts handed to the protocol scanner. */
uint32_t od_radio_bursts(OdRadio* radio);

/** Which band and profile the radio is on right now (Auto moves these). */
uint8_t od_radio_band(OdRadio* radio);
uint8_t od_radio_profile(OdRadio* radio);

/** Milliseconds until Auto moves on, or 0 when the radio is pinned. */
uint32_t od_radio_hop_remaining(OdRadio* radio);

#ifdef __cplusplus
}
#endif
