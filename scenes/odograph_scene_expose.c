#include "../odograph_i.h"

static void odograph_scene_expose_view_cb(void* context, ExposeViewEvent event) {
    OdographApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, OdCustomEventViewBase + event);
}

static void expose_publish(OdographApp* app) {
    bool mine_only = scene_manager_get_scene_state(app->scene_manager, OdographSceneExpose) != 0;

    ExposeUi ui;
    memset(&ui, 0, sizeof(ui));
    ui.mine_only = mine_only;
    ui.garage_available = od_store_garage_count(app->store) > 0;
    ui.frame = furi_get_tick() / furi_ms_to_ticks(ODOGRAPH_TICK_MS);
    odograph_build_exposure(app, mine_only, &ui.exposure);

    expose_view_update(app->expose_view, &ui);
}

void odograph_scene_expose_on_enter(void* context) {
    OdographApp* app = context;

    expose_view_set_event_callback(app->expose_view, odograph_scene_expose_view_cb, app);
    expose_view_reset(app->expose_view);
    scene_manager_set_scene_state(app->scene_manager, OdographSceneExpose, 0);

    furi_timer_start(app->timer, furi_ms_to_ticks(ODOGRAPH_TICK_MS));
    expose_publish(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, OdographViewExpose);
}

bool odograph_scene_expose_on_event(void* context, SceneManagerEvent event) {
    OdographApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == OdCustomEventTick || event.event == OdCustomEventConfirmed) {
            expose_publish(app);
            consumed = true;
        } else if(event.event >= OdCustomEventViewBase) {
            ExposeViewEvent ev = (ExposeViewEvent)(event.event - OdCustomEventViewBase);
            if(ev == ExposeEventToggleScope) {
                /* Flip between "everything in earshot" and "just my car". The
                 * second is the one that lands, because it is theirs. */
                uint32_t state =
                    scene_manager_get_scene_state(app->scene_manager, OdographSceneExpose);
                scene_manager_set_scene_state(
                    app->scene_manager, OdographSceneExpose, state ? 0 : 1);
                odograph_notify_click(app);
                expose_publish(app);
                consumed = true;
            }
        }
    }

    return consumed;
}

void odograph_scene_expose_on_exit(void* context) {
    OdographApp* app = context;
    furi_timer_stop(app->timer);
}
