#include "sensor_view.h"
#include "../helpers/od_ui.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

/* ---------------- layout ---------------- */
#define SV_W 128
#define SV_H 64

#define SV_HDR_BASE 9
#define SV_RULE_Y   11

/* Every page owns y 13..SV_CONTENT_BOTTOM and nothing below it; the page dots
 * get the last row to themselves. Keeping that budget in one place is what
 * stops a page growing a line and quietly landing on the dots. */
#define SV_CONTENT_BOTTOM 57
#define SV_DOTS_Y         61

#define SV_PLATE_Y 18 /* top of the seven-segment digits */

/* Pressure meter scale. Car tyres live around 200-250 kPa; 350 is the top of
 * what these sensors are specified to report, so the bar never pins. */
#define SV_PRESSURE_FULL_DKPA 3500

struct SensorView {
    View* view;
    SensorViewEventCallback cb;
    void* ctx;
};

typedef struct {
    SensorUi ui;
    uint8_t page;
} SensorModel;

static void emit(SensorView* v, SensorViewEvent event) {
    if(v->cb) v->cb(v->ctx, event);
}

static const char* const page_titles[SensorPageCount] = {
    "SERIAL",
    "READING",
    "TRACK",
    "RAW",
};

/* ---------------- pages ---------------- */

static void draw_plate_page(Canvas* canvas, const SensorUi* ui) {
    const OdSensor* s = &ui->sensor;

    char id[10];
    od_ui_id(id, sizeof(id), s->id, s->id_bits);
    od_ui_draw_plate(canvas, SV_W / 2, SV_PLATE_Y, id);

    canvas_set_font(canvas, FontSecondary);

    char line[40];
    if(s->proto == OdProtoUnknown) {
        snprintf(line, sizeof(line), "Unrecognised - fingerprint");
    } else {
        snprintf(
            line, sizeof(line), "%s  %u-bit serial", od_tpms_proto_name(s->proto), s->id_bits);
    }
    canvas_draw_str_aligned(canvas, SV_W / 2, 48, AlignCenter, AlignBottom, line);

    const char* note;
    if(ui->flash_full) {
        note = "Garage is full";
    } else if(ui->flash_saved) {
        note = "Added to garage";
    } else if(ui->flash_forgot) {
        note = "Removed";
    } else if(s->known) {
        note = "In your garage";
    } else if(s->proto == OdProtoUnknown) {
        note = "Pattern, not a decode";
    } else {
        /* The point of the whole screen, said once, plainly. */
        note = "This never changes";
    }
    canvas_draw_str_aligned(canvas, SV_W / 2, SV_CONTENT_BOTTOM, AlignCenter, AlignBottom, note);
}

static void draw_field(Canvas* canvas, int32_t y, const char* label, const char* value) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, y, label);
    canvas_draw_str_aligned(canvas, SV_W - 3, y, AlignRight, AlignBottom, value);
}

static void draw_reading_page(Canvas* canvas, const SensorUi* ui) {
    const OdSensor* s = &ui->sensor;
    char value[24];

    od_ui_pressure(value, sizeof(value), s->pressure_dkpa, s->pressure_valid, ui->units);
    draw_field(canvas, 22, "Pressure", value);

    uint8_t pct = 0;
    if(s->pressure_valid && s->pressure_dkpa > 0) {
        int32_t p = ((int32_t)s->pressure_dkpa * 100) / SV_PRESSURE_FULL_DKPA;
        pct = (uint8_t)(p > 100 ? 100 : p);
    }
    od_ui_draw_meter(canvas, 3, 25, SV_W - 6, 7, pct);

    od_ui_temp(value, sizeof(value), s->temp_c, s->temp_valid, ui->temp_f);
    draw_field(canvas, 42, "Temperature", value);

    if(s->moving_valid) {
        draw_field(canvas, 51, "Wheel", s->moving ? "turning" : "at rest");
    } else {
        draw_field(canvas, 51, "Fitted to", od_tpms_proto_makes(s->proto));
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, SV_CONTENT_BOTTOM, "All of it in the clear");
}

