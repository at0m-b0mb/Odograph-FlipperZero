#include "learn_view.h"
#include "../helpers/od_ui.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdio.h>
#include <string.h>

#define LV_W 128
#define LV_H 64

#define LV_HDR_BASE 9
#define LV_RULE_Y   11
#define LV_DOTS_Y   61

struct LearnView {
    View* view;
};

typedef struct {
    uint8_t frame;
    uint32_t tick;
} LearnModel;

static const char* const frame_titles[LEARN_FRAMES] = {
    "THE SENSOR",
    "THE PACKET",
    "THE TRAIL",
    "NO OPT-OUT",
    "SO WHAT NOW",
};

/* ---------------- glyphs ---------------- */

/** A receiving mast: post, crossbar, and arcs that light up on cue. */
static void draw_mast(Canvas* canvas, int32_t x, int32_t base_y, bool active) {
    canvas_draw_line(canvas, x, base_y, x, base_y - 12);
    canvas_draw_line(canvas, x - 3, base_y - 12, x + 3, base_y - 12);
    canvas_draw_line(canvas, x - 2, base_y, x + 2, base_y);

    if(active) {
        for(int32_t r = 3; r <= 7; r += 2) {
            canvas_draw_line(canvas, x - r, base_y - 14 - r + 3, x - r + 2, base_y - 16 - r + 3);
            canvas_draw_line(canvas, x + r, base_y - 14 - r + 3, x + r - 2, base_y - 16 - r + 3);
        }
    }
}

/** A car in profile, small enough to drive across the screen. */
static void draw_car(Canvas* canvas, int32_t x, int32_t base_y) {
    canvas_draw_box(canvas, x, base_y - 6, 16, 4);
    canvas_draw_box(canvas, x + 4, base_y - 9, 8, 3);
    canvas_draw_disc(canvas, x + 3, base_y - 1, 2);
    canvas_draw_disc(canvas, x + 12, base_y - 1, 2);
}

/* ---------------- frames ---------------- */

/* 1 - where the radio is. */
static void draw_sensor_frame(Canvas* canvas, uint32_t tick) {
    od_ui_draw_wheel(canvas, 26, 34, tick / 3);

    /* The wheel's outermost wave reaches x=48, so the column starts at 52 -
     * near enough to read as one picture, far enough not to be struck by it. */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 52, 22, "Every tyre has");
    canvas_draw_str(canvas, 52, 31, "a radio inside.");
    canvas_draw_str(canvas, 52, 44, "It wakes when");
    canvas_draw_str(canvas, 52, 53, "the wheel turns.");

    canvas_draw_line(canvas, 0, 56, LV_W - 1, 56);
    canvas_draw_str(canvas, 2, 63, "So does anyone nearby.");
}

/* 2 - what it says. The serial field is the one that matters, so it pulses. */
static void draw_packet_frame(Canvas* canvas, uint32_t tick) {
    const int32_t y = 20;
    const int32_t h = 13;

    struct {
        int32_t x, w;
        const char* label;
    } fields[] = {
        {2, 46, "SERIAL"},
        {50, 26, "kPa"},
        {78, 22, "C"},
        {102, 24, "CRC"},
    };

    canvas_set_font(canvas, FontSecondary);
    for(uint32_t i = 0; i < 4; i++) {
        bool hot = (i == 0) && ((tick / 8) % 2 == 0);
        if(hot) {
            canvas_draw_box(canvas, fields[i].x, y, fields[i].w, h);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_frame(canvas, fields[i].x, y, fields[i].w, h);
        }
        canvas_draw_str_aligned(
            canvas,
            fields[i].x + fields[i].w / 2,
            y + h - 3,
            AlignCenter,
            AlignBottom,
            fields[i].label);
        if(hot) canvas_set_color(canvas, ColorBlack);
    }

    /* the same serial, sent again and again, drifting down the screen. The
     * travel stops short of the closing line rather than sliding under it. */
    uint32_t phase = (tick / 3) % 9;
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 6, 42 + (int32_t)phase, "45BB320F");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 62, "Plain text. No key. Always.");
}

