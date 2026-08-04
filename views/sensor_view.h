/**
 * Odograph - one sensor, four ways.
 *
 * PLATE     the serial, rendered as the number plate it effectively is
 * READING   what else the packet gave away: pressure, temperature, motion
 * TRACK     the part that matters - when it was heard, and how often it came back
 * RAW       the decoded bytes, so nothing here has to be taken on trust
 */
#pragma once

#include <gui/view.h>
#include "../helpers/od_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SensorPagePlate = 0,
    SensorPageReading,
    SensorPageTrack,
    SensorPageRaw,
    SensorPageCount,
} SensorPage;

typedef struct {
    OdSensor sensor;
    uint8_t units; /* OdUnits */
    bool temp_f;
    uint32_t now_tick;
    bool show_hint;
    /** Flashes over the plate right after the garage is toggled. */
    bool flash_saved;
    bool flash_forgot;
    bool flash_full;
} SensorUi;

typedef enum {
    SensorEventToggleGarage, /* OK */
} SensorViewEvent;

typedef void (*SensorViewEventCallback)(void* context, SensorViewEvent event);

typedef struct SensorView SensorView;

SensorView* sensor_view_alloc(void);
void sensor_view_free(SensorView* v);
View* sensor_view_get_view(SensorView* v);
void sensor_view_set_event_callback(SensorView* v, SensorViewEventCallback cb, void* context);

/** Publish a frame. The argument is copied. */
void sensor_view_update(SensorView* v, const SensorUi* ui);

/** Reset to the first page - call when the screen is opened. */
void sensor_view_reset(SensorView* v);

#ifdef __cplusplus
}
#endif