static void draw_track_page(Canvas* canvas, const SensorUi* ui) {
    const OdSensor* s = &ui->sensor;
    char value[24];
    char ago[14];

    od_ui_elapsed(ago, sizeof(ago), ui->now_tick - s->first_tick);
    snprintf(value, sizeof(value), "%s ago", ago);
    draw_field(canvas, 20, "First heard", value);

    od_ui_elapsed(ago, sizeof(ago), ui->now_tick - s->last_tick);
    snprintf(value, sizeof(value), "%s ago", ago);
    draw_field(canvas, 29, "Last heard", value);

    snprintf(value, sizeof(value), "%u", s->hits);
    draw_field(canvas, 38, "Packets", value);

    snprintf(value, sizeof(value), "%d / %d dBm", s->rssi, s->rssi_best);
    draw_field(canvas, 47, "Signal", value);

    /*
     * The sightings strip. Each column is one packet, its height the signal
     * at that moment - a car approaching and leaving draws itself.
     */
    const int32_t strip_y = 50;
    const int32_t strip_h = 7;
    canvas_draw_line(canvas, 0, strip_y + strip_h, SV_W - 1, strip_y + strip_h);
    for(uint8_t i = 0; i < s->track_count && i < OD_TRACK_POINTS; i++) {
        int32_t dbm = (int32_t)s->track[i] - 128;
        int32_t h = ((dbm + 110) * strip_h) / 60;
        if(h < 1) h = 1;
        if(h > strip_h) h = strip_h;
        int32_t x = 2 + i * 4;
        if(x >= SV_W - 2) break;
        canvas_draw_box(canvas, x, strip_y + strip_h - h, 3, h);
    }

    if(s->returns > 0) {
        /* Coming back is the whole argument. Stamp it over the strip. */
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 62, strip_y, 66, strip_h);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        snprintf(value, sizeof(value), "SEEN AGAIN x%u", s->returns);
        canvas_draw_str_aligned(
            canvas, 125, strip_y + strip_h - 2, AlignRight, AlignBottom, value);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_raw_page(Canvas* canvas, const SensorUi* ui) {
    const OdSensor* s = &ui->sensor;
    char line[40];

    canvas_set_font(canvas, FontSecondary);

    snprintf(
        line,
        sizeof(line),
        "%lu.%02lu MHz",
        (unsigned long)(s->frequency / 1000000u),
        (unsigned long)((s->frequency % 1000000u) / 10000u));
    canvas_draw_str(canvas, 3, 21, line);
    canvas_draw_str_aligned(
        canvas, SV_W - 3, 21, AlignRight, AlignBottom, od_tpms_proto_name(s->proto));

    /* The payload, five bytes a line, in the order it came off the air. */
    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t row = 0; row < 2; row++) {
        char hex[32];
        size_t at = 0;
        hex[0] = '\0';
        for(uint8_t i = 0; i < 5; i++) {
            uint8_t index = (uint8_t)(row * 5 + i);
            if(index >= s->raw_len) break;
            at += (size_t)snprintf(hex + at, sizeof(hex) - at, "%02X ", s->raw[index]);
        }
        if(hex[0]) canvas_draw_str(canvas, 3, 33 + row * 11, hex);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(
        canvas,
        3,
        SV_CONTENT_BOTTOM,
        s->proto == OdProtoUnknown ? "No checksum to verify" : "Checksum verified");
}

/* ---------------- draw ---------------- */

static void sensor_view_draw(Canvas* canvas, void* model) {
    SensorModel* m = (SensorModel*)model;
    const SensorUi* ui = &m->ui;

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, SV_HDR_BASE, page_titles[m->page]);

    canvas_set_font(canvas, FontSecondary);
    if(ui->sensor.known) {
        canvas_draw_str_aligned(canvas, SV_W - 2, SV_HDR_BASE, AlignRight, AlignBottom, "MINE");
    } else {
        canvas_draw_str_aligned(
            canvas, SV_W - 2, SV_HDR_BASE, AlignRight, AlignBottom, od_ui_proto_tag(ui->sensor.proto));
    }
    canvas_draw_line(canvas, 0, SV_RULE_Y, SV_W - 1, SV_RULE_Y);

    switch(m->page) {
    case SensorPageReading:
        draw_reading_page(canvas, ui);
        break;
    case SensorPageTrack:
        draw_track_page(canvas, ui);
        break;
    case SensorPageRaw:
        draw_raw_page(canvas, ui);
        break;
    default:
        draw_plate_page(canvas, ui);
        break;
    }

    /* The legend and the dots share the last row, so only one of them is up
     * at a time. The legend is transient; the dots are the resting state. */
    if(ui->show_hint) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, SV_H - 1, "< > page");
        canvas_draw_str_aligned(canvas, SV_W - 2, SV_H - 1, AlignRight, AlignBottom, "OK save");
    } else {
        od_ui_draw_dots(canvas, SV_W / 2, SV_DOTS_Y, SensorPageCount, m->page);
    }
}

/* ---------------- input ---------------- */

static bool sensor_view_input(InputEvent* event, void* context) {
    SensorView* v = (SensorView*)context;
    furi_assert(v);

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyLeft:
        with_view_model(
            v->view,
            SensorModel * m,
            { m->page = (uint8_t)((m->page + SensorPageCount - 1) % SensorPageCount); },
            true);
        return true;

    case InputKeyRight:
        with_view_model(
            v->view,
            SensorModel * m,
            { m->page = (uint8_t)((m->page + 1) % SensorPageCount); },
            true);
        return true;

    case InputKeyOk:
        emit(v, SensorEventToggleGarage);
        return true;

    default:
        return false;
    }
}

/* ---------------- lifecycle ---------------- */

SensorView* sensor_view_alloc(void) {
    SensorView* v = malloc(sizeof(SensorView));
    v->cb = NULL;
    v->ctx = NULL;
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, sensor_view_draw);
    view_set_input_callback(v->view, sensor_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(SensorModel));
    with_view_model(v->view, SensorModel * m, { memset(m, 0, sizeof(SensorModel)); }, false);
    return v;
}

void sensor_view_free(SensorView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* sensor_view_get_view(SensorView* v) {
    furi_assert(v);
    return v->view;
}

void sensor_view_set_event_callback(SensorView* v, SensorViewEventCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->ctx = context;
}

void sensor_view_update(SensorView* v, const SensorUi* ui) {
    furi_assert(v);
    with_view_model(v->view, SensorModel * m, { m->ui = *ui; }, true);
}

void sensor_view_reset(SensorView* v) {
    furi_assert(v);
    with_view_model(v->view, SensorModel * m, { m->page = SensorPagePlate; }, true);
}
