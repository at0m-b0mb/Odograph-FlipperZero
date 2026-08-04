/**
 * Odograph - the sweep display.
 *
 * Two faces over the same scan. Until something decodes it is a listening
 * screen: a turning wheel, the band you are on, and an honest readout of what
 * the radio is hearing, so silence never looks like a crash. Once a serial is
 * confirmed it becomes a list of the identities in the air around you.
 */
#pragma once

#include <gui/view.h>
#include "../helpers/od_tpms.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Sensors the list can hold. Matches the store. */
#define SWEEP_MAX_ROWS 24

typedef struct {
    uint32_t id;
    uint8_t id_bits;
    uint8_t proto; /* OdProto */
    int16_t pressure_dkpa;
    bool pressure_valid;
    int8_t rssi;
    bool known; /* in the garage */
    uint16_t hits;
} SweepRow;

typedef struct {
    /* header */
    char band[8]; /* "433.92"          */
    char profile[6]; /* "FSK" / "ASK"     */
    int8_t rssi;
    bool auto_mode;
    uint32_t hop_ms; /* countdown in Auto */

    /* list */
    SweepRow rows[SWEEP_MAX_ROWS];
    uint8_t count;
    uint8_t selected;

    /* the listening screen */
    uint32_t edges;
    uint32_t bursts;
    uint32_t packets;
    uint32_t elapsed_ticks;

    /* footer */
    uint16_t bits;
    uint8_t level; /* OdExposureLevel */

    uint8_t units; /* OdUnits */
    bool show_hint;
} SweepUi;

typedef enum {
    SweepEventOpen, /* OK on a row      */
    SweepEventReport, /* Right: exposure  */
    SweepEventCycleBand, /* Left: next band  */
} SweepViewEvent;

typedef void (*SweepViewEventCallback)(void* context, SweepViewEvent event);

typedef struct SweepView SweepView;

SweepView* sweep_view_alloc(void);
void sweep_view_free(SweepView* v);
View* sweep_view_get_view(SweepView* v);
void sweep_view_set_event_callback(SweepView* v, SweepViewEventCallback cb, void* context);

/** Publish a frame. The argument is copied. */
void sweep_view_update(SweepView* v, const SweepUi* ui);

/** Which row the cursor is on, so the scene can open the right sensor. */
uint8_t sweep_view_selected(SweepView* v);

#ifdef __cplusplus
}
#endif
