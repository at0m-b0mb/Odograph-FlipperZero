#include "sweep_view.h"
#include "../helpers/od_ui.h"
#include "../helpers/od_expose.h"
#include "../helpers/od_radio.h" /* OD_HOP_DWELL_MS, for the Auto dwell bar */

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

/* ---------------- layout ----------------
 * One set of constants for the C and for tools_gen_mockups.py, which mirrors
 * this file line for line so a collision shows up in the README before it
 * ever ships on a device.
 */
#define SW_W 128
#define SW_H 64

#define SW_HDR_BASE  9 /* header text baseline            */
#define SW_RULE_Y    11 /* hairline under the header       */
#define SW_TOP       13 /* first content row               */

#define SW_ROW_H     13
#define SW_ROWS      3 /* rows on screen at once          */
#define SW_TEXT_DY   9 /* baseline inside a row           */

#define SW_COL_MARK  2 /* garage star / bullet            */
#define SW_COL_ID    9
#define SW_COL_TAG   52
#define SW_COL_PRESS 70
#define SW_COL_BARS  108 /* 11 px wide, clears the scrollbar */

#define SW_STRIP_Y    52 /* inverted status strip           */
#define SW_STRIP_H    12
#define SW_STRIP_BASE 61

/* listening screen */
#define SW_WHEEL_CX 26
#define SW_WHEEL_CY 33
#define SW_INFO_X   52

struct SweepView {
    View* view;
    SweepViewEventCallback cb;
    void* ctx;
};

typedef struct {
    SweepUi ui;
    uint32_t frame; /* advances on every published update, drives animation */
} SweepModel;

static void emit(SweepView* v, SweepViewEvent event) {
    if(v->cb) v->cb(v->ctx, event);
}

/* ---------------- header ---------------- */

static void draw_header(Canvas* canvas, const SweepUi* ui) {
    canvas_set_font(canvas, FontSecondary);

    char left[20];
    snprintf(left, sizeof(left), "%s %s", ui->band, ui->profile);
    canvas_draw_str(canvas, 2, SW_HDR_BASE, left);

    /* In Auto the dwell bar drains left to right, so you can see the radio is
     * about to move rather than wondering why it stopped hearing things. */
    if(ui->auto_mode) {
        int32_t x = 2 + canvas_string_width(canvas, left) + 4;
        int32_t w = 18;
        int32_t fill = (int32_t)((ui->hop_ms * (uint32_t)(w - 2)) / OD_HOP_DWELL_MS);
        if(fill > w - 2) fill = w - 2;
        canvas_draw_frame(canvas, x, 3, w, 6);
        if(fill > 0) canvas_draw_box(canvas, x + 1, 4, fill, 4);
    }

    char rssi[12];
    snprintf(rssi, sizeof(rssi), "%d dBm", ui->rssi);
    canvas_draw_str_aligned(canvas, SW_W - 2, SW_HDR_BASE, AlignRight, AlignBottom, rssi);

    canvas_draw_line(canvas, 0, SW_RULE_Y, SW_W - 1, SW_RULE_Y);
}

/* ---------------- the listening screen ---------------- */

/** Rotating coaching lines. A tyre sensor is quiet by design, so the screen
 * has to explain the wait instead of leaving the user staring at nothing. */
static const char* const sweep_hints[] = {
    "Sensors wake when",
    "a wheel turns.",
    "",
    "At rest they speak",
    "about once a minute.",
    "",
    "Park near a road, or",
    "roll the car forward.",
};
#define SWEEP_HINT_PAIRS 4

