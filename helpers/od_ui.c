#include "od_ui.h"

#include <furi.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ formatting */

const char* od_ui_pressure_unit(uint8_t units) {
    switch(units) {
    case OdUnitsPsi:
        return "psi";
    case OdUnitsBar:
        return "bar";
    default:
        return "kPa";
    }
}

/* Pressure is carried as kPa x 10 so every protocol's native resolution
 * survives; the display maths stays in integers from there. */
static void pressure_parts(int16_t dkpa, uint8_t units, int32_t* whole, int32_t* frac) {
    switch(units) {
    case OdUnitsPsi: {
        /* 1 kPa = 0.145038 psi, so psi x 10 = dkPa * 1450 / 10000 */
        int32_t dpsi = ((int32_t)dkpa * 1450 + 5000) / 10000;
        *whole = dpsi / 10;
        *frac = dpsi % 10;
        break;
    }
    case OdUnitsBar: {
        /* 1 bar = 100 kPa, shown to two places */
        int32_t cbar = ((int32_t)dkpa + 5) / 10; /* bar x 100 */
        *whole = cbar / 100;
        *frac = cbar % 100;
        break;
    }
    default:
        *whole = dkpa / 10;
        *frac = dkpa % 10;
        break;
    }
}

void od_ui_pressure(char* out, size_t size, int16_t dkpa, bool valid, uint8_t units) {
    if(!valid) {
        snprintf(out, size, "--");
        return;
    }
    int32_t whole, frac;
    pressure_parts(dkpa, units, &whole, &frac);
    if(units == OdUnitsBar) {
        snprintf(out, size, "%ld.%02ld %s", (long)whole, (long)frac, od_ui_pressure_unit(units));
    } else {
        snprintf(out, size, "%ld.%ld %s", (long)whole, (long)frac, od_ui_pressure_unit(units));
    }
}

void od_ui_pressure_short(char* out, size_t size, int16_t dkpa, bool valid, uint8_t units) {
    if(!valid) {
        snprintf(out, size, "--");
        return;
    }
    int32_t whole, frac;
    pressure_parts(dkpa, units, &whole, &frac);
    if(units == OdUnitsKpa) {
        snprintf(out, size, "%ld", (long)whole); /* kPa needs no decimal in a row */
    } else if(units == OdUnitsBar) {
        snprintf(out, size, "%ld.%02ld", (long)whole, (long)frac);
    } else {
        snprintf(out, size, "%ld.%ld", (long)whole, (long)frac);
    }
}

void od_ui_temp(char* out, size_t size, int16_t celsius, bool valid, bool fahrenheit) {
    if(!valid) {
        snprintf(out, size, "--");
        return;
    }
    if(fahrenheit) {
        int32_t f = ((int32_t)celsius * 9) / 5 + 32;
        snprintf(out, size, "%ld F", (long)f);
    } else {
        snprintf(out, size, "%d C", celsius);
    }
}

void od_ui_id(char* out, size_t size, uint32_t id, uint8_t id_bits) {
    uint8_t digits = (uint8_t)((id_bits + 3) / 4);
    if(digits == 0) digits = 8;
    if(digits > 8) digits = 8;
    snprintf(out, size, "%0*lX", (int)digits, (unsigned long)id);
}

const char* od_ui_proto_tag(OdProto proto) {
    switch(proto) {
    case OdProtoFord:
        return "FRD";
    case OdProtoRenault:
        return "REN";
    case OdProtoCitroen:
        return "CIT";
    case OdProtoHyundaiVdo:
        return "HYU";
    case OdProtoToyota:
        return "TOY";
    case OdProtoSchrader:
        return "SCH";
    default:
        return "???";
    }
}

