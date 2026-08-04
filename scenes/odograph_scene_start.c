#include "../odograph_i.h"

typedef enum {
    StartIndexSweep,
    StartIndexExpose,
    StartIndexGarage,
    StartIndexLearn,
    StartIndexSettings,
    StartIndexAbout,
} StartIndex;

static void odograph_scene_start_submenu_cb(void* context, uint32_t index) {
    OdographApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void odograph_scene_start_on_enter(void* context) {
    OdographApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Odograph");
    submenu_add_item(submenu, "Listen", StartIndexSweep, odograph_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "Exposure report", StartIndexExpose, odograph_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Garage", StartIndexGarage, odograph_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "How this works", StartIndexLearn, odograph_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "Settings", StartIndexSettings, odograph_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "About", StartIndexAbout, odograph_scene_start_submenu_cb, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, OdographSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, OdographViewSubmenu);
}

bool odograph_scene_start_on_event(void* context, SceneManagerEvent event) {
    OdographApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, OdographSceneStart, event.event);
        switch(event.event) {
        case StartIndexSweep:
            scene_manager_next_scene(app->scene_manager, OdographSceneSweep);
            consumed = true;
            break;
        case StartIndexExpose:
            scene_manager_next_scene(app->scene_manager, OdographSceneExpose);
            consumed = true;
            break;
        case StartIndexGarage:
            scene_manager_next_scene(app->scene_manager, OdographSceneGarage);
            consumed = true;
            break;
        case StartIndexLearn:
            scene_manager_next_scene(app->scene_manager, OdographSceneLearn);
            consumed = true;
            break;
        case StartIndexSettings:
            scene_manager_next_scene(app->scene_manager, OdographSceneSettings);
            consumed = true;
            break;
        case StartIndexAbout:
            scene_manager_next_scene(app->scene_manager, OdographSceneAbout);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void odograph_scene_start_on_exit(void* context) {
    OdographApp* app = context;
    submenu_reset(app->submenu);
}