/* 3 - two receivers is all it takes. */
static void draw_trail_frame(Canvas* canvas, uint32_t tick) {
    /* The road sits high enough that the two timestamps and the conclusion
     * both land on screen; at 44 the last line fell off the bottom. */
    const int32_t road = 40;
    canvas_draw_line(canvas, 0, road, LV_W - 1, road);

    uint32_t t = (tick / 2) % 140;
    int32_t car_x = (int32_t)t - 18;

    draw_mast(canvas, 24, road, car_x > 12 && car_x < 34);
    draw_mast(canvas, 100, road, car_x > 88 && car_x < 110);

    if(car_x > -18 && car_x < LV_W) draw_car(canvas, car_x, road);

    /* Kept short so it ends well before the right-hand mast, whose waves
     * climb to y=20 at x=93 and would otherwise strike the last word. */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 20, "Same serial, twice.");

    /* the record those two receivers just wrote */
    canvas_set_font(canvas, FontKeyboard);
    if(car_x > 12) canvas_draw_str(canvas, 2, 52, "A 09:14");
    if(car_x > 88) canvas_draw_str(canvas, 62, 52, "B 09:17");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(
        canvas, 2, 62, car_x > 88 ? "Route, time, speed." : "No camera needed.");
}

static void draw_text_frame(Canvas* canvas, const char* const* lines, uint8_t count) {
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < count; i++) {
        canvas_draw_str(canvas, 3, 21 + i * 9, lines[i]);
    }
}

/* 4 - why it is not going away. */
static const char* const no_optout[] = {
    "Fitted by law: US since",
    "2007, EU since 2014.",
    "Powered whenever you",
    "drive. No pairing, no",
    "encryption, no setting",
};

/* 5 - the honest answer. */
static const char* const so_what[] = {
    "You cannot turn it off,",
    "and shielding a wheel is",
    "not a thing.",
    "What changes it is people",
    "knowing it is there.",
};

/* ---------------- draw ---------------- */

static void learn_view_draw(Canvas* canvas, void* model) {
    LearnModel* m = (LearnModel*)model;

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, LV_HDR_BASE, frame_titles[m->frame]);

    canvas_set_font(canvas, FontSecondary);
    char step[8];
    snprintf(step, sizeof(step), "%u/%u", m->frame + 1, LEARN_FRAMES);
    canvas_draw_str_aligned(canvas, LV_W - 2, LV_HDR_BASE, AlignRight, AlignBottom, step);
    canvas_draw_line(canvas, 0, LV_RULE_Y, LV_W - 1, LV_RULE_Y);

    /* No page dots here: the "3/5" in the header is already the indicator,
     * and every frame wants the bottom row for its closing line. */
    switch(m->frame) {
    case 1:
        draw_packet_frame(canvas, m->tick);
        break;
    case 2:
        draw_trail_frame(canvas, m->tick);
        break;
    case 3:
        draw_text_frame(canvas, no_optout, 5);
        break;
    case 4:
        draw_text_frame(canvas, so_what, 5);
        break;
    default:
        draw_sensor_frame(canvas, m->tick);
        break;
    }
}

/* ---------------- input ---------------- */

static bool learn_view_input(InputEvent* event, void* context) {
    LearnView* v = (LearnView*)context;
    furi_assert(v);

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyLeft:
        with_view_model(
            v->view,
            LearnModel * m,
            {
                m->frame = (uint8_t)((m->frame + LEARN_FRAMES - 1) % LEARN_FRAMES);
                m->tick = 0;
            },
            true);
        return true;

    case InputKeyRight:
    case InputKeyOk:
        with_view_model(
            v->view,
            LearnModel * m,
            {
                m->frame = (uint8_t)((m->frame + 1) % LEARN_FRAMES);
                m->tick = 0;
            },
            true);
        return true;

    default:
        return false;
    }
}

/* ---------------- lifecycle ---------------- */

LearnView* learn_view_alloc(void) {
    LearnView* v = malloc(sizeof(LearnView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, learn_view_draw);
    view_set_input_callback(v->view, learn_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(LearnModel));
    with_view_model(v->view, LearnModel * m, { memset(m, 0, sizeof(LearnModel)); }, false);
    return v;
}

void learn_view_free(LearnView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* learn_view_get_view(LearnView* v) {
    furi_assert(v);
    return v->view;
}

void learn_view_tick(LearnView* v) {
    furi_assert(v);
    with_view_model(v->view, LearnModel * m, { m->tick++; }, true);
}

void learn_view_reset(LearnView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        LearnModel * m,
        {
            m->frame = 0;
            m->tick = 0;
        },
        true);
}