static void draw_listening(Canvas* canvas, const SweepModel* m) {
    const SweepUi* ui = &m->ui;

    od_ui_draw_wheel(canvas, SW_WHEEL_CX, SW_WHEEL_CY, m->frame / 3);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, SW_INFO_X, 22, "Listening");

    canvas_set_font(canvas, FontSecondary);

    char line[26];
    if(ui->count > 0) {
        /* Heard, but not yet confirmed: say so rather than showing nothing. */
        snprintf(line, sizeof(line), "%u unconfirmed", ui->count);
    } else if(ui->packets > 0) {
        snprintf(line, sizeof(line), "%lu packets", (unsigned long)ui->packets);
    } else {
        char ago[12];
        od_ui_elapsed(ago, sizeof(ago), ui->elapsed_ticks);
        snprintf(line, sizeof(line), "for %s", ago);
    }
    canvas_draw_str(canvas, SW_INFO_X, 33, line);

    /*
     * The radio's own pulse. Edges climbing with no decodes is the signature
     * of "right radio, wrong band" - the single most useful thing this screen
     * can tell someone who is not hearing their car.
     */
    uint32_t k = ui->edges / 1000u;
    if(k > 0) {
        snprintf(line, sizeof(line), "%luk edges", (unsigned long)k);
    } else {
        snprintf(line, sizeof(line), "%lu edges", (unsigned long)ui->edges);
    }
    canvas_draw_str(canvas, SW_INFO_X, 43, line);

    /* A two-line hint that changes every few seconds. */
    uint32_t pair = (m->frame / 40) % SWEEP_HINT_PAIRS;
    canvas_draw_line(canvas, 0, 47, SW_W - 1, 47);
    canvas_draw_str_aligned(
        canvas, SW_W / 2, 55, AlignCenter, AlignBottom, sweep_hints[pair * 2]);
    canvas_draw_str_aligned(
        canvas, SW_W / 2, 63, AlignCenter, AlignBottom, sweep_hints[pair * 2 + 1]);
}

/* ---------------- the sensor list ---------------- */

static void draw_row(Canvas* canvas, int32_t y, const SweepRow* row, uint8_t units, bool selected) {
    if(selected) {
        canvas_draw_box(canvas, 0, y, SW_W - 4, SW_ROW_H - 1);
        canvas_set_color(canvas, ColorWhite);
    }

    int32_t base = y + SW_TEXT_DY;

    /* A remembered sensor gets a filled marker: this one is yours, and it
     * just told the whole street it was here again. */
    if(row->known) {
        canvas_draw_disc(canvas, SW_COL_MARK + 2, y + 6, 2);
    } else {
        canvas_draw_dot(canvas, SW_COL_MARK + 2, y + 6);
    }

    canvas_set_font(canvas, FontSecondary);

    char id[10];
    od_ui_id(id, sizeof(id), row->id, row->id_bits);
    canvas_draw_str(canvas, SW_COL_ID, base, id);

    canvas_draw_str(canvas, SW_COL_TAG, base, od_ui_proto_tag((OdProto)row->proto));

    char press[12];
    od_ui_pressure_short(press, sizeof(press), row->pressure_dkpa, row->pressure_valid, units);
    canvas_draw_str(canvas, SW_COL_PRESS, base, press);

    od_ui_draw_signal(canvas, SW_COL_BARS, y + 2, row->rssi);

    if(selected) canvas_set_color(canvas, ColorBlack);
}

static void draw_list(Canvas* canvas, const SweepUi* ui) {
    /* Keep the cursor on screen without storing a scroll offset: the window
     * is always the SW_ROWS-sized block that contains the selection. */
    uint8_t top = 0;
    if(ui->selected >= SW_ROWS) top = (uint8_t)(ui->selected - (SW_ROWS - 1));
    if(top + SW_ROWS > ui->count && ui->count > SW_ROWS) top = (uint8_t)(ui->count - SW_ROWS);

    for(uint8_t i = 0; i < SW_ROWS; i++) {
        uint8_t index = (uint8_t)(top + i);
        if(index >= ui->count) break;
        draw_row(
            canvas,
            SW_TOP + i * SW_ROW_H,
            &ui->rows[index],
            ui->units,
            index == ui->selected);
    }

    if(ui->count > SW_ROWS) {
        elements_scrollbar_pos(canvas, SW_W, SW_TOP, SW_STRIP_Y - SW_TOP, ui->selected, ui->count);
    }
}

