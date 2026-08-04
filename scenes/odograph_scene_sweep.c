#include "../odograph_i.h"

/**
 * The listening screen. Owns the radio for as long as it is on screen, and
 * republishes the store to the view on a timer rather than on every decoded
 * packet - a burst can be ten packets in a few milliseconds, and the GUI has
 * no business being woken ten times for one car.
 */

/* Scene state, so pushing the sensor detail can be told apart from leaving.
 * scene_manager_next_scene() runs this scene's on_exit either way, and the
 * radio must survive the detour. */
typedef enum {
    SweepStateLeaving = 0,
    SweepStateDetour,
} SweepState;

static void odograph_scene_sweep_view_cb(void* context, SweepViewEvent event) {
    OdographApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, OdCustomEventViewBase + event);
}

static void sweep_publish(OdographApp* app) {
    SweepUi ui;
    memset(&ui, 0, sizeof(ui));

    uint8_t band = od_radio_band(app->radio);
    uint8_t profile = od_radio_profile(app->radio);
    strncpy(ui.band, od_bands[band].label, sizeof(ui.band) - 1);
    strncpy(ui.profile, od_profile_names[profile], sizeof(ui.profile) - 1);
    ui.rssi = (int8_t)od_radio_rssi(app->radio);
    ui.auto_mode = (app->settings.profile == OdProfileAuto);
    ui.hop_ms = od_radio_hop_remaining(app->radio);

    ui.edges = od_radio_edges(app->radio);
    ui.bursts = od_radio_bursts(app->radio);
    ui.packets = od_store_packets(app->store);
    ui.elapsed_ticks = furi_get_tick() - app->scan_started;
    ui.units = app->settings.units;
    ui.show_hint = furi_get_tick() < app->hint_until;

    uint8_t min_hits = od_settings_min_hits(&app->settings);
    uint8_t confirmed = od_store_confirmed_count(app->store, min_hits);
    if(confirmed > SWEEP_MAX_ROWS) confirmed = SWEEP_MAX_ROWS;

    for(uint8_t i = 0; i < confirmed; i++) {
        OdSensor s;
        if(!od_store_get_confirmed(app->store, i, min_hits, &s)) break;
        SweepRow* row = &ui.rows[ui.count++];
        row->id = s.id;
        row->id_bits = s.id_bits;
        row->proto = (uint8_t)s.proto;
        row->pressure_dkpa = s.pressure_dkpa;
        row->pressure_valid = s.pressure_valid;
        row->rssi = s.rssi;
        row->known = s.known;
        row->hits = s.hits;
    }

    /* The footer carries the same verdict the report screen shows, so the
     * headline number is never more than a glance away. */
    OdExposure exposure;
    odograph_build_exposure(app, false, &exposure);
    ui.bits = exposure.bits;
    ui.level = (uint8_t)exposure.level;

    sweep_view_update(app->sweep_view, &ui);
}

void odograph_scene_sweep_on_enter(void* context) {
    OdographApp* app = context;

    scene_manager_set_scene_state(app->scene_manager, OdographSceneSweep, SweepStateLeaving);
    sweep_view_set_event_callback(app->sweep_view, odograph_scene_sweep_view_cb, app);

    if(!od_radio_is_running(app->radio)) {
        odograph_apply_settings(app);
        od_radio_start(app->radio);
        app->scan_started = furi_get_tick();
    }

    app->hint_until = furi_get_tick() + furi_ms_to_ticks(ODOGRAPH_HINT_MS);
    furi_timer_start(app->timer, furi_ms_to_ticks(ODOGRAPH_TICK_MS));

    sweep_publish(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, OdographViewSweep);
}

bool odograph_scene_sweep_on_event(void* context, SceneManagerEvent event) {
    OdographApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == OdCustomEventTick) {
            sweep_publish(app);
            consumed = true;
        } else if(event.event == OdCustomEventConfirmed) {
            odograph_notify_new(app);
            consumed = true;
        } else if(event.event >= OdCustomEventViewBase) {
            SweepViewEvent ev = (SweepViewEvent)(event.event - OdCustomEventViewBase);
            switch(ev) {
            case SweepEventOpen: {
                uint8_t index = sweep_view_selected(app->sweep_view);
                uint8_t min_hits = od_settings_min_hits(&app->settings);
                app->selected_valid =
                    od_store_get_confirmed(app->store, index, min_hits, &app->selected);
                if(app->selected_valid) {
                    app->from_garage = false;
                    scene_manager_set_scene_state(
                        app->scene_manager, OdographSceneSweep, SweepStateDetour);
                    scene_manager_next_scene(app->scene_manager, OdographSceneSensor);
                }
                consumed = true;
                break;
            }

            case SweepEventReport:
                scene_manager_set_scene_state(
                    app->scene_manager, OdographSceneSweep, SweepStateDetour);
                scene_manager_next_scene(app->scene_manager, OdographSceneExpose);
                consumed = true;
                break;

            case SweepEventCycleBand:
                /* Cycling the band from the scan screen keeps the radio
                 * running: the worker adopts the change on its next pass. */
                app->settings.band_index =
                    (uint8_t)((app->settings.band_index + 1) % OD_BAND_COUNT);
                if(app->settings.profile == OdProfileAuto) {
                    app->settings.profile = OdProfileFsk;
                }
                odograph_apply_settings(app);
                odograph_save_settings(app);
                odograph_notify_click(app);
                sweep_publish(app);
                consumed = true;
                break;

            default:
                break;
            }
        }
    }

    return consumed;
}

void odograph_scene_sweep_on_exit(void* context) {
    OdographApp* app = context;

    furi_timer_stop(app->timer);

    /* Only tear the radio down when the user is actually leaving. Pushing the
     * sensor or report screen runs this handler too, and killing the receiver
     * there would drop the very transmissions the user came to see. */
    if(scene_manager_get_scene_state(app->scene_manager, OdographSceneSweep) ==
       SweepStateLeaving) {
        od_radio_stop(app->radio);
    }
}
