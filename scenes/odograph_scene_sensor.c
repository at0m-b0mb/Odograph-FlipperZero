#include "../odograph_i.h"

/** How long the "added / removed" confirmation sits on the plate, in ms. */
#define SENSOR_FLASH_MS 1400

/** Which message the flash is showing, held in the scene state's low bits so
 * the whole thing stays in one word alongside the deadline. */
typedef enum {
    SensorFlashSaved = 0,
    SensorFlashForgot,
    SensorFlashFull,
} SensorFlashKind;

static void odograph_scene_sensor_view_cb(void* context, SensorViewEvent event) {
    OdographApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, OdCustomEventViewBase + event);
}

/** Pull the live row for this serial, if the radio is still hearing it. */
static void sensor_refresh(OdographApp* app) {
    uint8_t count = od_store_count(app->store);
    for(uint8_t i = 0; i < count; i++) {
        OdSensor s;
        if(!od_store_get(app->store, i, &s)) continue;
        if(s.id == app->selected.id && s.proto == app->selected.proto) {
            app->selected = s;
            return;
        }
    }
}

static void sensor_publish(OdographApp* app) {
    SensorUi ui;
    memset(&ui, 0, sizeof(ui));
    ui.sensor = app->selected;
    ui.units = app->settings.units;
    ui.temp_f = app->settings.temp_f;
    ui.now_tick = furi_get_tick();
    ui.show_hint = furi_get_tick() < app->hint_until;

    if(app->flash_until && furi_get_tick() < app->flash_until) {
        ui.flash_saved = (app->flash_kind == SensorFlashSaved);
        ui.flash_forgot = (app->flash_kind == SensorFlashForgot);
        ui.flash_full = (app->flash_kind == SensorFlashFull);
    }

    sensor_view_update(app->sensor_view, &ui);
}

void odograph_scene_sensor_on_enter(void* context) {
    OdographApp* app = context;

    sensor_view_set_event_callback(app->sensor_view, odograph_scene_sensor_view_cb, app);
    sensor_view_reset(app->sensor_view);
    app->flash_until = 0;

    app->hint_until = furi_get_tick() + furi_ms_to_ticks(ODOGRAPH_HINT_MS);
    furi_timer_start(app->timer, furi_ms_to_ticks(ODOGRAPH_TICK_MS));

    sensor_publish(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, OdographViewSensor);
}

bool odograph_scene_sensor_on_event(void* context, SceneManagerEvent event) {
    OdographApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == OdCustomEventTick) {
            sensor_refresh(app);
            sensor_publish(app);
            consumed = true;
        } else if(event.event == OdCustomEventConfirmed) {
            /* Another sensor confirmed while this one is open. Nothing to do
             * here, but it must not fall through to the scene manager. */
            consumed = true;
        } else if(event.event >= OdCustomEventViewBase) {
            SensorViewEvent ev = (SensorViewEvent)(event.event - OdCustomEventViewBase);
            if(ev == SensorEventToggleGarage) {
                /* Toggling reports only what the garage holds afterwards, so
                 * a refused add and a genuine removal look identical from
                 * here. Ask first, and say "full" rather than "removed". */
                bool was_known = app->selected.known;
                bool no_room =
                    !was_known && od_store_garage_count(app->store) >= OD_GARAGE_MAX;

                if(no_room) {
                    app->flash_kind = SensorFlashFull;
                } else {
                    char label[14];
                    od_ui_id(label, sizeof(label), app->selected.id, app->selected.id_bits);
                    bool remembered = od_store_garage_toggle(
                        app->store, app->selected.id, app->selected.proto, label);
                    app->selected.known = remembered;
                    app->flash_kind = remembered ? SensorFlashSaved : SensorFlashForgot;
                }

                app->flash_until = furi_get_tick() + furi_ms_to_ticks(SENSOR_FLASH_MS);
                odograph_notify_save(app);
                sensor_publish(app);
                consumed = true;
            }
        }
    }

    return consumed;
}

void odograph_scene_sensor_on_exit(void* context) {
    OdographApp* app = context;
    furi_timer_stop(app->timer);
}
