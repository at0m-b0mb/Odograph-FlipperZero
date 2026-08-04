#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "odograph_icons.h" // generated from icons/ by fbt

#include "helpers/od_tpms.h"
#include "helpers/od_demod.h"
#include "helpers/od_expose.h"
#include "helpers/od_settings.h"
#include "helpers/od_store.h"
#include "helpers/od_radio.h"
#include "helpers/od_ui.h"
#include "views/sweep_view.h"
#include "views/sensor_view.h"
#include "views/expose_view.h"
#include "views/learn_view.h"
#include "scenes/odograph_scene.h"

#define ODOGRAPH_VERSION "1.0"

/** Screen refresh while the radio is live, in ms. */
#define ODOGRAPH_TICK_MS 120

/** How long the control legend stays up when a screen opens, in ms. */
#define ODOGRAPH_HINT_MS 2800

/** Floor on the gap between two "new sensor" chirps, in ms. */
#define ODOGRAPH_ALERT_GAP_MS 700

typedef enum {
    OdographViewSubmenu,
    OdographViewSweep,
    OdographViewSensor,
    OdographViewExpose,
    OdographViewLearn,
    OdographViewSettings,
    OdographViewText,
} OdographViewId;

typedef enum {
    /** Periodic redraw, posted by the app timer. */
    OdCustomEventTick = 100,
    /** A serial just crossed the confirmation threshold. */
    OdCustomEventConfirmed,
    /** View events arrive offset from here so a scene decodes them with one
     * subtraction, the same way the rest of this family does it. */
    OdCustomEventViewBase = 200,
} OdCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;

    SweepView* sweep_view;
    SensorView* sensor_view;
    ExposeView* expose_view;
    LearnView* learn_view;

    OdStore* store;
    OdRadio* radio;
    OdSettings settings;

    /** Drives redraws while a live screen is up. */
    FuriTimer* timer;

    /** The sensor the detail screen is showing, copied out of the store so
     * the radio thread cannot move it under the GUI mid-draw. */
    OdSensor selected;
    bool selected_valid;

    /** Where the sensor screen was opened from, so Back goes back there. */
    bool from_garage;

    /** Transient confirmation on the sensor screen: what to say, and until
     * when. Kept on the app rather than in the scene state so the message and
     * its deadline cannot drift apart. */
    uint32_t flash_until;
    uint8_t flash_kind;

    uint32_t scan_started;
    uint32_t last_alert;
    uint32_t hint_until;
} OdographApp;

/** Push the current settings into the radio. */
void odograph_apply_settings(OdographApp* app);

/** Persist settings to the SD card (best effort). */
void odograph_save_settings(OdographApp* app);

/** Build the exposure report over everything confirmed this session, or over
 * the garage sensors only when @p mine_only. */
void odograph_build_exposure(OdographApp* app, bool mine_only, OdExposure* out);

/* feedback, gated by settings */
void odograph_notify_new(OdographApp* app);
void odograph_notify_click(OdographApp* app);
void odograph_notify_save(OdographApp* app);
