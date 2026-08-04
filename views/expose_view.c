#include "expose_view.h"
#include "../helpers/od_ui.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

#define EV_W 128
#define EV_H 64

#define EV_HDR_BASE 9
#define EV_RULE_Y   11

/* Same budget the sensor screen keeps: content stops at 57, the last row is
 * the page dots and nothing else. */
#define EV_CONTENT_BOTTOM 57
#define EV_DOTS_Y         61

struct ExposeView {
    View* view;
    ExposeViewEventCallback cb;
    void* ctx;
};

typedef struct {
    ExposeUi ui;
    uint8_t page;
} ExposeModel;

static void emit(ExposeView* v, ExposeViewEvent event) {
    if(v->cb) v->cb(v->ctx, event);
}

/* ---------------- pages ---------------- */

static void draw_verdict(Canvas* canvas, const ExposeUi* ui) {
    const OdExposure* e = &ui->exposure;

    od_ui_draw_badge(canvas, EV_W / 2, 17, od_expose_level_name(e->level));

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, EV_W / 2, 43, AlignCenter, AlignBottom, od_expose_level_hint(e->level));

    char line[40];
    if(e->sensors == 0) {
        snprintf(line, sizeof(line), "Nothing decoded yet");
    } else if(e->decoded == 0) {
        snprintf(line, sizeof(line), "%u pattern(s), no serial", e->unknown);
    } else {
        snprintf(
            line,
            sizeof(line),
            "%u serial%s  %u bits",
            e->decoded,
            e->decoded == 1 ? "" : "s",
            e->bits);
    }
    canvas_draw_str_aligned(canvas, EV_W / 2, 52, AlignCenter, AlignBottom, line);

    if(e->persistence) {
        /* The strongest thing this app can say, so it takes the whole last
         * row - dots included, which is why the caller suppresses them. */
        canvas_draw_box(canvas, 0, 55, EV_W, 9);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(
            canvas, EV_W / 2, 63, AlignCenter, AlignBottom, "IT CAME BACK");
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_row(Canvas* canvas, int32_t y, const char* label, const char* value) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, y, label);
    canvas_draw_str_aligned(canvas, EV_W - 3, y, AlignRight, AlignBottom, value);
}

static void draw_numbers(Canvas* canvas, const ExposeUi* ui) {
    const OdExposure* e = &ui->exposure;
    char value[24];

    snprintf(value, sizeof(value), "%u", e->sensors);
    draw_row(canvas, 20, "Sensors heard", value);

    snprintf(value, sizeof(value), "%u decoded", e->decoded);
    draw_row(canvas, 29, "Readable serials", value);

    snprintf(value, sizeof(value), "%u bits", e->bits);
    draw_row(canvas, 38, "Identity on air", value);

    /*
     * The line that lands: how many of the world's cars would answer to this
     * same set of serials. Past 31 bits the honest answer is "none".
     */
    if(e->decoded == 0) {
        draw_row(canvas, 47, "Cars that match", "unknown");
    } else if(e->alone) {
        draw_row(canvas, 47, "Others that match", "none on earth");
    } else {
        snprintf(value, sizeof(value), "~%lu", (unsigned long)e->peers);
        draw_row(canvas, 47, "Others that match", value);
    }

    canvas_draw_line(canvas, 0, 50, EV_W - 1, 50);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, EV_CONTENT_BOTTOM, "of 1.5 billion vehicles");
}

/** Honest, and short enough to read on a 64 px screen. */
static const char* const meaning_lines[] = {
    "A tyre sensor's serial is",
    "fixed for the life of the",
    "sensor. No encryption, no",
    "rotation, no opt-out. It",
    "is a plate you cannot",
    "cover, readable from a",
    "parked car or a gantry.",
};
#define MEANING_LINES (sizeof(meaning_lines) / sizeof(meaning_lines[0]))

static void draw_meaning(Canvas* canvas, const ExposeUi* ui) {
    canvas_set_font(canvas, FontSecondary);

    /* Scrolls itself: seven lines will not sit still on four rows. */
    uint32_t step = (ui->frame / 24) % (MEANING_LINES + 1);
    for(uint32_t i = 0; i < 4; i++) {
        uint32_t index = step + i;
        if(index >= MEANING_LINES) break;
        canvas_draw_str(canvas, 3, 20 + (int32_t)i * 9, meaning_lines[index]);
    }

    canvas_draw_line(canvas, 0, 50, EV_W - 1, 50);
    canvas_draw_str(canvas, 3, EV_CONTENT_BOTTOM, "Knowing is the defence.");
}

/* ---------------- draw ---------------- */

static void expose_view_draw(Canvas* canvas, void* model) {
    ExposeModel* m = (ExposeModel*)model;
    const ExposeUi* ui = &m->ui;

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, EV_HDR_BASE, "EXPOSURE");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        EV_W - 2,
        EV_HDR_BASE,
        AlignRight,
        AlignBottom,
        ui->mine_only ? "my car" : "all heard");
    canvas_draw_line(canvas, 0, EV_RULE_Y, EV_W - 1, EV_RULE_Y);

    switch(m->page) {
    case ExposePageNumbers:
        draw_numbers(canvas, ui);
        break;
    case ExposePageMeaning:
        draw_meaning(canvas, ui);
        break;
    default:
        draw_verdict(canvas, ui);
        break;
    }

    if(m->page != ExposePageVerdict || !ui->exposure.persistence) {
        od_ui_draw_dots(canvas, EV_W / 2, EV_DOTS_Y, ExposePageCount, m->page);
    }
}

/* ---------------- input ---------------- */

static bool expose_view_input(InputEvent* event, void* context) {
    ExposeView* v = (ExposeView*)context;
    furi_assert(v);

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyLeft:
        with_view_model(
            v->view,
            ExposeModel * m,
            { m->page = (uint8_t)((m->page + ExposePageCount - 1) % ExposePageCount); },
            true);
        return true;

    case InputKeyRight:
        with_view_model(
            v->view,
            ExposeModel * m,
            { m->page = (uint8_t)((m->page + 1) % ExposePageCount); },
            true);
        return true;

    case InputKeyOk:
        emit(v, ExposeEventToggleScope);
        return true;

    default:
        return false;
    }
}

/* ---------------- lifecycle ---------------- */

ExposeView* expose_view_alloc(void) {
    ExposeView* v = malloc(sizeof(ExposeView));
    v->cb = NULL;
    v->ctx = NULL;
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, expose_view_draw);
    view_set_input_callback(v->view, expose_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ExposeModel));
    with_view_model(v->view, ExposeModel * m, { memset(m, 0, sizeof(ExposeModel)); }, false);
    return v;
}

void expose_view_free(ExposeView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* expose_view_get_view(ExposeView* v) {
    furi_assert(v);
    return v->view;
}

void expose_view_set_event_callback(ExposeView* v, ExposeViewEventCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->ctx = context;
}

void expose_view_update(ExposeView* v, const ExposeUi* ui) {
    furi_assert(v);
    with_view_model(v->view, ExposeModel * m, { m->ui = *ui; }, true);
}

void expose_view_reset(ExposeView* v) {
    furi_assert(v);
    with_view_model(v->view, ExposeModel * m, { m->page = ExposePageVerdict; }, true);
}
