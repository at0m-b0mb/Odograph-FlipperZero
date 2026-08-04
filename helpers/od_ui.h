/**
 * Odograph - shared drawing and formatting.
 *
 * The pieces more than one view needs: unit conversion, the seven-segment
 * serial renderer that turns a sensor ID into something that reads like a
 * number plate, and the small glyphs the sweep screen animates.
 */
#pragma once

#include "od_tpms.h"
#include "od_settings.h"

#include <gui/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ formatting */

/** "182.7 kPa" / "26.5 psi" / "1.83 bar". Writes "--" when not measured. */
void od_ui_pressure(char* out, size_t size, int16_t dkpa, bool valid, uint8_t units);

/** The same value with no unit, for tight rows: "182" / "26.5" / "1.83". */
void od_ui_pressure_short(char* out, size_t size, int16_t dkpa, bool valid, uint8_t units);

/** "kPa" / "psi" / "bar" */
const char* od_ui_pressure_unit(uint8_t units);

/** "23 C" / "73 F", or "--" when the sensor did not report one. */
void od_ui_temp(char* out, size_t size, int16_t celsius, bool valid, bool fahrenheit);

/** The serial as hex, exactly as wide as the protocol's ID really is. */
void od_ui_id(char* out, size_t size, uint32_t id, uint8_t id_bits);

/** Three columns of protocol: "FRD", "REN", "TOY"... "???" for unknown. */
const char* od_ui_proto_tag(OdProto proto);

/** "4s" / "2m 10s" / "1h 04m", from a tick delta. */
void od_ui_elapsed(char* out, size_t size, uint32_t ticks);

/* --------------------------------------------------------------- drawing */

/** Width in pixels of a seven-segment serial of @p digits characters. */
uint8_t od_ui_seg_width(uint8_t digits);

#define OD_SEG_HEIGHT 15

/**
 * Draw a hex string as seven-segment digits with @p x, @p y at its top-left.
 * Every hex character has a standard seven-segment form, which is exactly why
 * a tyre sensor's serial reads so naturally as a licence plate.
 */
void od_ui_draw_seg(Canvas* canvas, int32_t x, int32_t y, const char* hex);

/** The seven-segment serial inside a bolted plate frame, centred on @p cx. */
void od_ui_draw_plate(Canvas* canvas, int32_t cx, int32_t y, const char* hex);

/** Four signal bars, 11x9, top-left at @p x, @p y. */
void od_ui_draw_signal(Canvas* canvas, int32_t x, int32_t y, int8_t rssi);

/**
 * A wheel with a valve-stem sensor, radiating. @p frame animates the waves;
 * pass a monotonically rising counter.
 */
void od_ui_draw_wheel(Canvas* canvas, int32_t cx, int32_t cy, uint32_t frame);

/** A horizontal meter: filled fraction of @p w, with a frame. */
void od_ui_draw_meter(Canvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t pct);

/** Page dots, @p count of them, @p active highlighted, centred on @p cx. */
void od_ui_draw_dots(Canvas* canvas, int32_t cx, int32_t y, uint8_t count, uint8_t active);

/** A word in a filled box, the way the exposure verdict is stamped. */
void od_ui_draw_badge(Canvas* canvas, int32_t cx, int32_t y, const char* text);

#ifdef __cplusplus
}
#endif