static void draw_strip(Canvas* canvas, const SweepUi* ui) {
    canvas_draw_box(canvas, 0, SW_STRIP_Y, SW_W, SW_STRIP_H);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);

    char left[24];
    snprintf(
        left,
        sizeof(left),
        "%u sensor%s  %u bit",
        ui->count,
        ui->count == 1 ? "" : "s",
        ui->bits);
    canvas_draw_str(canvas, 2, SW_STRIP_BASE, left);

    canvas_draw_str_aligned(
        canvas,
        SW_W - 2,
        SW_STRIP_BASE,
        AlignRight,
        AlignBottom,
        od_expose_level_name((OdExposureLevel)ui->level));

    canvas_set_color(canvas, ColorBlack);
}

/* ---------------- draw ---------------- */

static void sweep_view_draw(Canvas* canvas, void* model) {
    const SweepModel* m = (const SweepModel*)model;
    const SweepUi* ui = &m->ui;

    canvas_clear(canvas);
    draw_header(canvas, ui);

    /* The list only appears once something is confirmed; unconfirmed hits
     * stay on the listening screen as a count, never as a named sensor. */
    if(ui->count == 0) {
        draw_listening(canvas, m);
        return;
    }

    draw_list(canvas, ui);
    draw_strip(canvas, ui);

    if(ui->show_hint) {
        /* A transient legend over the strip, so the controls are discoverable
         * without permanently spending a row on them. */
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, SW_STRIP_Y, SW_W, SW_STRIP_H);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, SW_W / 2, SW_STRIP_BASE, AlignCenter, AlignBottom, "OK detail   > report");
        canvas_set_color(canvas, ColorBlack);
    }
}

/* ---------------- input ---------------- */

static bool sweep_view_input(InputEvent* event, void* context) {
    SweepView* v = (SweepView*)context;
    furi_assert(v);

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool handled = false;
    switch(event->key) {
    case InputKeyUp:
        with_view_model(
            v->view,
            SweepModel * m,
            {
                if(m->ui.count > 0 && m->ui.selected > 0) m->ui.selected--;
            },
            true);
        handled = true;
        break;

    case InputKeyDown:
        with_view_model(
            v->view,
            SweepModel * m,
            {
                if(m->ui.count > 0 && m->ui.selected + 1 < m->ui.count) m->ui.selected++;
            },
            true);
        handled = true;
        break;

    case InputKeyOk: {
        bool any = false;
        with_view_model(v->view, SweepModel * m, { any = m->ui.count > 0; }, false);
        if(any) emit(v, SweepEventOpen);
        handled = true;
        break;
    }

    case InputKeyRight:
        emit(v, SweepEventReport);
        handled = true;
        break;

    case InputKeyLeft:
        emit(v, SweepEventCycleBand);
        handled = true;
        break;

    default:
        break;
    }

    return handled;
}

/* ---------------- lifecycle ---------------- */

SweepView* sweep_view_alloc(void) {
    SweepView* v = malloc(sizeof(SweepView));
    v->cb = NULL;
    v->ctx = NULL;
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, sweep_view_draw);
    view_set_input_callback(v->view, sweep_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(SweepModel));
    with_view_model(v->view, SweepModel * m, { memset(m, 0, sizeof(SweepModel)); }, false);
    return v;
}

void sweep_view_free(SweepView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* sweep_view_get_view(SweepView* v) {
    furi_assert(v);
    return v->view;
}

void sweep_view_set_event_callback(SweepView* v, SweepViewEventCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->ctx = context;
}

void sweep_view_update(SweepView* v, const SweepUi* ui) {
    furi_assert(v);
    with_view_model(
        v->view,
        SweepModel * m,
        {
            /* The cursor belongs to the view, not the publisher: the scene
             * refreshes ten times a second and must not stamp on it. */
            uint8_t selected = m->ui.selected;
            m->ui = *ui;
            if(selected >= ui->count) selected = ui->count ? (uint8_t)(ui->count - 1) : 0;
            m->ui.selected = selected;
            m->frame++;
        },
        true);
}

uint8_t sweep_view_selected(SweepView* v) {
    furi_assert(v);
    uint8_t selected = 0;
    with_view_model(v->view, SweepModel * m, { selected = m->ui.selected; }, false);
    return selected;
}
