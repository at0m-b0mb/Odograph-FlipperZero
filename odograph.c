/**
 * Odograph - a licence plate you cannot cover.
 *
 * Every tyre on a modern car carries a pressure sensor with a permanent
 * serial number, and it announces that serial in the clear whenever the wheel
 * turns. Odograph listens on 315 and 433.92 MHz, decodes the serials, and
 * shows what they give away: how many identifying bits are in the air, how
 * many of the world's cars would answer to the same fingerprint, and - if you
 * save one and come back later - that the handle still works days on.
 *
 * Listen-only. There is no transmit path in this application at all.
 */
#include "odograph_i.h"

static bool odograph_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    OdographApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool odograph_back_event_callback(void* context) {
    furi_assert(context);
    OdographApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/** One timer for the whole app; every live scene starts and stops it. */
static void odograph_tick_callback(void* context) {
    OdographApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, OdCustomEventTick);
}

/* ------------------------------------------------------------- helpers -- */

void odograph_apply_settings(OdographApp* app) {
    furi_assert(app);
    od_radio_configure(
        app->radio,
        app->settings.band_index,
        app->settings.profile,
        od_settings_min_hits(&app->settings));
}

void odograph_save_settings(OdographApp* app) {
    furi_assert(app);
    od_settings_save(&app->settings);
}

void odograph_build_exposure(OdographApp* app, bool mine_only, OdExposure* out) {
    furi_assert(app);
    furi_assert(out);

    OdExposeSensor set[OD_MAX_SENSORS];
    uint8_t count = 0;

    uint8_t min_hits = od_settings_min_hits(&app->settings);
    uint8_t confirmed = od_store_confirmed_count(app->store, min_hits);

    for(uint8_t i = 0; i < confirmed && count < OD_MAX_SENSORS; i++) {
        OdSensor s;
        if(!od_store_get_confirmed(app->store, i, min_hits, &s)) break;
        if(mine_only && !s.known) continue;

        set[count].id_bits = s.id_bits;
        set[count].decoded = (s.proto != OdProtoUnknown);
        set[count].returned = (s.returns > 0);
        count++;
    }

    od_expose_compute(set, count, out);
}

/* -------------------------------------------------------------- notify -- */

void odograph_notify_new(OdographApp* app) {
    uint32_t now = furi_get_tick();
    if(now - app->last_alert < furi_ms_to_ticks(ODOGRAPH_ALERT_GAP_MS)) return;
    app->last_alert = now;

    if(app->settings.sound && app->settings.led) {
        notification_message(app->notifications, &sequence_blink_green_100);
        notification_message(app->notifications, &sequence_success);
    } else if(app->settings.sound) {
        notification_message(app->notifications, &sequence_success);
    } else if(app->settings.led) {
        notification_message(app->notifications, &sequence_blink_green_100);
    }
}

void odograph_notify_click(OdographApp* app) {
    if(app->settings.sound) notification_message(app->notifications, &sequence_semi_success);
}

void odograph_notify_save(OdographApp* app) {
    if(app->settings.sound) notification_message(app->notifications, &sequence_single_vibro);
    if(app->settings.led) notification_message(app->notifications, &sequence_blink_blue_100);
}

/* ----------------------------------------------------------- lifecycle -- */

static OdographApp* odograph_app_alloc(void) {
    OdographApp* app = malloc(sizeof(OdographApp));
    memset(app, 0, sizeof(OdographApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&odograph_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, odograph_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, odograph_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, OdographViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        OdographViewSettings,
        variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, OdographViewText, widget_get_view(app->widget));

    app->sweep_view = sweep_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, OdographViewSweep, sweep_view_get_view(app->sweep_view));

    app->sensor_view = sensor_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, OdographViewSensor, sensor_view_get_view(app->sensor_view));

    app->expose_view = expose_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, OdographViewExpose, expose_view_get_view(app->expose_view));

    app->learn_view = learn_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, OdographViewLearn, learn_view_get_view(app->learn_view));

    od_settings_load(&app->settings);

    app->store = od_store_alloc();
    od_store_garage_load(app->store);

    app->radio = od_radio_alloc(app->view_dispatcher, OdCustomEventConfirmed, app->store);
    odograph_apply_settings(app);

    app->timer = furi_timer_alloc(odograph_tick_callback, FuriTimerTypePeriodic, app);

    return app;
}

static void odograph_app_free(OdographApp* app) {
    furi_assert(app);

    /* The radio owns a thread that writes into the store, so it goes first
     * and is joined before anything it touches is released. */
    od_radio_free(app->radio);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);

    view_dispatcher_remove_view(app->view_dispatcher, OdographViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, OdographViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, OdographViewText);
    view_dispatcher_remove_view(app->view_dispatcher, OdographViewSweep);
    view_dispatcher_remove_view(app->view_dispatcher, OdographViewSensor);
    view_dispatcher_remove_view(app->view_dispatcher, OdographViewExpose);
    view_dispatcher_remove_view(app->view_dispatcher, OdographViewLearn);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    sweep_view_free(app->sweep_view);
    sensor_view_free(app->sensor_view);
    expose_view_free(app->expose_view);
    learn_view_free(app->learn_view);

    od_store_free(app->store);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t odograph_app(void* p) {
    UNUSED(p);

    OdographApp* app = odograph_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, OdographSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    odograph_app_free(app);
    return 0;
}
