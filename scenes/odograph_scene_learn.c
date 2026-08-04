#include "../odograph_i.h"

void odograph_scene_learn_on_enter(void* context) {
    OdographApp* app = context;

    learn_view_reset(app->learn_view);
    furi_timer_start(app->timer, furi_ms_to_ticks(ODOGRAPH_TICK_MS));
    view_dispatcher_switch_to_view(app->view_dispatcher, OdographViewLearn);
}

bool odograph_scene_learn_on_event(void* context, SceneManagerEvent event) {
    OdographApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == OdCustomEventTick) {
            learn_view_tick(app->learn_view);
            consumed = true;
        } else if(event.event == OdCustomEventConfirmed) {
            consumed = true;
        }
    }

    return consumed;
}

void odograph_scene_learn_on_exit(void* context) {
    OdographApp* app = context;
    furi_timer_stop(app->timer);
}