void od_ui_elapsed(char* out, size_t size, uint32_t ticks) {
    uint32_t hz = furi_kernel_get_tick_frequency();
    if(hz == 0) hz = 1000;
    uint32_t sec = ticks / hz;

    if(sec < 60) {
        snprintf(out, size, "%lus", (unsigned long)sec);
    } else if(sec < 3600) {
        snprintf(out, size, "%lum %02lus", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    } else {
        snprintf(
            out, size, "%luh %02lum", (unsigned long)(sec / 3600), (unsigned long)((sec / 60) % 60));
    }
}

/* --------------------------------------------------- seven-segment digits */

/**
 *      aaa
 *     f   b
 *     f   b
 *      ggg
 *     e   c
 *     e   c
 *      ddd
 *
 * Bit order below is a,b,c,d,e,f,g from bit 0 up. Every hex character has a
 * conventional seven-segment shape - 0-9 plus A, b, C, d, E, F - which is
 * what makes a raw sensor serial legible as a plate rather than as a dump.
 */
#define SEG_A (1u << 0)
#define SEG_B (1u << 1)
#define SEG_C (1u << 2)
#define SEG_D (1u << 3)
#define SEG_E (1u << 4)
#define SEG_F (1u << 5)
#define SEG_G (1u << 6)

static const uint8_t seg_hex[16] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, /* 0 */
    SEG_B | SEG_C, /* 1 */
    SEG_A | SEG_B | SEG_D | SEG_E | SEG_G, /* 2 */
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_G, /* 3 */
    SEG_B | SEG_C | SEG_F | SEG_G, /* 4 */
    SEG_A | SEG_C | SEG_D | SEG_F | SEG_G, /* 5 */
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, /* 6 */
    SEG_A | SEG_B | SEG_C, /* 7 */
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, /* 8 */
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G, /* 9 */
    SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G, /* A */
    SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, /* b */
    SEG_A | SEG_D | SEG_E | SEG_F, /* C */
    SEG_B | SEG_C | SEG_D | SEG_E | SEG_G, /* d */
    SEG_A | SEG_D | SEG_E | SEG_F | SEG_G, /* E */
    SEG_A | SEG_E | SEG_F | SEG_G, /* F */
};

#define SEG_W     9 /* digit cell width           */
#define SEG_H     OD_SEG_HEIGHT /* digit cell height          */
#define SEG_T     2 /* segment thickness          */
#define SEG_GAP   2 /* space between digits       */
#define SEG_INSET 2 /* how far the bars are inset */

uint8_t od_ui_seg_width(uint8_t digits) {
    if(digits == 0) return 0;
    return (uint8_t)(digits * SEG_W + (digits - 1) * SEG_GAP);
}

static void seg_draw_digit(Canvas* canvas, int32_t x, int32_t y, uint8_t mask) {
    const int32_t mid = y + (SEG_H - SEG_T) / 2;
    const int32_t bar_w = SEG_W - 2 * SEG_INSET;
    const int32_t arm_h = mid - (y + SEG_INSET);

    if(mask & SEG_A) canvas_draw_box(canvas, x + SEG_INSET, y, bar_w, SEG_T);
    if(mask & SEG_G) canvas_draw_box(canvas, x + SEG_INSET, mid, bar_w, SEG_T);
    if(mask & SEG_D) canvas_draw_box(canvas, x + SEG_INSET, y + SEG_H - SEG_T, bar_w, SEG_T);

    if(mask & SEG_F) canvas_draw_box(canvas, x, y + SEG_INSET, SEG_T, arm_h);
    if(mask & SEG_B) canvas_draw_box(canvas, x + SEG_W - SEG_T, y + SEG_INSET, SEG_T, arm_h);
    if(mask & SEG_E) canvas_draw_box(canvas, x, mid, SEG_T, arm_h);
    if(mask & SEG_C) canvas_draw_box(canvas, x + SEG_W - SEG_T, mid, SEG_T, arm_h);
}

void od_ui_draw_seg(Canvas* canvas, int32_t x, int32_t y, const char* hex) {
    for(const char* p = hex; *p; p++) {
        uint8_t mask = 0;
        if(*p >= '0' && *p <= '9') {
            mask = seg_hex[*p - '0'];
        } else if(*p >= 'A' && *p <= 'F') {
            mask = seg_hex[10 + (*p - 'A')];
        } else if(*p >= 'a' && *p <= 'f') {
            mask = seg_hex[10 + (*p - 'a')];
        } else {
            mask = SEG_G; /* anything else shows as a dash */
        }
        seg_draw_digit(canvas, x, y, mask);
        x += SEG_W + SEG_GAP;
    }
}

void od_ui_draw_plate(Canvas* canvas, int32_t cx, int32_t y, const char* hex) {
    uint8_t digits = (uint8_t)strlen(hex);
    int32_t w = od_ui_seg_width(digits);
    int32_t x = cx - w / 2;

    /* the plate itself: a bolted frame with a hairline inside it */
    int32_t fx = x - 7;
    int32_t fy = y - 5;
    int32_t fw = w + 14;
    int32_t fh = SEG_H + 10;
    canvas_draw_rframe(canvas, fx, fy, fw, fh, 3);
    canvas_draw_dot(canvas, fx + 3, fy + 3);
    canvas_draw_dot(canvas, fx + fw - 4, fy + 3);
    canvas_draw_dot(canvas, fx + 3, fy + fh - 4);
    canvas_draw_dot(canvas, fx + fw - 4, fy + fh - 4);

    od_ui_draw_seg(canvas, x, y, hex);
}

