/**
 * Odograph - settings that survive a reboot.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pressure readout. */
typedef enum {
    OdUnitsKpa = 0,
    OdUnitsPsi,
    OdUnitsBar,
    OdUnitsCount,
} OdUnits;

/** How many decodes of the same serial before Odograph will name a sensor.
 * A tyre sensor repeats its packet several times per burst, so two is nearly
 * free for a real sensor and ruinous for a checksum collision. */
typedef enum {
    OdConfirmOne = 0,
    OdConfirmTwo,
    OdConfirmThree,
    OdConfirmCount,
} OdConfirm;

typedef struct {
    uint8_t band_index; /* index into od_bands            */
    uint8_t profile; /* OdProfile, incl. Auto          */
    uint8_t units; /* OdUnits                        */
    bool temp_f; /* Fahrenheit instead of Celsius  */
    uint8_t confirm; /* OdConfirm                      */
    bool sound;
    bool led;
    bool logging; /* append sightings to the SD log */
} OdSettings;

void od_settings_default(OdSettings* s);
bool od_settings_load(OdSettings* s);
bool od_settings_save(const OdSettings* s);

/** Confirm setting -> the actual hit count required. */
uint8_t od_settings_min_hits(const OdSettings* s);

#ifdef __cplusplus
}
#endif