/* --------------------------------------------------------------- widgets */

void od_ui_draw_signal(Canvas* canvas, int32_t x, int32_t y, int8_t rssi) {
    /* -100 dBm is the floor of anything useful, -50 is a car beside you. */
    int32_t level = ((int32_t)rssi + 100) / 13; /* 0..~4 */
    if(level < 0) level = 0;
    if(level > 4) level = 4;

    for(int32_t i = 0; i < 4; i++) {
        int32_t h = 3 + i * 2;
        int32_t bx = x + i * 3;
        int32_t by = y + 9 - h;
        if(i < level) {
            canvas_draw_box(canvas, bx, by, 2, h);
        } else {
            canvas_draw_dot(canvas, bx, y + 8);
        }
    }
}

void od_ui_draw_wheel(Canvas* canvas, int32_t cx, int32_t cy, uint32_t frame) {
    /* tyre */
    canvas_draw_circle(canvas, cx, cy, 12);
    canvas_draw_circle(canvas, cx, cy, 11);
    canvas_draw_circle(canvas, cx, cy, 5);

    /* four spokes, rotating a quarter turn every other frame so the wheel
     * reads as turning - which is when a tyre sensor actually talks */
    bool tilt = (frame / 2) & 1;
    if(tilt) {
        canvas_draw_line(canvas, cx - 8, cy - 8, cx - 4, cy - 4);
        canvas_draw_line(canvas, cx + 8, cy + 8, cx + 4, cy + 4);
        canvas_draw_line(canvas, cx - 8, cy + 8, cx - 4, cy + 4);
        canvas_draw_line(canvas, cx + 8, cy - 8, cx + 4, cy - 4);
    } else {
        canvas_draw_line(canvas, cx, cy - 10, cx, cy - 6);
        canvas_draw_line(canvas, cx, cy + 10, cx, cy + 6);
        canvas_draw_line(canvas, cx - 10, cy, cx - 6, cy);
        canvas_draw_line(canvas, cx + 10, cy, cx + 6, cy);
    }

    /* the valve stem, and the sensor bolted to it */
    canvas_draw_line(canvas, cx + 8, cy - 9, cx + 11, cy - 12);
    canvas_draw_box(canvas, cx + 11, cy - 14, 3, 3);

    /* radiating, one chevron lighting up per frame */
    uint32_t wave = frame % 4;
    for(uint32_t i = 0; i < 3; i++) {
        if(i >= wave) continue;
        int32_t r = 4 + (int32_t)i * 4;
        int32_t ox = cx + 14;
        int32_t oy = cy - 13;
        canvas_draw_line(canvas, ox + r - 2, oy - r, ox + r, oy - r + 2);
        canvas_draw_line(canvas, ox + r, oy - r + 2, ox + r, oy + r - 2);
        canvas_draw_line(canvas, ox + r, oy + r - 2, ox + r - 2, oy + r);
    }
}

void od_ui_draw_meter(Canvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t pct) {
    if(pct > 100) pct = 100;
    canvas_draw_frame(canvas, x, y, w, h);
    int32_t fill = ((w - 4) * pct) / 100;
    if(fill > 0) canvas_draw_box(canvas, x + 2, y + 2, fill, h - 4);
}

void od_ui_draw_dots(Canvas* canvas, int32_t cx, int32_t y, uint8_t count, uint8_t active) {
    if(count == 0) return;
    const int32_t pitch = 6;
    int32_t x = cx - (count * pitch) / 2 + pitch / 2;
    for(uint8_t i = 0; i < count; i++) {
        if(i == active) {
            canvas_draw_disc(canvas, x, y, 2);
        } else {
            canvas_draw_circle(canvas, x, y, 1);
        }
        x += pitch;
    }
}

void od_ui_draw_badge(Canvas* canvas, int32_t cx, int32_t y, const char* text) {
    canvas_set_font(canvas, FontPrimary);
    int32_t tw = canvas_string_width(canvas, text);
    int32_t w = tw + 14;
    int32_t h = 17;
    int32_t x = cx - w / 2;

    canvas_draw_rbox(canvas, x, y, w, h, 3);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str_aligned(canvas, cx, y + h / 2, AlignCenter, AlignCenter, text);
    canvas_set_color(canvas, ColorBlack);
}
